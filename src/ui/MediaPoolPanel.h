#pragma once

#include <QTreeWidget>
#include <QWidget>

class QLineEdit;
class QToolButton;

namespace up {
class EditorSession;
class MediaAssets;
}  // namespace up

namespace up::ui {

inline constexpr const char* kMediaMimeType = "application/x-ultimatepost-media-id";

// Tree widget that drags media ids (not files) onto the timeline.
class MediaTree : public QTreeWidget {
    Q_OBJECT
public:
    using QTreeWidget::QTreeWidget;

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    QStringList mimeTypes() const override;
};

// Media browser: lists project media with thumbnails, technical metadata and
// online state; supports search, import and relinking.
class MediaPoolPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaPoolPanel(QWidget* parent = nullptr);

    void setSession(EditorSession* session);
    // Source of thumbnails (generated in the background; may be null).
    void setAssets(MediaAssets* assets);
    void refresh();
    // Fills in thumbnails that have become ready since the last refresh.
    void updateThumbnails();
    int itemCount() const;
    int thumbnailCount() const;
    QString selectedMediaId() const;

signals:
    void importRequested();
    void relinkRequested(const QString& mediaId);
    void mediaActivated(const QString& mediaId);

private:
    void showContextMenu(const QPoint& pos);
    void applyIconSize();

    EditorSession* session_ = nullptr;
    MediaAssets* assets_ = nullptr;
    QLineEdit* search_ = nullptr;
    QToolButton* largeThumbnails_ = nullptr;
    MediaTree* tree_ = nullptr;
};

}  // namespace up::ui
