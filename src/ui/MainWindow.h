#pragma once

#include <QMainWindow>
#include <functional>
#include <memory>

#include <optional>
#include <string>

#include "core/Rational.h"
#include "core/Result.h"
#include "timeline/EditOperations.h"

class QAction;
class QTimer;

namespace up {
class EditorSession;
class MediaAssets;
class Project;
}  // namespace up

namespace up::ui {

class ColorPanel;
class InspectorPanel;
class MediaPoolPanel;
class MixerPanel;
class ScopesPanel;
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
    InspectorPanel* inspector() const { return inspector_; }
    MixerPanel* mixer() const { return mixer_; }
    ColorPanel* colorPanel() const { return color_; }
    ScopesPanel* scopes() const { return scopes_; }
    ViewerPanel* viewer() const { return viewer_; }  // program monitor
    ViewerPanel* sourceViewer() const { return sourceViewer_; }
    ViewerPanel* activeViewer() const { return activeViewer_; }
    // Opens a media item in the source monitor.
    bool loadSource(const QString& mediaId);
    // Insert/overwrite the source monitor's clip into the timeline (three-point edit).
    bool threePointEdit(ops::EditMode mode);
    void setActiveViewer(ViewerPanel* viewer);
    // Opens the marker dialog for a timeline or clip marker and applies the result.
    void editMarker(const QString& markerId);
    TimelineView* timeline() const;
    MediaAssets* assets() const { return assets_.get(); }

    // Replaces the current session (no save prompt). Used by open/new and tests.
    void setSession(std::unique_ptr<EditorSession> session);
    bool openProjectFile(const QString& path, bool offerRecovery = true);
    bool exportTo(const QString& path, bool showProgress = true);
    // Sets (empty path = removes) the timeline's output LUT, reporting errors.
    bool setOutputLut(const QString& path);

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
    void clearSource();
    void syncSourceAndMarks();
    void markIn(ViewerPanel* viewer, FrameIndex frame);
    void markOut(ViewerPanel* viewer, FrameIndex frame);
    void clearMarks(ViewerPanel* viewer);
    std::vector<std::string> selectedClips() const;
    void copySelection(bool cut);
    void pasteClipboard(ops::EditMode mode);
    void duplicateSelection();
    void addMarkerAtPlayhead(bool onClip);
    void jumpToMarker(bool next);
    void applyTransition(TransitionKind kind);
    void removeTransitions();
    void pasteGrade();
    void relinkMissingLuts();

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
    InspectorPanel* inspector_ = nullptr;
    MixerPanel* mixer_ = nullptr;
    ColorPanel* color_ = nullptr;
    ScopesPanel* scopes_ = nullptr;
    ViewerPanel* viewer_ = nullptr;
    ViewerPanel* sourceViewer_ = nullptr;
    ViewerPanel* activeViewer_ = nullptr;
    std::unique_ptr<Project> sourceProject_;  // one-clip project shown by the source monitor
    std::string sourceMediaId_;
    TimelinePanel* timelinePanel_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* bypassGradesAction_ = nullptr;
    bool exporting_ = false;
};

}  // namespace up::ui
