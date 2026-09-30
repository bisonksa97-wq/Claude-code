#pragma once

#include <QWidget>
#include <array>
#include <string>

#include "core/Rational.h"
#include "core/Result.h"
#include "timeline/Grade.h"

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QToolButton;

namespace up {
class EditorSession;
}

namespace up::ui {

// A colour-balance wheel: the puck's direction is a hue on the vectorscope (Rec.709
// Cb/Cr plane, red at upper left like the scope) and its distance the strength.
// Dragging previews locally and emits `balanceCommitted` once on release, so a
// drag is one undo step. Double-click emits a reset to neutral.
class ColorWheel : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheel(QWidget* parent = nullptr);
    // Puck position in the unit disk (x = Cb direction, y = Cr direction, up positive).
    void setBalance(double x, double y);
    double balanceX() const { return x_; }
    double balanceY() const { return y_; }
    QSize sizeHint() const override { return {96, 96}; }
    QSize minimumSizeHint() const override { return {64, 64}; }

signals:
    void balanceCommitted(double x, double y);
    void resetRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void moveTo(const QPointF& pos);
    double x_ = 0.0;
    double y_ = 0.0;
    bool dragging_ = false;
};

// Converts between a wheel position and per-channel offsets (dR, dG, dB) with zero
// Rec.709 luma: moving the puck changes hue/saturation, never brightness. `range`
// is the channel-offset magnitude at the rim.
std::array<double, 3> wheelToChannelOffsets(double x, double y, double range);
std::array<double, 2> channelOffsetsToWheel(const std::array<double, 3>& offsets, double range);

// Primary colour correction of the selected video clip at the program playhead:
// lift / gamma / gain / offset wheels with master and per-channel values, plus
// contrast, pivot, saturation, exposure and white balance. Every control is a
// keyframable parameter edited through EditorSession (undoable).
class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget* parent = nullptr);

    void setSession(EditorSession* session);
    void setClip(const std::string& clipId);
    void setPlayhead(FrameIndex frame);
    void refresh();

    const std::string& clipId() const { return clipId_; }
    // For tests and automation.
    QDoubleSpinBox* valueEditor(GradeParam param) const { return rows_[static_cast<std::size_t>(param)].value; }
    QToolButton* keyframeToggle(GradeParam param) const { return rows_[static_cast<std::size_t>(param)].key; }
    // 0 = lift, 1 = gamma, 2 = gain, 3 = offset.
    ColorWheel* wheel(int index) const { return wheels_[static_cast<std::size_t>(index)]; }
    QPushButton* copyButton() const { return copy_; }
    QPushButton* pasteButton() const { return paste_; }
    QPushButton* resetButton() const { return reset_; }
    // Applies a wheel position to a wheel's R/G/B parameters (as a drag release does).
    void commitWheel(int index, double x, double y);

signals:
    // Paste targets are the timeline selection, which the window owns.
    void pasteRequested();
    void errorOccurred(const QString& message);

private:
    struct Row {
        QLabel* label = nullptr;
        QDoubleSpinBox* value = nullptr;
        QToolButton* key = nullptr;
        QToolButton* reset = nullptr;
    };

    void commitValue(GradeParam param);
    void toggleKey(GradeParam param);
    void report(const Status& status);

    EditorSession* session_ = nullptr;
    std::string clipId_;
    FrameIndex playhead_ = 0;
    QLabel* title_ = nullptr;
    QWidget* form_ = nullptr;
    std::array<Row, kGradeParamCount> rows_{};
    std::array<ColorWheel*, 4> wheels_{};
    QPushButton* copy_ = nullptr;
    QPushButton* paste_ = nullptr;
    QPushButton* reset_ = nullptr;
    bool updating_ = false;
};

}  // namespace up::ui
