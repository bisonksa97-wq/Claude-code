#include "ui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <atomic>
#include <thread>

#include "app/EditorSession.h"
#include "app/MediaAssets.h"
#include "app/SourceProject.h"
#include "core/Log.h"
#include "render/ExportJob.h"
#include "ui/InspectorPanel.h"
#include "ui/MarkerDialog.h"
#include "ui/MediaPoolPanel.h"
#include "ui/Theme.h"
#include "ui/TimelineView.h"
#include "ui/ViewerPanel.h"

namespace up::ui {
namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

constexpr const char* kMediaFilter =
    "Media (*.mp4 *.mov *.mkv *.mxf *.avi *.webm *.m4v *.mts *.wav *.mp3 *.m4a *.aac *.flac *.ogg *.png *.jpg *.jpeg *.tif *.tiff);;All files (*)";

}  // namespace

MainWindow::MainWindow(QWidget* parent, bool checkRecovery) : QMainWindow(parent) {
    setObjectName("MainWindow");
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);

    // Source monitor (left) and program monitor (right).
    auto* monitors = new QSplitter(Qt::Horizontal, this);
    sourceViewer_ = new ViewerPanel(monitors);
    sourceViewer_->setObjectName("SourceViewer");
    sourceViewer_->setTitle(tr("Source — double-click media to load"));
    viewer_ = new ViewerPanel(monitors);
    viewer_->setObjectName("ProgramViewer");
    monitors->addWidget(sourceViewer_);
    monitors->addWidget(viewer_);
    setCentralWidget(monitors);

    mediaPool_ = new MediaPoolPanel(this);
    auto* poolDock = new QDockWidget(tr("Media Pool"), this);
    poolDock->setObjectName("MediaPoolDock");
    poolDock->setWidget(mediaPool_);
    addDockWidget(Qt::LeftDockWidgetArea, poolDock);

    inspector_ = new InspectorPanel(this);
    auto* inspectorDock = new QDockWidget(tr("Inspector"), this);
    inspectorDock->setObjectName("InspectorDock");
    inspectorDock->setWidget(inspector_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    timelinePanel_ = new TimelinePanel(this);
    auto* timelineDock = new QDockWidget(tr("Timeline"), this);
    timelineDock->setObjectName("TimelineDock");
    timelineDock->setWidget(timelinePanel_);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock);

    buildActions();
    buildMenus();

    TimelineView* tv = timeline();
    connect(tv, &TimelineView::playheadMoved, viewer_, &ViewerPanel::setPosition);
    connect(viewer_, &ViewerPanel::positionChanged, tv, &TimelineView::setPlayhead);
    connect(tv, &TimelineView::errorOccurred, this, [this](const QString& m) { showError(tr("The edit could not be applied."), m); });
    connect(tv, &TimelineView::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 2000); });
    for (ViewerPanel* v : {viewer_, sourceViewer_}) {
        connect(v, &ViewerPanel::errorOccurred, this, [this](const QString& m) { statusBar()->showMessage(m.section('\n', 0, 0), 5000); });
        connect(v, &ViewerPanel::activated, this, [this, v] { setActiveViewer(v); });
        connect(v, &ViewerPanel::markInRequested, this, [this, v](FrameIndex f) { markIn(v, f); });
        connect(v, &ViewerPanel::markOutRequested, this, [this, v](FrameIndex f) { markOut(v, f); });
        connect(v, &ViewerPanel::clearMarksRequested, this, [this, v] { clearMarks(v); });
    }
    connect(tv, &TimelineView::playheadMoved, this, [this] { setActiveViewer(viewer_); });
    connect(tv, &TimelineView::selectionChanged, this, [this](const QString& id) {
        setActiveViewer(viewer_);
        inspector_->setClip(id.toStdString());
    });
    connect(viewer_, &ViewerPanel::positionChanged, inspector_, &InspectorPanel::setPlayhead);
    connect(inspector_, &InspectorPanel::seekRequested, viewer_, &ViewerPanel::setPosition);
    connect(inspector_, &InspectorPanel::errorOccurred, this, [this](const QString& m) { statusBar()->showMessage(m, 4000); });
    connect(tv, &TimelineView::markerEditRequested, this, &MainWindow::editMarker);
    connect(tv, &TimelineView::markerDeleteRequested, this, [this](const QString& id) {
        runEdit([&] { return session_->removeMarker(id.toStdString()); });
    });
    connect(mediaPool_, &MediaPoolPanel::mediaActivated, this, [this](const QString& id) { loadSource(id); });
    setActiveViewer(viewer_);
    connect(mediaPool_, &MediaPoolPanel::importRequested, this, &MainWindow::importMedia);
    connect(mediaPool_, &MediaPoolPanel::relinkRequested, this, &MainWindow::relinkMedia);

    autosaveTimer_ = new QTimer(this);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::autosave);
    const int interval = QSettings().value("autosave/intervalSeconds", 120).toInt();
    if (interval > 0) autosaveTimer_->start(interval * 1000);

    // Thumbnails and waveforms are generated in the background into a regeneratable cache.
    const QString cacheDir = QSettings()
                                 .value("cache/directory", QString::fromStdString(DiskCache::defaultDirectory().string()))
                                 .toString();
    assets_ = std::make_unique<MediaAssets>(cacheDir.toStdString());
    assetRefresh_ = new QTimer(this);
    assetRefresh_->setSingleShot(true);
    assetRefresh_->setInterval(50);  // coalesce bursts of finished jobs into one repaint
    connect(assetRefresh_, &QTimer::timeout, this, [this] {
        mediaPool_->updateThumbnails();
        timeline()->update();
    });
    assets_->setListener([this](const std::string&, MediaAssets::Kind) {
        QMetaObject::invokeMethod(assetRefresh_, qOverload<>(&QTimer::start), Qt::QueuedConnection);
    });
    mediaPool_->setAssets(assets_.get());
    timeline()->setAssets(assets_.get());

    resize(1400, 900);
    setSession(EditorSession::createNew("Untitled"));
    if (checkRecovery) QTimer::singleShot(0, this, &MainWindow::offerStartupRecovery);
}

