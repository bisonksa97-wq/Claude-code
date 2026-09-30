#pragma once

#include <QElapsedTimer>
#include <QWidget>
#include <map>
#include <string>
#include <vector>

#include "render/AudioMixer.h"

class QDial;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;

namespace up {
class EditorSession;
}

namespace up::ui {

class ViewerPanel;

// Stereo level meter (dBFS, -60..0) with fall-off ballistics and a peak-hold marker.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    // Feeds new linear peaks; the display rises instantly and falls at kFallDbPerSecond.
    void setLevels(float peakLeft, float peakRight);
    double displayedDb(int channel) const { return shown_[channel]; }
    QSize sizeHint() const override;

    static constexpr double kFloorDb = -60.0;
    static constexpr double kFallDbPerSecond = 24.0;
    static constexpr int kHoldMs = 1200;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double shown_[2] = {kFloorDb, kFloorDb};
    double hold_[2] = {kFloorDb, kFloorDb};
    qint64 holdSince_[2] = {0, 0};
    QElapsedTimer clock_;
    qint64 last_ = 0;
};

// Audio mixer: one strip per audio track (meter, fader, pan, mute, solo, effects)
// and a master meter. Controls write through EditorSession (undoable); meters follow
// the program monitor's playback.
class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(QWidget* parent = nullptr);

    void setSession(EditorSession* session);
    void setViewer(ViewerPanel* viewer) { viewer_ = viewer; }
    // Re-reads track state; rebuilds the strips when tracks were added/removed/reordered.
    void refresh();
    // Pushes meter values (normally called by the internal timer; public for tests).
    void showMeters(const render::MixMeters& meters);

    int stripCount() const { return static_cast<int>(strips_.size()); }
    QSlider* fader(int strip) const { return strips_.at(static_cast<std::size_t>(strip)).fader; }
    QDial* panDial(int strip) const { return strips_.at(static_cast<std::size_t>(strip)).pan; }
    LevelMeter* meter(int strip) const { return strips_.at(static_cast<std::size_t>(strip)).meter; }
    LevelMeter* masterMeter() const { return master_; }

signals:
    void errorOccurred(const QString& message);

private:
    struct Strip {
        std::string trackId;
        QLabel* name = nullptr;
        LevelMeter* meter = nullptr;
        QSlider* fader = nullptr;
        QLabel* gainLabel = nullptr;
        QDial* pan = nullptr;
        QToolButton* mute = nullptr;
        QToolButton* solo = nullptr;
        QPushButton* effects = nullptr;
    };

    void rebuild();
    void commitGain(const std::string& trackId, int tenthsDb);
    void commitPan(const std::string& trackId, int percent);
    void toggle(const std::string& trackId, bool mute);
    void editEffects(const std::string& trackId);
    void tick();

    EditorSession* session_ = nullptr;
    ViewerPanel* viewer_ = nullptr;
    QHBoxLayout* stripsLayout_ = nullptr;
    QWidget* stripsHost_ = nullptr;
    std::vector<Strip> strips_;
    LevelMeter* master_ = nullptr;
    QTimer* timer_ = nullptr;
    bool updating_ = false;
};

}  // namespace up::ui
