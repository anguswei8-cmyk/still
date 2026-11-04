#include "ui/i18n.h"
#include "platform.h"

#import <Cocoa/Cocoa.h>
#import <Vision/Vision.h>
#import <Carbon/Carbon.h>
#import <ApplicationServices/ApplicationServices.h>
#include <QBuffer>
#include <QCoreApplication>
#include <QScreen>
#include <unistd.h>

namespace pixclone {
bool excludeWindowFromCapture(quintptr) { return false; }

quintptr currentPasteTarget() { return quintptr(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier); }
bool activatePasteTarget(quintptr target) {
    @autoreleasepool {
        NSRunningApplication *application = [NSRunningApplication runningApplicationWithProcessIdentifier:pid_t(target)];
        return application && [application activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    }
}
bool pasteToTarget(quintptr target, QString *error) {
    if (!target || currentPasteTarget() != target) { *error = t("目标应用已切换，内容已复制，请手动粘贴"); return false; }
    if (!AXIsProcessTrusted()) { *error = t("自动粘贴需要系统辅助功能权限；内容已复制，可按 Command+V"); return false; }
    const auto flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
    if (flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift)) {
        *error = t("请松开快捷键后手动粘贴，内容已复制"); return false;
    }
    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, kVK_ANSI_V, true);
    CGEventRef up = CGEventCreateKeyboardEvent(nullptr, kVK_ANSI_V, false);
    if (!down || !up) { if (down) CFRelease(down); if (up) CFRelease(up); *error = t("无法发送粘贴按键"); return false; }
    CGEventSetFlags(down, kCGEventFlagMaskCommand);
    CGEventSetFlags(up, kCGEventFlagMaskCommand);
    CGEventPost(kCGHIDEventTap, down); CGEventPost(kCGHIDEventTap, up);
    CFRelease(down); CFRelease(up);
    return true;
}

bool requestCapturePermission() {
    if (@available(macOS 10.15, *)) {
        if (!CGPreflightScreenCaptureAccess()) return CGRequestScreenCaptureAccess();
    }
    return true;
}

OcrResult recognizeNative(const QImage &image, const QString &language) {
    @autoreleasepool {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) return {{}, t("无法编码 OCR 图像")};
        NSData *data = [NSData dataWithBytes:png.constData() length:png.size()];
        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = YES;
        NSString *requestedLanguage = [NSString stringWithUTF8String:language.toUtf8().constData()];
        NSError *error = nil;
        NSArray<NSString *> *supported = [request supportedRecognitionLanguagesAndReturnError:&error];
        if ([supported containsObject:requestedLanguage]) request.recognitionLanguages = @[requestedLanguage];
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithData:data options:@{}];
        if (![handler performRequests:@[request] error:&error])
            return {{}, QString::fromUtf8(error.localizedDescription.UTF8String)};
        OcrResult output;
        int line = 0;
        for (VNRecognizedTextObservation *observation in request.results) {
            VNRecognizedText *candidate = [observation topCandidates:1].firstObject;
            if (!candidate) continue;
            NSString *text = candidate.string;
            // Vision's accurate mode may return word boxes for multiple characters.
            // Merge equal boxes rather than pretending to offer exact character geometry.
            for (NSUInteger offset = 0; offset < text.length;) {
                NSRange range = [text rangeOfComposedCharacterSequenceAtIndex:offset];
                NSString *part = [text substringWithRange:range];
                offset = NSMaxRange(range);
                if ([[part stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] length] == 0) continue;
                VNRectangleObservation *rectangle = [candidate boundingBoxForRange:range error:nil];
                if (!rectangle) continue;
                CGRect box = rectangle.boundingBox;
                QRectF bounds(box.origin.x * image.width(), (1 - box.origin.y - box.size.height) * image.height(),
                              box.size.width * image.width(), box.size.height * image.height());
                QString value = QString::fromUtf8(part.UTF8String);
                if (!output.words.isEmpty() && output.words.last().line == line && output.words.last().bounds == bounds)
                    output.words.last().text += value;
                else output.words.append({value, bounds, line});
            }
            ++line;
        }
        return output;
    }
}

QVector<QRect> windowRectangles(QScreen *screen) {
    QVector<QRect> rectangles;
    NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    for (NSDictionary *window in windows) {
        if ([window[(id)kCGWindowOwnerPID] intValue] == getpid() || [window[(id)kCGWindowLayer] intValue] != 0) continue;
        CGRect bounds;
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)window[(id)kCGWindowBounds], &bounds)) continue;
        QRect rect(qRound(bounds.origin.x), qRound(bounds.origin.y), qRound(bounds.size.width), qRound(bounds.size.height));
        rect = rect.translated(-screen->geometry().topLeft()).intersected(QRect(QPoint(), screen->size()));
        if (rect.width() >= 20 && rect.height() >= 20) rectangles.append(rect);
    }
    return rectangles;
}

