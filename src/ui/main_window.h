#pragma once

#include "clipboard/clipboard_browse.h"
#include "storage/history.h"
#include "i18n.h"
#include "settings/settings_draft.h"
#include "platform/startup.h"
#include <QDialog>
#include <QJsonObject>
#include <QTimer>
#include <QVariantList>

class QQuickWidget;

namespace still {
class ClipboardArchive;
class ClipboardController;
class SettingsApply;
class UiImages;

class MainWindow final : public QDialog {
    Q_OBJECT
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(QVariantMap form READ form NOTIFY formChanged)
    Q_PROPERTY(QString saveNamePreview READ saveNamePreview NOTIFY formChanged)
    Q_PROPERTY(QVariantList settingRows READ settingRows NOTIFY pageChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusError READ statusError NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap startup READ startup NOTIFY startupChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY clipsChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY clipboardStateChanged)
    Q_PROPERTY(bool pasteDefault READ pasteDefault NOTIFY clipboardStateChanged)
    Q_PROPERTY(bool previewVisible READ previewVisible WRITE setPreviewVisible NOTIFY previewChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY previewChanged)
    Q_PROPERTY(QString previewImage READ previewImage NOTIFY previewChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QVariantList historyItems READ historyItems NOTIFY historyChanged)
    Q_PROPERTY(QString historySummary READ historySummary NOTIFY historyChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyChanged)
    Q_PROPERTY(bool historyMore READ historyMore NOTIFY historyChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY appearanceChanged)
    Q_PROPERTY(bool glassAvailable READ glassAvailable NOTIFY appearanceChanged)
    Q_PROPERTY(bool maximized READ isMaximized NOTIFY windowStateChanged)
public:
    MainWindow(SettingsApply &settings, HistoryStore &history, ClipboardArchive &archive, ClipboardController &clipboard);
    ~MainWindow() override;
    void showAndActivate();
    QQuickWidget *view() const { return view_; }
    int page() const { return page_; }
    void setPage(int page);
    QVariantMap form() const { return draft_.form().toVariantMap(); }
    QString saveNamePreview() const;
    QVariantList settingRows() const;
    QString status() const { return draft_.status(); }
    bool statusError() const { return draft_.statusError(); }
    bool busy() const;
    QVariantMap startup() const;
    QVariantList clips() const { return browse_.clips(); }
    int totalCount() const;
    QString search() const { return browse_.search(); }
    void setSearch(const QString &search) { browse_.setSearch(search); }
    QString filter() const { return browse_.filter(); }
    void setFilter(const QString &filter) { browse_.setFilter(filter); }
    int selectedIndex() const { return browse_.selectedIndex(); }
    void setSelectedIndex(int index);
    QVariantMap selected() const { return browse_.selected(); }
    bool paused() const;
    bool pasteDefault() const;
    bool previewVisible() const { return browse_.previewVisible(); }
    void setPreviewVisible(bool visible) { browse_.setPreviewVisible(visible); }
    QString previewText() const { return browse_.previewText(); }
    QString previewImage() const { return browse_.previewUrl(); }
    QString message() const { return message_; }
    QVariantList historyItems() const { return historyItems_; }
    QString historySummary() const { return historySummary_; }
    bool historyLoading() const { return historyLoading_; }
    bool historyMore() const { return historyItems_.size() < historyEntries_.size(); }
    bool dark() const;
    bool glassAvailable() const { return glassAvailable_; }
    Q_INVOKABLE void setClipboardPreviewFits(bool fits) { browse_.setPreviewFits(fits); }
    Q_INVOKABLE void focusSearch();
    Q_INVOKABLE void setDraft(const QString &group, const QString &key, const QVariant &value) { draft_.setDraft(group, key, value); }
    Q_INVOKABLE void setSetting(const QString &group, const QString &key, const QVariant &value) { draft_.setSetting(group, key, value); }
    Q_INVOKABLE bool restoreDefault(const QString &group, const QString &key);
    Q_INVOKABLE void commitDrafts() { draft_.commitDrafts(); }
    Q_INVOKABLE void setShortcut(const QString &name, int key, int modifiers);
    Q_INVOKABLE QString shortcutText(const QString &name) const;
    Q_INVOKABLE void browseDirectory(const QString &group);
    Q_INVOKABLE void openDirectory(const QString &group);
    Q_INVOKABLE void openSettingsBackup();
    Q_INVOKABLE void openApiSettings();
    Q_INVOKABLE void setStartupEnabled(bool enabled);
    Q_INVOKABLE void openStartupSettings();
    Q_INVOKABLE void activate(const QString &action = QString());
    Q_INVOKABLE void togglePin();
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void clearClipboard();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void refreshHistory();
    Q_INVOKABLE void loadMoreHistory();
    Q_INVOKABLE void historyAction(const QString &path, const QString &action);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE bool moveWindow();
    Q_INVOKABLE bool resizeWindow(int edges);
    Q_INVOKABLE void toggleMaximized();
    void reject() override;
signals:
    void pageChanged();
    void windowStateChanged();
    void appearanceChanged();
    void formChanged();
    void statusChanged();
    void startupChanged();
    void clipsChanged();
    void searchChanged();
    void filterChanged();
    void selectionChanged();
    void clipboardStateChanged();
    void previewChanged();
    void messageChanged();
    void historyChanged();
    void focusSearchRequested();
    void captureRequested();
    void historyImageRequested(const QImage &image, bool pin);
protected:
    void closeEvent(QCloseEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    SettingsApply &settings_;
    HistoryStore &history_;
    ClipboardArchive &archive_;
    ClipboardController &clipboard_;
    QQuickWidget *view_ = nullptr;
    UiImages *images_ = nullptr;
    int page_ = 0;
    SettingsDraft draft_;
    StartupStatus startup_;
    QString startupError_;
    bool glassAvailable_ = false;
    ClipboardBrowse browse_;
    QString message_;
    QVariantList historyItems_;
    QList<HistoryEntry> historyEntries_;
    QString historySummary_;
    bool historyLoading_ = false;
    QFuture<QList<QImage>> historyThumbnails_;
    quint64 historyGeneration_ = 0;
    quint64 historyOpenGeneration_ = 0;
    bool historyOpening_ = false;
    void showMessage(const QString &message);
    void updateBackdrop();
    void refreshStartup();
};
} // namespace still
