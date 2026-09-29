#include "ui/MediaPoolPanel.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/EditorSession.h"
#include "core/Timecode.h"
#include "media/MediaLibrary.h"
#include "ui/Theme.h"

namespace up::ui {

QStringList MediaTree::mimeTypes() const { return {kMediaMimeType}; }

QMimeData* MediaTree::mimeData(const QList<QTreeWidgetItem*>& items) const {
    if (items.isEmpty()) return nullptr;
    auto* mime = new QMimeData;
    mime->setData(kMediaMimeType, items.first()->data(0, Qt::UserRole).toString().toUtf8());
    return mime;
}

MediaPoolPanel::MediaPoolPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search media (name, codec, keyword)"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("Search media"));
    tree_ = new MediaTree(this);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({tr("Name"), tr("Duration"), tr("Format"), tr("Status")});
    tree_->setRootIsDecorated(false);
    tree_->setDragEnabled(true);
    tree_->setDragDropMode(QAbstractItemView::DragOnly);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setAccessibleName(tr("Media pool"));
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    auto* import = new QPushButton(tr("Import Media…"), this);
    layout->addWidget(search_);
    layout->addWidget(tree_, 1);
    layout->addWidget(import);

    connect(import, &QPushButton::clicked, this, &MediaPoolPanel::importRequested);
    connect(search_, &QLineEdit::textChanged, this, &MediaPoolPanel::refresh);
    connect(tree_, &QWidget::customContextMenuRequested, this, &MediaPoolPanel::showContextMenu);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        emit mediaActivated(item->data(0, Qt::UserRole).toString());
    });
}

void MediaPoolPanel::setSession(EditorSession* session) {
    session_ = session;
    refresh();
}

int MediaPoolPanel::itemCount() const { return tree_->topLevelItemCount(); }

QString MediaPoolPanel::selectedMediaId() const {
    const auto items = tree_->selectedItems();
    return items.isEmpty() ? QString() : items.first()->data(0, Qt::UserRole).toString();
}

void MediaPoolPanel::refresh() {
    const QString selected = selectedMediaId();
    tree_->clear();
    if (!session_) return;
    const auto& tokens = currentTokens();
    const FrameRate rate = session_->timeline().frameRate;
    for (const MediaItem* m : media::search(session_->project(), search_->text().toStdString())) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setData(0, Qt::UserRole, QString::fromStdString(m->id));
        item->setText(0, QString::fromStdString(m->name));
        item->setToolTip(0, QString::fromStdString(m->path.string()));
        item->setText(1, m->info.isStill ? tr("Still")
                                         : QString::fromStdString(formatTimecode(
                                               secondsToFrames(m->info.durationSeconds, rate), rate)));
        QStringList format;
        if (m->info.hasVideo)
            format << QString("%1x%2 %3").arg(m->info.width).arg(m->info.height).arg(QString::fromStdString(m->info.videoCodec));
        if (m->info.hasAudio)
            format << QString("%1 %2ch").arg(QString::fromStdString(m->info.audioCodec)).arg(m->info.channels);
        item->setText(2, format.join(" · "));
        item->setText(3, m->online ? tr("Online") : tr("Offline"));
        if (!m->online) {
            for (int c = 0; c < 4; ++c) item->setForeground(c, tokens.warning);
            item->setToolTip(3, tr("File not found: %1. Right-click to relink.").arg(QString::fromStdString(m->path.string())));
        }
        if (item->data(0, Qt::UserRole).toString() == selected) item->setSelected(true);
    }
}

void MediaPoolPanel::showContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = tree_->itemAt(pos);
    QMenu menu(this);
    menu.addAction(tr("Import Media…"), this, &MediaPoolPanel::importRequested);
    if (item) {
        const QString id = item->data(0, Qt::UserRole).toString();
        menu.addAction(tr("Relink / Replace Source…"), this, [this, id] { emit relinkRequested(id); });
    }
    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

}  // namespace up::ui