MainWindow::~MainWindow() {
    viewer_->stop();
    sourceViewer_->stop();
    // Stop background asset jobs before any widget they notify is destroyed.
    assets_->setListener({});
    assets_.reset();
}

void MainWindow::clearMediaCache() {
    Status s = assets_->clearCache();
    if (!s.ok()) {
        showError(tr("The media cache could not be cleared."), qs(s.error().toString()));
        return;
    }
    mediaPool_->refresh();  // regenerates thumbnails on demand
    timeline()->update();
    statusBar()->showMessage(tr("Media cache cleared; thumbnails and waveforms will be regenerated."), 5000);
}

TimelineView* MainWindow::timeline() const { return timelinePanel_->view(); }

void MainWindow::setSession(std::unique_ptr<EditorSession> session) {
    viewer_->stop();
    session_ = std::move(session);
    session_->setChangeListener([this] {
        assets_->prefetch(session_->project());
        mediaPool_->refresh();
        timeline()->update();
        timeline()->tracksChanged();
        timeline()->viewChanged();
        viewer_->refresh();
        syncSourceAndMarks();
        inspector_->refresh();
        updateTitleAndActions();
    });
    assets_->prefetch(session_->project());
    mediaPool_->setSession(session_.get());
    timeline()->setSession(session_.get());
    inspector_->setSession(session_.get());
    viewer_->setSource(&session_->project(), session_->timeline().id);
    viewer_->setTitle(tr("Program — %1").arg(qs(session_->timeline().name)));
    clearSource();
    syncSourceAndMarks();
    QTimer::singleShot(0, timeline(), &TimelineView::zoomToFit);
    updateTitleAndActions();
}

void MainWindow::buildActions() {
    undoAction_ = new QAction(tr("&Undo"), this);
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] { session_->undo(); });
    redoAction_ = new QAction(tr("&Redo"), this);
    redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence("Ctrl+Shift+Z")});
    connect(redoAction_, &QAction::triggered, this, [this] { session_->redo(); });
    saveAction_ = new QAction(tr("&Save"), this);
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, &MainWindow::save);
}

