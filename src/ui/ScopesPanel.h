#pragma once

#include <QImage>
#include <QWidget>

#include "render/Scopes.h"

class QComboBox;
class QLabel;
class QTimer;

namespace up::ui {

class ViewerPanel;

enum class ScopeMode { Waveform, Parade, Vectorscope, Histogram };

// Draws one scope from computed scope data (256 levels high; see render/Scopes.h).
QImage renderScopeImage(const render::Scopes& scopes, ScopeMode mode);

class ScopeView : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    void setImage(QImage image, ScopeMode mode);
    const QImage& image() const { return image_; }
    QSize sizeHint() const override { return {320, 220}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    ScopeMode mode_ = ScopeMode::Waveform;
};

// Video scopes of the program monitor's current picture (what the viewer shows,
// i.e. the graded composite at preview resolution, reduced to at most 480 px wide).
// Scopes are computed on the UI thread, at most ~8 times per second and only while
// the panel is visible.
class ScopesPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScopesPanel(QWidget* parent = nullptr);

    void setViewer(const ViewerPanel* viewer);
    void setMode(ScopeMode mode);
    ScopeMode mode() const;
    // Recomputes from the viewer's current image now (tests; normally timer-driven).
    void updateNow();
    const render::Scopes& scopes() const { return scopes_; }
    const QImage& scopeImage() const;

private:
    void poll();

    const ViewerPanel* viewer_ = nullptr;
    QComboBox* modeBox_ = nullptr;
    ScopeView* view_ = nullptr;
    QLabel* stats_ = nullptr;
    QTimer* timer_ = nullptr;
    qint64 lastKey_ = -1;
    render::Scopes scopes_;
};

}  // namespace up::ui
