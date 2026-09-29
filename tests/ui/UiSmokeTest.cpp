// UI tests: drive the real widgets (offscreen) through the first vertical slice.

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QMenuBar>
#include <QMimeData>
#include <QTest>

#include "app/EditorSession.h"
#include "support/TestSupport.h"
#include "ui/MainWindow.h"
#include "ui/MediaPoolPanel.h"
#include "ui/Theme.h"
#include "ui/TimelineView.h"
#include "ui/ViewerPanel.h"

using namespace up;

namespace {

QAction* findAction(QWidget* window, const QString& text) {
    for (QAction* a : window->findChildren<QAction*>())
        if (a->text() == text) return a;
    return nullptr;
}

void dropMedia(ui::TimelineView* view, const QString& mediaId, QPoint pos) {
    QMimeData mime;
    mime.setData(ui::kMediaMimeType, mediaId.toUtf8());
    QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view, &enter);
    QDropEvent drop(QPointF(pos), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view, &drop);
}

}  // namespace

TEST(Ui, EditWorkflowThroughWidgets) {
    test::TempDir dir;
    test::makeMedia(dir / "red.mp4", test::solid(220, 20, 20, 50));
    test::makeMedia(dir / "blue.mp4", test::solid(20, 20, 220, 40));

    ui::applyTheme(*qApp, ui::ThemeKind::Dark);  // as the application does at startup
    ui::MainWindow window(nullptr, /*checkRecovery=*/false);
    window.resize(1280, 800);
    auto session = EditorSession::createNew("UI", SequenceSettings{FrameRate{25, 1}, 320, 240, 48000});
    const auto ids = session->importMedia({dir / "red.mp4", dir / "blue.mp4"}).importedIds;
    ASSERT_EQ(ids.size(), 2u);
    window.setSession(std::move(session));
    window.show();
    QApplication::processEvents();
    EXPECT_EQ(window.mediaPool()->itemCount(), 2);

    ui::TimelineView* tv = window.timeline();
    tv->setPixelsPerFrame(8.0);
    tv->setScrollFrame(0);
    const auto rows = tv->rowTrackIds();
    // Row 1 is V1 (V2 is drawn above it).
    const int v1Y = tv->rowTop(1) + 10;
    dropMedia(tv, QString::fromStdString(ids[0]), QPoint(tv->xForFrame(0) + 1, v1Y));
    const Timeline& tl = window.session()->timeline();
    ASSERT_EQ(tl.tracks[0].clips.size(), 1u);
    EXPECT_EQ(tl.tracks[0].clips[0].start, 0);
    EXPECT_EQ(tl.tracks[2].clips.size(), 1u);  // linked audio on A1

    // Second clip dropped just after the first snaps to its end at frame 50.
    tv->setPixelsPerFrame(2.0);
    dropMedia(tv, QString::fromStdString(ids[1]), QPoint(tv->xForFrame(52), v1Y));
    tv->setPixelsPerFrame(8.0);
    ASSERT_EQ(tl.tracks[0].clips.size(), 2u);
    EXPECT_EQ(tl.tracks[0].clips[1].start, 50);

    // Viewer shows the first clip.
    window.viewer()->setPosition(10);
    QApplication::processEvents();
    const QImage img = window.viewer()->currentImage();
    ASSERT_FALSE(img.isNull());
    const QColor c = img.pixelColor(img.width() / 2, img.height() / 2);
    EXPECT_GT(c.red(), 180);
    EXPECT_LT(c.blue(), 60);

    // Razor at playhead via the menu action.
    window.viewer()->setPosition(25);
    QAction* razor = findAction(&window, "Razor at Playhead");
    ASSERT_NE(razor, nullptr);
    razor->trigger();
    EXPECT_EQ(tl.tracks[0].clips.size(), 3u);

    // Drag the out-edge of V1:2 (frames 25..50) left by 5 frames.
    const std::string second = tl.tracks[0].clips[1].id;
    const QRect r = tv->clipRect(second);
    const QPoint grab(r.right() - 1, r.center().y());
    QTest::mousePress(tv, Qt::LeftButton, Qt::NoModifier, grab);
    QTest::mouseMove(tv, grab - QPoint(20, 0));
    QTest::mouseMove(tv, grab - QPoint(40, 0));
    QTest::mouseRelease(tv, Qt::LeftButton, Qt::NoModifier, grab - QPoint(40, 0));
    EXPECT_EQ(tl.clip(second)->end(), 45);
    EXPECT_EQ(tl.tracks[2].clips[1].end(), 45);  // linked audio followed

    // Undo through the Edit menu restores the edge.
    QAction* undo = nullptr;
    for (QAction* a : window.findChildren<QAction*>())
        if (a->text().startsWith("&Undo")) undo = a;
    ASSERT_NE(undo, nullptr);
    undo->trigger();
    EXPECT_EQ(tl.clip(second)->end(), 50);
    EXPECT_TRUE(window.isWindowModified());

    // Save and export through the window.
    ASSERT_TRUE(window.session()->saveAs(dir / "ui.uproj").ok());
    EXPECT_FALSE(window.isWindowModified());
    ASSERT_TRUE(window.exportTo(QString::fromStdString((dir / "ui.mp4").string()), false));
    EXPECT_TRUE(std::filesystem::exists(dir / "ui.mp4"));

    if (const char* shot = std::getenv("UP_UI_SCREENSHOT")) {
        window.viewer()->setPosition(60);
        tv->selectClip(QString::fromStdString(second));
        QApplication::processEvents();
        window.grab().save(QString::fromLocal8Bit(shot));
    }
}

TEST(Ui, ThemesUseCentralTokens) {
    for (auto kind : {ui::ThemeKind::Dark, ui::ThemeKind::Light, ui::ThemeKind::HighContrast}) {
        ui::applyTheme(*qApp, kind);
        EXPECT_EQ(qApp->palette().color(QPalette::Window), ui::currentTokens().window);
    }
    ui::applyTheme(*qApp, ui::ThemeKind::Dark);
}

int main(int argc, char** argv) {
    // UI tests are headless by default (CI, test discovery); set QT_QPA_PLATFORM to override.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
