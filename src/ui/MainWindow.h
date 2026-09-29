#pragma once

#include <QMainWindow>
#include <functional>
#include <memory>

#include "core/Result.h"

class QAction;
class QTimer;

namespace up {
class EditorSession;
class MediaAssets;
}  // namespace up

namespace up::ui {

class MediaPoolPanel;
class TimelinePanel;
class TimelineView;
class ViewerPanel;

// Edit workspace: media pool, viewer and timeline in dockable panels.
// Owns the EditorSession; all model changes go through it.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // `checkRecovery` offers to restore unsaved projects from a previous crash on startup.
    explicit MainWindow(QWidget* parent = nullptr, bool checkRecovery = true);
    ~MainWindow() override;

    EditorSession* session() const { return session_.get(); }
    MediaPoolPanel* mediaPool() const { return mediaPool_; }
    ViewerPanel* viewer() const { return viewer_; }
    TimelineView* timeline() const;
    MediaAssets* assets() const { return assets_.get(); }

    // Replaces the current session (no save prompt). Used by open/new and tests.
    void setSession(std::unique_ptr<EditorSession> session);
    bool openProjectFile(const QString& path, bool offerRecovery = true);
    bool exportTo(const QString& path, bool showProgress = true);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildActions();
    void buildMenus();
    void updateTitleAndActions();
    bool maybeSave();
    void showError(const QString& summary, const QString& details = {});
    void offerStartupRecovery();

    void newProject();
    void openProject();
    bool save();
    bool saveAs();
    void importMedia();
    void relinkMedia(const QString& mediaId);
    void exportTimeline();
    void autosave();
    void clearMediaCache();

    // Edit commands operating on the selected clip / playhead.
    void razor();
    void deleteSelected(bool ripple);
    void trimSelected(bool outEdge, int delta);
    void slipSelected(int delta);
    void slideSelected(int delta);
    void runEdit(const std::function<Status()>& edit);
    QString selectedClipOrWarn();

    std::unique_ptr<EditorSession> session_;
    std::unique_ptr<MediaAssets> assets_;
    QTimer* assetRefresh_ = nullptr;
    MediaPoolPanel* mediaPool_ = nullptr;
    ViewerPanel* viewer_ = nullptr;
    TimelinePanel* timelinePanel_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    bool exporting_ = false;
};

}  // namespace up::ui
