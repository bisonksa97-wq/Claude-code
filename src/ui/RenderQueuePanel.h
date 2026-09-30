#pragma once

#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class QTimer;
class QTreeWidget;

namespace up::render {
class RenderQueue;
}

namespace up::ui {

// Shows the render queue: one row per job with its preset, state and progress, the
// log of the selected job, and Cancel / Remove / Clear Finished. The queue notifies
// from its worker thread; updates are marshalled here and coalesced (~10 per second).
class RenderQueuePanel : public QWidget {
    Q_OBJECT
public:
    explicit RenderQueuePanel(QWidget* parent = nullptr);
    ~RenderQueuePanel() override;

    // The queue must outlive the panel's use of it (setQueue(nullptr) detaches).
    void setQueue(render::RenderQueue* queue);
    void refresh();

    QTreeWidget* list() const { return list_; }
    QPlainTextEdit* logView() const { return log_; }
    QPushButton* cancelButton() const { return cancel_; }
    QPushButton* removeButton() const { return remove_; }
    QPushButton* clearButton() const { return clear_; }

signals:
    // Emitted (on the UI thread) when a job finishes, fails or is cancelled.
    void jobFinished(int id, const QString& summary);

private:
    int selectedId() const;

    render::RenderQueue* queue_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* clear_ = nullptr;
    QTimer* coalesce_ = nullptr;
    QList<int> reported_;  // finished jobs already announced
};

}  // namespace up::ui
