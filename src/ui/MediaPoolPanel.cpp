#include "ui/MediaPoolPanel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/EditorSession.h"
#include "app/MediaAssets.h"
#include "core/Timecode.h"
#include "media/MediaLibrary.h"
#include "ui/FrameImage.h"
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
    auto* top = new QHBoxLayout;
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search media (name, codec, keyword)"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("Search media"));
    largeThumbnails_ = new QToolButton(this);
    largeThumbnails_->setText(tr("▣"));
    largeThumbnails_->setCheckable(true);
    largeThumbnails_->setToolTip(tr("Large thumbnails"));
    largeThumbnails_->setAccessibleName(tr("Large thumbnails"));
    top->addWidget(search_, 1);
    top->addWidget(largeThumbnails_);

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
    applyIconSize();
    auto* import = new QPushButton(tr("Import Media…"), this);
    layout->addLayout(top);
    layout->addWidget(tree_, 1);
    layout->addWidget(import);

    connect(import, &QPushButton::clicked, this, &MediaPoolPanel::importRequested);
    connect(search_, &QLineEdit::textChanged, this, &MediaPoolPanel::refresh);
    connect(largeThumbnails_, &QToolButton::toggled, this, &MediaPoolPanel::applyIconSize);
    connect(tree_, &QWidget::customContextMenuRequested, this, &MediaPoolPanel::showContextMenu);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        emit mediaActivated(item->data(0, Qt::UserRole).toString());
    });
}

void MediaPoolPanel::setSession(EditorSession* session) {
    session_ = session;
    refresh();
}

void MediaPoolPanel::setAssets(MediaAssets* assets) {
    assets_ = assets;
    updateThumbnails();
}

int MediaPoolPanel::itemCount() const { return tree_->topLevelItemCount(); }

int MediaPoolPanel::thumbnailCount() const {
    int n = 0;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) n += tree_->topLevelItem(i)->icon(0).isNull() ? 0 : 1;
    return n;
}

QString MediaPoolPanel::selectedMediaId() const {
    const auto items = tree_->selectedItems();
    return items.isEmpty() ? QString() : items.first()->data(0, Qt::UserRole).toString();
}

void MediaPoolPanel::applyIconSize() {
    // 16:9 icons whose size follows the font, so they scale with the UI.
    const int h = fontMetrics().height() * (largeThumbnails_->isChecked() ? 5 : 2);
    tree_->setIconSize(QSize(h * 16 / 9, h));
}

void MediaPoolPanel::updateThumbnails() {
    if (!session_ || !assets_) return;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree_->topLevelItem(i);
        if (!item->icon(0).isNull()) continue;
        const MediaItem* m = session_->project().findMedia(item->data(0, Qt::UserRole).toString().toStdString());
        if (!m) continue;
        if (auto thumb = assets_->thumbnail(*m)) item->setIcon(0, QIcon(QPixmap::fromImage(toQImage(*thumb))));
    }
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
    updateThumbnails();
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
