#include "ui/RenderQueuePanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "render/RenderQueue.h"
#include "ui/Theme.h"

namespace up::ui {

using render::RenderQueue;

RenderQueuePanel::RenderQueuePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* splitter = new QSplitter(Qt::Vertical, this);
    list_ = new QTreeWidget(splitter);
    list_->setRootIsDecorated(false);
    list_->setHeaderLabels({tr("Job"), tr("Preset"), tr("Status"), tr("Progress"), tr("Output")});
    list_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    list_->setAccessibleName(tr("Render queue"));
    log_ = new QPlainTextEdit(splitter);
    log_->setReadOnly(true);
    log_->setAccessibleName(tr("Job log"));
    log_->setPlaceholderText(tr("Select a job to see its log."));
    splitter->addWidget(list_);
    splitter->addWidget(log_);
    layout->addWidget(splitter, 1);

    auto* buttons = new QHBoxLayout;
    cancel_ = new QPushButton(tr("Cancel"), this);
    remove_ = new QPushButton(tr("Remove"), this);
    clear_ = new QPushButton(tr("Clear Finished"), this);
    buttons->addWidget(cancel_);
    buttons->addWidget(remove_);
    buttons->addStretch(1);
    buttons->addWidget(clear_);
    layout->addLayout(buttons);

    coalesce_ = new QTimer(this);
    coalesce_->setSingleShot(true);
    coalesce_->setInterval(100);
    connect(coalesce_, &QTimer::timeout, this, &RenderQueuePanel::refresh);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &RenderQueuePanel::refresh);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        if (queue_ && selectedId() > 0) queue_->cancel(selectedId());
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (queue_ && selectedId() > 0) queue_->remove(selectedId());
        refresh();
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        if (queue_) queue_->clearFinished();
        refresh();
    });
    refresh();
}

RenderQueuePanel::~RenderQueuePanel() { setQueue(nullptr); }

void RenderQueuePanel::setQueue(RenderQueue* queue) {
    if (queue_) queue_->setListener({});
    queue_ = queue;
    if (queue_) {
        // Worker thread -> UI thread, coalesced by the single-shot timer.
        queue_->setListener([this] { QMetaObject::invokeMethod(coalesce_, qOverload<>(&QTimer::start), Qt::QueuedConnection); });
    }
    refresh();
}

int RenderQueuePanel::selectedId() const {
    const auto items = list_->selectedItems();
    return items.isEmpty() ? 0 : items.first()->data(0, Qt::UserRole).toInt();
}

void RenderQueuePanel::refresh() {
    const auto jobs = queue_ ? queue_->jobs() : std::vector<RenderQueue::Job>{};
    const int selected = selectedId();
    const QSignalBlocker block(list_);
    list_->clear();
    const auto& tokens = currentTokens();
    const RenderQueue::Job* shown = nullptr;
    for (const auto& job : jobs) {
        auto* item = new QTreeWidgetItem(list_);
        item->setData(0, Qt::UserRole, job.id);
        item->setText(0, QString::fromStdString(job.name));
        item->setText(1, QString::fromStdString(job.presetName));
        item->setText(2, tr(RenderQueue::toString(job.state)));
        item->setText(4, QString::fromStdString(job.output.string()));
        item->setToolTip(2, QString::fromStdString(job.message));
        if (job.state == RenderQueue::State::Failed) item->setForeground(2, tokens.warning);
        auto* bar = new QProgressBar(list_);
        bar->setRange(0, static_cast<int>(std::max<FrameIndex>(1, job.framesTotal)));
        bar->setValue(static_cast<int>(job.state == RenderQueue::State::Done ? std::max<FrameIndex>(1, job.framesTotal) : job.framesDone));
        bar->setFormat(job.state == RenderQueue::State::Queued ? tr("waiting") : QStringLiteral("%p%"));
        list_->setItemWidget(item, 3, bar);
        if (job.id == selected) {
            item->setSelected(true);
            shown = &job;
        }
        const bool finished = job.state == RenderQueue::State::Done || job.state == RenderQueue::State::Failed ||
                              job.state == RenderQueue::State::Cancelled;
        if (finished && !reported_.contains(job.id)) {
            reported_.append(job.id);
            emit jobFinished(job.id, QString("%1: %2").arg(QString::fromStdString(job.name), QString::fromStdString(job.message)));
        }
    }
    QStringList lines;
    if (shown)
        for (const auto& line : shown->log) lines << QString::fromStdString(line);
    if (log_->toPlainText() != lines.join('\n')) log_->setPlainText(lines.join('\n'));
    const bool running = shown && shown->state == RenderQueue::State::Running;
    const bool queued = shown && shown->state == RenderQueue::State::Queued;
    cancel_->setEnabled(running || queued);
    remove_->setEnabled(shown && !running);
    clear_->setEnabled(std::any_of(jobs.begin(), jobs.end(), [](const auto& j) {
        return j.state == RenderQueue::State::Done || j.state == RenderQueue::State::Failed || j.state == RenderQueue::State::Cancelled;
    }));
}

}  // namespace up::ui
