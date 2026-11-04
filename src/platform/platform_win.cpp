#include "ui/i18n.h"
#include "platform.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QScreen>
#include <QOperatingSystemVersion>
#include <QTemporaryDir>

namespace pixclone {

quintptr currentPasteTarget() { return reinterpret_cast<quintptr>(GetForegroundWindow()); }
bool excludeWindowFromCapture(quintptr windowId) {
    // Older Windows treats this flag as a black rectangle instead of excluding it.
    if (!windowId || QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows10_2004) return false;
    BOOL composed = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composed)) || !composed) return false;
    return SetWindowDisplayAffinity(reinterpret_cast<HWND>(windowId), WDA_EXCLUDEFROMCAPTURE);
}
bool activatePasteTarget(quintptr target) {
    const auto window = reinterpret_cast<HWND>(target);
    if (!window || !IsWindow(window)) return false;
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    return SetForegroundWindow(window) || GetForegroundWindow() == window;
}
bool pasteToTarget(quintptr target, QString *error) {
    const auto window = reinterpret_cast<HWND>(target);
    if (!window || !IsWindow(window) || GetForegroundWindow() != window) { *error = t("目标窗口已切换，内容已复制，请手动粘贴"); return false; }
    for (int key : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN}) if (GetAsyncKeyState(key) & 0x8000) {
        *error = t("请松开快捷键后手动粘贴，内容已复制"); return false;
    }
    INPUT input[4]{};
    for (auto &event : input) event.type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_CONTROL;
    input[1].ki.wVk = 'V';
    input[2].ki.wVk = 'V'; input[2].ki.dwFlags = KEYEVENTF_KEYUP;
    input[3].ki.wVk = VK_CONTROL; input[3].ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(4, input, sizeof(INPUT)) != 4) {
        SendInput(2, input + 2, sizeof(INPUT));
        *error = t("自动粘贴未完成，内容已复制；请切回目标应用手动粘贴"); return false;
    }
    return true;
}

OcrResult recognizeNative(const QImage &image, const QString &language) {
    QTemporaryDir directory;
    const QString path = directory.filePath("image.png");
    if (!directory.isValid() || !image.save(path, "PNG")) return {{}, t("无法写入 OCR 临时图片")};
    const QString script = QDir(QCoreApplication::applicationDirPath()).filePath("native/Invoke-Ocr.ps1");
    if (!QFileInfo::exists(script)) return {{}, t("缺少 native/Invoke-Ocr.ps1，请重新构建或部署应用")};
    QProcess process;
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    process.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script,
                                     "-ImagePath", QDir::toNativeSeparators(path), "-Lang", language});
    if (!process.waitForStarted(5000)) return {{}, t("无法启动 Windows OCR：") + process.errorString()};
    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished();
        return {{}, t("OCR 超时，请缩小图片后重试")};
    }
    const QByteArray output = process.readAllStandardOutput().trimmed();
    if (process.exitStatus() != QProcess::NormalExit || output.isEmpty()) return {{}, t("Windows OCR 失败：") + QString::fromLocal8Bit(process.readAllStandardError()).left(300)};
    return parseOcr(output, image.size());
}

bool requestCapturePermission() { return true; }

QVector<QRect> windowRectangles(QScreen *screen) {
    struct Context { QScreen *screen; QRect physical; QVector<QRect> rectangles; } context{screen, {}, {}};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto *ctx = reinterpret_cast<Context *>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        if (QString::fromWCharArray(info.szDevice) == ctx->screen->name())
            ctx->physical = QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    if (context.physical.isEmpty()) return {};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto *ctx = reinterpret_cast<Context *>(data);
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (!IsWindowVisible(window) || IsIconic(window) || pid == GetCurrentProcessId()) return TRUE;
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        wchar_t cls[256]{};
        GetClassNameW(window, cls, 256);
        if (QString::fromWCharArray(cls) == "Progman" || QString::fromWCharArray(cls) == "WorkerW") return TRUE;
        RECT rect{};
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) GetWindowRect(window, &rect);
        const qreal sx = qreal(ctx->screen->geometry().width()) / ctx->physical.width();
        const qreal sy = qreal(ctx->screen->geometry().height()) / ctx->physical.height();
        QRect logical(qRound((rect.left - ctx->physical.x()) * sx), qRound((rect.top - ctx->physical.y()) * sy),
                      qRound((rect.right - rect.left) * sx), qRound((rect.bottom - rect.top) * sy));
        logical = logical.intersected(QRect(QPoint(), ctx->screen->size()));
        if (logical.width() >= 20 && logical.height() >= 20) ctx->rectangles.append(logical);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.rectangles;
}

Hotkeys::Hotkeys(QObject *parent) : QObject(parent) { QCoreApplication::instance()->installNativeEventFilter(this); }
Hotkeys::~Hotkeys() {
    discardStaged();
    for (int id : registrations_.keys()) unbind(id);
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

quintptr Hotkeys::registerSequence(const QKeySequence &sequence, int nativeId) {
    if (sequence.count() != 1) return 0;
    const auto combination = sequence[0];
    const int key = combination.key();
    UINT vk = 0;
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) vk = UINT(key);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) vk = VK_F1 + UINT(key - Qt::Key_F1);
    else if (key == Qt::Key_Print) vk = VK_SNAPSHOT;
    else if (key == Qt::Key_Space) vk = VK_SPACE;
    if (!vk) return false;
    UINT modifiers = MOD_NOREPEAT;
    const auto mods = combination.keyboardModifiers();
    if (mods & Qt::ControlModifier) modifiers |= MOD_CONTROL;
    if (mods & Qt::AltModifier) modifiers |= MOD_ALT;
    if (mods & Qt::ShiftModifier) modifiers |= MOD_SHIFT;
    if (mods & Qt::MetaModifier) modifiers |= MOD_WIN;
    return RegisterHotKey(nullptr, nativeId, modifiers, vk) ? quintptr(nativeId) : 0;
}

void Hotkeys::releaseRegistration(quintptr handle) { UnregisterHotKey(nullptr, int(handle)); }

bool Hotkeys::nativeEventFilter(const QByteArray &, void *message, qintptr *result) {
    const auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY) return false;
    if (!dispatchNative(int(msg->wParam))) return false;
    *result = 0;
    return true;
}

} // namespace pixclone