static OSStatus hotkeyCallback(EventHandlerCallRef, EventRef event, void *context) {
    EventHotKeyID key{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(key), nullptr, &key) != noErr
        || key.signature != UInt32(reinterpret_cast<quintptr>(context))) return eventNotHandledErr;
    return static_cast<Hotkeys *>(context)->dispatchNative(int(key.id)) ? noErr : eventNotHandledErr;
}

Hotkeys::Hotkeys(QObject *parent) : QObject(parent) {
    EventTypeSpec event{kEventClassKeyboard, kEventHotKeyPressed};
    EventHandlerRef handler = nullptr;
    if (InstallApplicationEventHandler(hotkeyCallback, 1, &event, this, &handler) == noErr) handler_ = handler;
}

Hotkeys::~Hotkeys() {
    discardStaged();
    for (int id : registrations_.keys()) unbind(id);
    if (handler_) RemoveEventHandler(static_cast<EventHandlerRef>(handler_));
}

quintptr Hotkeys::registerSequence(const QKeySequence &sequence, int nativeId) {
    if (!handler_ || sequence.count() != 1) return 0;
    const auto key = sequence[0];
    const QMap<int, UInt32> codes{
        {Qt::Key_A, kVK_ANSI_A}, {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_C, kVK_ANSI_C}, {Qt::Key_D, kVK_ANSI_D},
        {Qt::Key_E, kVK_ANSI_E}, {Qt::Key_F, kVK_ANSI_F}, {Qt::Key_G, kVK_ANSI_G}, {Qt::Key_H, kVK_ANSI_H},
        {Qt::Key_I, kVK_ANSI_I}, {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K}, {Qt::Key_L, kVK_ANSI_L},
        {Qt::Key_M, kVK_ANSI_M}, {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_O, kVK_ANSI_O}, {Qt::Key_P, kVK_ANSI_P},
        {Qt::Key_Q, kVK_ANSI_Q}, {Qt::Key_R, kVK_ANSI_R}, {Qt::Key_S, kVK_ANSI_S}, {Qt::Key_T, kVK_ANSI_T},
        {Qt::Key_U, kVK_ANSI_U}, {Qt::Key_V, kVK_ANSI_V}, {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_X, kVK_ANSI_X},
        {Qt::Key_Y, kVK_ANSI_Y}, {Qt::Key_Z, kVK_ANSI_Z}, {Qt::Key_0, kVK_ANSI_0}, {Qt::Key_1, kVK_ANSI_1},
        {Qt::Key_2, kVK_ANSI_2}, {Qt::Key_3, kVK_ANSI_3}, {Qt::Key_4, kVK_ANSI_4}, {Qt::Key_5, kVK_ANSI_5},
        {Qt::Key_6, kVK_ANSI_6}, {Qt::Key_7, kVK_ANSI_7}, {Qt::Key_8, kVK_ANSI_8}, {Qt::Key_9, kVK_ANSI_9},
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3}, {Qt::Key_F4, kVK_F4},
        {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6}, {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8},
        {Qt::Key_F9, kVK_F9}, {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12}, {Qt::Key_Space, kVK_Space}};
    if (!codes.contains(key.key())) return false;
    UInt32 modifiers = 0;
    // Qt maps ControlModifier to Command on macOS; MetaModifier denotes physical Control.
    if (key.keyboardModifiers() & Qt::ControlModifier) modifiers |= cmdKey;
    if (key.keyboardModifiers() & Qt::MetaModifier) modifiers |= controlKey;
    if (key.keyboardModifiers() & Qt::AltModifier) modifiers |= optionKey;
    if (key.keyboardModifiers() & Qt::ShiftModifier) modifiers |= shiftKey;
    EventHotKeyRef ref = nullptr;
    const EventHotKeyID identifier{UInt32(reinterpret_cast<quintptr>(this)), UInt32(nativeId)};
    if (RegisterEventHotKey(codes.value(key.key()), modifiers, identifier, GetApplicationEventTarget(), 0, &ref) != noErr) return 0;
    return reinterpret_cast<quintptr>(ref);
}

void Hotkeys::releaseRegistration(quintptr handle) { UnregisterEventHotKey(reinterpret_cast<EventHotKeyRef>(handle)); }
bool Hotkeys::nativeEventFilter(const QByteArray &, void *, qintptr *) { return false; }

} // namespace pixclone