void MainWindow::buildMenus() {
    auto add = [this](QMenu* menu, const QString& text, const QKeySequence& key, auto slot) {
        QAction* a = menu->addAction(text);
        if (!key.isEmpty()) a->setShortcut(key);
        a->setShortcutContext(Qt::ApplicationShortcut);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    QMenu* file = menuBar()->addMenu(tr("&File"));
    add(file, tr("&New Project"), QKeySequence::New, &MainWindow::newProject);
    add(file, tr("&Open Project…"), QKeySequence::Open, &MainWindow::openProject);
    file->addAction(saveAction_);
    add(file, tr("Save &As…"), QKeySequence::SaveAs, &MainWindow::saveAs);
    file->addSeparator();
    add(file, tr("&Import Media…"), QKeySequence("Ctrl+I"), &MainWindow::importMedia);
    add(file, tr("Clear Media Cache"), QKeySequence(), &MainWindow::clearMediaCache);
    add(file, tr("&Export Timeline…"), QKeySequence("Ctrl+M"), &MainWindow::exportTimeline);
    file->addSeparator();
    add(file, tr("&Quit"), QKeySequence::Quit, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(undoAction_);
    edit->addAction(redoAction_);
    edit->addSeparator();
    add(edit, tr("Cut"), QKeySequence::Cut, [this] { copySelection(true); });
    add(edit, tr("Copy"), QKeySequence::Copy, [this] { copySelection(false); });
    add(edit, tr("Paste"), QKeySequence::Paste, [this] { pasteClipboard(ops::EditMode::Overwrite); });
    add(edit, tr("Paste Insert"), QKeySequence("Ctrl+Shift+V"), [this] { pasteClipboard(ops::EditMode::Insert); });
    add(edit, tr("Duplicate"), QKeySequence("Ctrl+D"), &MainWindow::duplicateSelection);
    edit->addSeparator();
    add(edit, tr("Select All"), QKeySequence::SelectAll, [this] {
        std::vector<std::string> all;
        for (const auto& t : session_->timeline().tracks)
            if (!t.locked)
                for (const auto& c : t.clips) all.push_back(c.id);
        timeline()->setSelection(std::move(all));
    });
    add(edit, tr("Select Forward from Playhead"), QKeySequence("Ctrl+Alt+A"),
        [this] { timeline()->setSelection(session_->clipsFrom(viewer_->position())); });
    add(edit, tr("Deselect All"), QKeySequence("Ctrl+Shift+A"), [this] { timeline()->setSelection({}); });
    edit->addSeparator();
    add(edit, tr("Razor at Playhead"), QKeySequence("Ctrl+K"), &MainWindow::razor);
    add(edit, tr("Lift (Delete Leaving Gap)"), QKeySequence(Qt::Key_Delete), [this] { deleteSelected(false); });
    add(edit, tr("Ripple Delete"), QKeySequence("Shift+Del"), [this] { deleteSelected(true); });
    edit->addSeparator();
    add(edit, tr("Trim Start −1 Frame"), QKeySequence("Alt+["), [this] { trimSelected(false, -1); });
    add(edit, tr("Trim Start +1 Frame"), QKeySequence("Alt+]"), [this] { trimSelected(false, 1); });
    add(edit, tr("Trim End −1 Frame"), QKeySequence("["), [this] { trimSelected(true, -1); });
    add(edit, tr("Trim End +1 Frame"), QKeySequence("]"), [this] { trimSelected(true, 1); });
    add(edit, tr("Slip −1 Frame"), QKeySequence("Alt+,"), [this] { slipSelected(-1); });
    add(edit, tr("Slip +1 Frame"), QKeySequence("Alt+."), [this] { slipSelected(1); });
    add(edit, tr("Slide −1 Frame"), QKeySequence("Ctrl+Alt+,"), [this] { slideSelected(-1); });
    add(edit, tr("Slide +1 Frame"), QKeySequence("Ctrl+Alt+."), [this] { slideSelected(1); });

    QMenu* playback = menuBar()->addMenu(tr("&Playback"));
    // Transport and marks address the active monitor (highlighted title; click a monitor to activate it).
    add(playback, tr("Play / Pause"), QKeySequence(Qt::Key_Space), [this] { activeViewer_->togglePlay(); });
    add(playback, tr("Step Back"), QKeySequence(Qt::Key_Left), [this] { activeViewer_->step(-1); });
    add(playback, tr("Step Forward"), QKeySequence(Qt::Key_Right), [this] { activeViewer_->step(1); });
    add(playback, tr("Go to Start"), QKeySequence(Qt::Key_Home), [this] { activeViewer_->goToStart(); });
    add(playback, tr("Go to End"), QKeySequence(Qt::Key_End), [this] { activeViewer_->goToEnd(); });
    playback->addSeparator();
    add(playback, tr("Toggle Source / Program Monitor"), QKeySequence("Shift+2"),
        [this] { setActiveViewer(activeViewer_ == viewer_ ? sourceViewer_ : viewer_); });
    add(playback, tr("Mark In"), QKeySequence(Qt::Key_I), [this] { markIn(activeViewer_, activeViewer_->position()); });
    add(playback, tr("Mark Out"), QKeySequence(Qt::Key_O), [this] { markOut(activeViewer_, activeViewer_->position() + 1); });
    add(playback, tr("Clear In and Out"), QKeySequence("Alt+X"), [this] { clearMarks(activeViewer_); });

    edit->addSeparator();
    add(edit, tr("Insert Edit"), QKeySequence(Qt::Key_Comma), [this] { threePointEdit(ops::EditMode::Insert); });
    add(edit, tr("Overwrite Edit"), QKeySequence(Qt::Key_Period), [this] { threePointEdit(ops::EditMode::Overwrite); });

    QMenu* markers = menuBar()->addMenu(tr("Mar&ker"));
    add(markers, tr("Add Marker"), QKeySequence(Qt::Key_M), [this] { addMarkerAtPlayhead(false); });
    add(markers, tr("Add Clip Marker"), QKeySequence("Alt+M"), [this] { addMarkerAtPlayhead(true); });
    add(markers, tr("Go to Next Marker"), QKeySequence("Shift+M"), [this] { jumpToMarker(true); });
    add(markers, tr("Go to Previous Marker"), QKeySequence("Ctrl+Shift+M"), [this] { jumpToMarker(false); });

    QMenu* view = menuBar()->addMenu(tr("&View"));
    add(view, tr("Zoom In"), QKeySequence("="), [this] { timeline()->zoomIn(); });
    add(view, tr("Zoom Out"), QKeySequence("-"), [this] { timeline()->zoomOut(); });
    add(view, tr("Zoom to Fit"), QKeySequence("Shift+Z"), [this] { timeline()->zoomToFit(); });
    view->addSeparator();
    auto* themes = new QActionGroup(this);
    const std::pair<QString, ThemeKind> themeList[] = {
        {tr("Dark Theme"), ThemeKind::Dark}, {tr("Light Theme"), ThemeKind::Light}, {tr("High Contrast Theme"), ThemeKind::HighContrast}};
    for (const auto& [label, kind] : themeList) {
        QAction* a = view->addAction(label);
        a->setCheckable(true);
        a->setChecked(kind == currentThemeKind());
        themes->addAction(a);
        connect(a, &QAction::triggered, this, [kind = kind] {
            applyTheme(*qApp, kind);
            QSettings().setValue("ui/theme", static_cast<int>(kind));
        });
    }
}

void MainWindow::updateTitleAndActions() {
    if (!session_) return;
    const auto& p = session_->project();
    const QString file = p.filePath.empty() ? tr("unsaved") : qs(p.filePath.filename().string());
    setWindowTitle(QString("%1[*] (%2) — Ultimate Post").arg(qs(p.name), file));
    setWindowModified(session_->isDirty());
    auto& h = session_->history();
    undoAction_->setEnabled(h.canUndo());
    undoAction_->setText(h.canUndo() ? tr("&Undo %1").arg(qs(h.undoName())) : tr("&Undo"));
    redoAction_->setEnabled(h.canRedo());
    redoAction_->setText(h.canRedo() ? tr("&Redo %1").arg(qs(h.redoName())) : tr("&Redo"));
}

void MainWindow::showError(const QString& summary, const QString& details) {
    UP_LOG_WARN(log::sub::Ui, summary.toStdString() << " " << details.toStdString());
    QMessageBox box(QMessageBox::Warning, tr("Ultimate Post"), summary, QMessageBox::Ok, this);
    box.setInformativeText(details.section('\n', 0, 1));
    box.setDetailedText(details);  // full diagnostics, copyable
    box.exec();
}

bool MainWindow::maybeSave() {
    if (!session_ || !session_->isDirty()) return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved Changes"),
                                              tr("Save changes to '%1' before closing?").arg(qs(session_->project().name)),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save();
    session_->discardAutosave();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (exporting_) {
        QMessageBox::information(this, tr("Export Running"), tr("Wait for the export to finish or cancel it first."));
        event->ignore();
        return;
    }
    if (maybeSave()) event->accept();
    else event->ignore();
}

void MainWindow::newProject() {
    if (!maybeSave()) return;
    setSession(EditorSession::createNew("Untitled"));
}

void MainWindow::openProject() {
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), tr("Ultimate Post projects (*.uproj)"));
    if (!path.isEmpty()) openProjectFile(path);
}

