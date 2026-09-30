#pragma once

#include <QWidget>
#include <array>
#include <string>

#include "core/Rational.h"
#include "core/Result.h"
#include "timeline/Animation.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QToolButton;

namespace up {
class EditorSession;
}

namespace up::ui {

// Shows and edits the selected video clip's transform at the program playhead.
// Edits go through EditorSession (undoable): on an animated parameter they set the
// keyframe at the playhead, otherwise they change the constant value.
class InspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    void setSession(EditorSession* session);
    // Follows the timeline selection; audio clips resolve to their linked video clip.
    void setClip(const std::string& clipId);
    void setPlayhead(FrameIndex frame);
    // Re-reads the model (after any edit or undo).
    void refresh();

    const std::string& clipId() const { return clipId_; }
    // For tests and automation: the widgets of one parameter row.
    QDoubleSpinBox* valueEditor(ClipParam param) const { return rows_[static_cast<std::size_t>(param)].value; }
    QToolButton* keyframeToggle(ClipParam param) const { return rows_[static_cast<std::size_t>(param)].key; }

signals:
    void seekRequested(FrameIndex frame);
    void errorOccurred(const QString& message);

private:
    struct Row {
        QDoubleSpinBox* value = nullptr;
        QToolButton* key = nullptr;
        QToolButton* previous = nullptr;
        QToolButton* next = nullptr;
        QComboBox* interpolation = nullptr;
        QToolButton* reset = nullptr;
    };

    void commitValue(ClipParam param);
    void toggleKey(ClipParam param);
    void jump(ClipParam param, bool forward);
    void report(const Status& status);

    EditorSession* session_ = nullptr;
    std::string clipId_;
    FrameIndex playhead_ = 0;
    QLabel* title_ = nullptr;
    QWidget* form_ = nullptr;
    std::array<Row, kAllClipParams.size()> rows_{};
    bool updating_ = false;
};

}  // namespace up::ui