bool MainWindow::openProjectFile(const QString& path, bool offerRecovery) {
    const std::filesystem::path file = path.toStdString();
    if (offerRecovery) {
        if (auto autosave = EditorSession::newerAutosaveFor(file)) {
            const auto answer = QMessageBox::question(
                this, tr("Recover Project"),
                tr("An autosave newer than '%1' exists, probably from a session that did not close normally. "
                   "Recover it?").arg(QFileInfo(path).fileName()));
            if (answer == QMessageBox::Yes) {
                auto recovered = EditorSession::openRecovery(*autosave, file);
                if (recovered.ok()) {
                    setSession(std::move(recovered.value()));
                    statusBar()->showMessage(tr("Recovered autosave — save to keep it."), 8000);
                    return true;
                }
                showError(tr("The autosave could not be opened; opening the saved project instead."),
                          qs(recovered.error().toString()));
            }
        }
    }
    auto session = EditorSession::open(file);
    if (!session.ok()) {
        showError(tr("The project could not be opened."), qs(session.error().toString()));
        return false;
    }
    const auto offline = std::count_if(session.value()->project().media.begin(), session.value()->project().media.end(),
                                       [](const MediaItem& m) { return !m.online; });
    setSession(std::move(session.value()));
    if (offline > 0)
        statusBar()->showMessage(tr("%1 media file(s) are offline. Right-click them in the Media Pool to relink.").arg(offline), 10000);
    return true;
}

void MainWindow::offerStartupRecovery() {
    std::error_code ec;
    const auto dir = EditorSession::recoveryDirectory();
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".uproj") continue;
        const auto t = entry.last_write_time(ec);
        if (newest.empty() || t > newestTime) {
            newest = entry.path();
            newestTime = t;
        }
    }
    if (newest.empty()) return;
    const auto answer = QMessageBox::question(this, tr("Recover Unsaved Project"),
                                              tr("An unsaved project from a previous session was found. Recover it?"),
                                              QMessageBox::Yes | QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        auto recovered = EditorSession::openRecovery(newest, {});
        if (recovered.ok()) {
            setSession(std::move(recovered.value()));
        } else {
            showError(tr("The recovery file could not be opened."), qs(recovered.error().toString()));
        }
    }
    std::filesystem::remove(newest, ec);
}

bool MainWindow::save() {
    if (session_->project().filePath.empty()) return saveAs();
    Status s = session_->save();
    if (!s.ok()) {
        showError(tr("The project could not be saved."), qs(s.error().toString()));
        return false;
    }
    statusBar()->showMessage(tr("Saved %1").arg(qs(session_->project().filePath.string())), 4000);
    updateTitleAndActions();
    return true;
}

bool MainWindow::saveAs() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"), qs(session_->project().name) + ".uproj",
                                                tr("Ultimate Post projects (*.uproj)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(".uproj")) path += ".uproj";
    const std::filesystem::path oldRecovery = session_->autosavePath();
    Status s = session_->saveAs(path.toStdString());
    if (!s.ok()) {
        showError(tr("The project could not be saved."), qs(s.error().toString()));
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(oldRecovery, ec);
    updateTitleAndActions();
    return true;
}

void MainWindow::autosave() {
    if (!session_ || !session_->isDirty() || exporting_) return;
    Status s = session_->writeAutosave();
    if (!s.ok()) statusBar()->showMessage(tr("Autosave failed: %1").arg(qs(s.error().message)), 5000);
}

void MainWindow::importMedia() {
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Media"), QString(), tr(kMediaFilter));
    if (files.isEmpty()) return;
    std::vector<std::filesystem::path> paths;
    for (const auto& f : files) paths.emplace_back(f.toStdString());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const ImportReport report = session_->importMedia(paths);
    QApplication::restoreOverrideCursor();
    if (!report.failures.empty()) {
        QStringList details;
        for (const auto& e : report.failures) details << qs(e.toString());
        showError(tr("%1 of %2 file(s) could not be imported.").arg(report.failures.size()).arg(files.size()),
                  details.join("\n\n"));
    }
    statusBar()->showMessage(tr("Imported %1 file(s)").arg(report.importedIds.size()), 4000);
}

void MainWindow::relinkMedia(const QString& mediaId) {
    const QString file = QFileDialog::getOpenFileName(this, tr("Relink Media"), QString(), tr(kMediaFilter));
    if (file.isEmpty()) return;
    Status s = session_->relinkMedia(mediaId.toStdString(), file.toStdString());
    if (!s.ok()) showError(tr("The media could not be relinked."), qs(s.error().toString()));
}

void MainWindow::exportTimeline() {
    if (session_->timeline().duration() == 0) {
        showError(tr("There is nothing to export."), tr("Add clips to the timeline first."));
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Export Timeline"), qs(session_->project().name) + ".mp4",
                                                tr("MPEG-4 (*.mp4);;QuickTime (*.mov);;Matroska (*.mkv)"));
    if (path.isEmpty()) return;
    exportTo(path);
}

bool MainWindow::exportTo(const QString& path, bool showProgress) {
    render::ExportOptions options;
    options.output = path.toStdString();
    // The job works on a snapshot, so editing can continue safely while it runs.
    auto job = std::make_shared<render::ExportJob>(session_->project(), session_->timeline().id, options);
    auto done = std::make_shared<std::atomic<bool>>(false);
    auto progress = std::make_shared<std::atomic<long long>>(0);
    auto result = std::make_shared<Status>();
    const FrameIndex total = session_->timeline().duration();

    exporting_ = true;
    std::thread worker([job, done, progress, result] {
        *result = job->run([&](const render::ExportProgress& p) { *progress = p.framesDone; });
        *done = true;
    });

    QProgressDialog dialog(tr("Exporting %1…").arg(QFileInfo(path).fileName()), tr("Cancel"), 0, static_cast<int>(total), this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(showProgress ? 300 : 1000000);
    while (!*done) {
        dialog.setValue(static_cast<int>(progress->load()));
        if (dialog.wasCanceled()) job->cancel();
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    dialog.reset();
    exporting_ = false;
    if (!result->ok()) {
        if (result->error().code != ErrorCode::Cancelled) showError(tr("The export failed."), qs(result->error().toString()));
        else statusBar()->showMessage(tr("Export cancelled"), 4000);
        return false;
    }
    statusBar()->showMessage(tr("Exported %1").arg(path), 6000);
    return true;
}

void MainWindow::runEdit(const std::function<Status()>& edit) {
    Status s = edit();
    if (!s.ok()) showError(tr("The edit could not be applied."), qs(s.error().toString()));
}

QString MainWindow::selectedClipOrWarn() {
    const QString id = timeline()->selectedClipId();
    if (id.isEmpty() || !session_->timeline().clip(id.toStdString())) {
        statusBar()->showMessage(tr("Select a clip in the timeline first."), 3000);
        return {};
    }
    return id;
}

void MainWindow::razor() {
    auto r = session_->razorAt(viewer_->position());
    if (!r.ok()) statusBar()->showMessage(qs(r.error().message), 4000);
    else statusBar()->showMessage(tr("Cut %1 clip(s)").arg(r.value()), 3000);
}

void MainWindow::deleteSelected(bool ripple) {
    const auto clips = selectedClips();
    if (clips.empty()) {
        statusBar()->showMessage(tr("Select a clip in the timeline first."), 3000);
        return;
    }
    runEdit([&] { return ripple ? session_->rippleDeleteClips(clips) : session_->liftClips(clips); });
    timeline()->selectClip({});
}

void MainWindow::trimSelected(bool outEdge, int delta) {
    const QString id = selectedClipOrWarn();
    if (id.isEmpty()) return;
    runEdit([&] {
        return session_->trimClip(id.toStdString(), outEdge ? ops::Edge::Out : ops::Edge::In, delta, ops::TrimMode::Normal);
    });
}

void MainWindow::slipSelected(int delta) {
    const QString id = selectedClipOrWarn();
    if (!id.isEmpty()) runEdit([&] { return session_->slipClip(id.toStdString(), delta); });
}

void MainWindow::slideSelected(int delta) {
    const QString id = selectedClipOrWarn();
    if (!id.isEmpty()) runEdit([&] { return session_->slideClip(id.toStdString(), delta); });
}

void MainWindow::setActiveViewer(ViewerPanel* viewer) {
    activeViewer_ = viewer;
    viewer_->setActive(viewer == viewer_);
    sourceViewer_->setActive(viewer == sourceViewer_);
}

void MainWindow::clearSource() {
    sourceViewer_->setSource(nullptr, {});
    sourceProject_.reset();
    sourceMediaId_.clear();
    sourceViewer_->setTitle(tr("Source — double-click media to load"));
    sourceViewer_->setMarks(std::nullopt, std::nullopt);
    if (activeViewer_ == sourceViewer_) setActiveViewer(viewer_);
}

bool MainWindow::loadSource(const QString& mediaId) {
    auto project = makeSourceProject(session_->project(), mediaId.toStdString());
    if (!project.ok()) {
        showError(tr("The media could not be opened in the source monitor."), qs(project.error().toString()));
        return false;
    }
    sourceViewer_->stop();
    sourceProject_ = std::make_unique<Project>(std::move(project.value()));
    sourceMediaId_ = mediaId.toStdString();
    sourceViewer_->setSource(sourceProject_.get(), sourceProject_->activeTimelineId);
    sourceViewer_->setTitle(tr("Source — %1").arg(qs(sourceProject_->name)));
    // Start at the source in mark, as editors expect when reloading a marked clip.
    const auto [in, out] = session_->mediaMarks(sourceMediaId_);
    sourceViewer_->setMarks(in, out);
    if (in) sourceViewer_->setPosition(*in);
    setActiveViewer(sourceViewer_);
    return true;
}

void MainWindow::syncSourceAndMarks() {
    const Timeline& tl = session_->timeline();
    viewer_->setMarks(tl.markIn, tl.markOut);
    if (sourceMediaId_.empty()) return;
    const MediaItem* media = session_->project().findMedia(sourceMediaId_);
    if (!media) {  // e.g. the import that added it was undone
        clearSource();
        return;
    }
    const MediaItem& shown = sourceProject_->media.front();
    if (shown.path != media->path || shown.online != media->online) {
        const FrameIndex position = sourceViewer_->position();
        loadSource(QString::fromStdString(sourceMediaId_));  // relinked: rebuild the source view
        sourceViewer_->setPosition(position);
    }
    const auto [in, out] = session_->mediaMarks(sourceMediaId_);
    sourceViewer_->setMarks(in, out);
}

void MainWindow::markIn(ViewerPanel* viewer, FrameIndex frame) {
    Status s = Status::success();
    if (viewer == sourceViewer_) {
        if (sourceMediaId_.empty()) return;
        auto [in, out] = session_->mediaMarks(sourceMediaId_);
        if (out && *out <= frame) out.reset();  // a new in after the out starts a new range
        s = session_->setMediaMarks(sourceMediaId_, frame, out);
    } else {
        auto out = session_->timeline().markOut;
        if (out && *out <= frame) out.reset();
        s = session_->setTimelineMarks(frame, out);
    }
    if (!s.ok()) statusBar()->showMessage(qs(s.error().message), 4000);
}

void MainWindow::markOut(ViewerPanel* viewer, FrameIndex frame) {
    Status s = Status::success();
    if (viewer == sourceViewer_) {
        if (sourceMediaId_.empty()) return;
        auto [in, out] = session_->mediaMarks(sourceMediaId_);
        if (in && *in >= frame) in.reset();
        s = session_->setMediaMarks(sourceMediaId_, in, frame);
    } else {
        auto in = session_->timeline().markIn;
        if (in && *in >= frame) in.reset();
        s = session_->setTimelineMarks(in, frame);
    }
    if (!s.ok()) statusBar()->showMessage(qs(s.error().message), 4000);
}

void MainWindow::clearMarks(ViewerPanel* viewer) {
    Status s = Status::success();
    if (viewer == sourceViewer_) {
        if (!sourceMediaId_.empty()) s = session_->setMediaMarks(sourceMediaId_, std::nullopt, std::nullopt);
    } else {
        s = session_->setTimelineMarks(std::nullopt, std::nullopt);
    }
    if (!s.ok()) statusBar()->showMessage(qs(s.error().message), 4000);
}

bool MainWindow::threePointEdit(ops::EditMode mode) {
    if (sourceMediaId_.empty()) {
        statusBar()->showMessage(tr("Load a clip into the source monitor first (double-click it in the Media Pool)."), 5000);
        return false;
    }
    auto r = session_->threePointEdit(sourceMediaId_, mode, viewer_->position());
    if (!r.ok()) {
        showError(tr("The edit could not be applied."), qs(r.error().toString()));
        return false;
    }
    // Like other NLEs, park the program playhead after the new material.
    viewer_->setPosition(r.value().recordOut);
    if (!r.value().clipIds.empty()) timeline()->selectClip(qs(r.value().clipIds.front()));
    statusBar()->showMessage(mode == ops::EditMode::Insert ? tr("Inserted") : tr("Overwrote"), 3000);
    return true;
}

std::vector<std::string> MainWindow::selectedClips() const {
    std::vector<std::string> out;
    for (const auto& id : timeline()->selectedClipIds())
        if (session_->timeline().clip(id)) out.push_back(id);
    return out;  // linked partners are added by the session
}

void MainWindow::copySelection(bool cut) {
    const auto clips = selectedClips();
    if (clips.empty()) {
        statusBar()->showMessage(tr("Select a clip in the timeline first."), 3000);
        return;
    }
    Status s = cut ? session_->cutClips(clips) : session_->copyClips(clips);
    if (!s.ok()) {
        showError(cut ? tr("The clips could not be cut.") : tr("The clips could not be copied."), qs(s.error().toString()));
        return;
    }
    if (cut) timeline()->selectClip({});
    statusBar()->showMessage(tr("%1 %2 clip(s)").arg(cut ? tr("Cut") : tr("Copied")).arg(session_->clipboard().items.size()), 3000);
}

void MainWindow::pasteClipboard(ops::EditMode mode) {
    auto r = session_->paste(viewer_->position(), mode);
    if (!r.ok()) {
        showError(tr("The clips could not be pasted."), qs(r.error().toString()));
        return;
    }
    if (!r.value().empty()) timeline()->selectClip(qs(r.value().front()));
    viewer_->setPosition(viewer_->position() + session_->clipboard().span);  // continue after the paste
}

void MainWindow::duplicateSelection() {
    const auto clips = selectedClips();
    if (clips.empty()) {
        statusBar()->showMessage(tr("Select a clip in the timeline first."), 3000);
        return;
    }
    auto r = session_->duplicateClips(clips);
    if (!r.ok()) {
        showError(tr("The clips could not be duplicated."), qs(r.error().toString()));
        return;
    }
    if (!r.value().empty()) timeline()->selectClip(qs(r.value().front()));
}

void MainWindow::addMarkerAtPlayhead(bool onClip) {
    const FrameIndex at = viewer_->position();
    if (onClip) {
        const auto clips = selectedClips();
        if (clips.empty()) {
            statusBar()->showMessage(tr("Select the clip to mark first."), 3000);
            return;
        }
        auto r = session_->addClipMarker(clips.front(), at);
        if (!r.ok()) statusBar()->showMessage(qs(r.error().message), 4000);
        return;
    }
    auto r = session_->addMarker(at);
    if (!r.ok()) statusBar()->showMessage(qs(r.error().message), 4000);
}

void MainWindow::jumpToMarker(bool next) {
    const Timeline& tl = session_->timeline();
    const auto target = next ? tl.nextMarker(viewer_->position()) : tl.previousMarker(viewer_->position());
    if (target) {
        setActiveViewer(viewer_);
        viewer_->setPosition(*target);
    } else {
        statusBar()->showMessage(next ? tr("No later marker.") : tr("No earlier marker."), 2000);
    }
}

void MainWindow::editMarker(const QString& markerId) {
    const auto ref = session_->findMarker(markerId.toStdString());
    if (!ref) return;
    MarkerDialog dialog(ref->marker, !ref->clipId.empty(), this);
    const int outcome = dialog.exec();
    if (outcome == MarkerDialog::Saved) runEdit([&] { return session_->updateMarker(dialog.marker()); });
    else if (outcome == MarkerDialog::DeleteRequested) runEdit([&] { return session_->removeMarker(markerId.toStdString()); });
}

}  // namespace up::ui

