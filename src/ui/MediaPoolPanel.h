#pragma once

#include <QTreeWidget>
#include <QWidget>

class QLineEdit;

namespace up {
class EditorSession;
}

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

// Media browser: lists project media with technical metadata and online state,
// supports search, import and relinking.
class MediaPoolPanel : public QWidget {
    Q_OBJECT
public:
    explicit MediaPoolPanel(QWidget* parent = nullptr);

    void setSession(EditorSession* session);
    void refresh();
    int itemCount() const;
    QString selectedMediaId() const;

signals:
    void importRequested();
    void relinkRequested(const QString& mediaId);
    void mediaActivated(const QString& mediaId);

private:
    void showContextMenu(const QPoint& pos);

    EditorSession* session_ = nullptr;
    QLineEdit* search_ = nullptr;
    MediaTree* tree_ = nullptr;
};

}  // namespace up::ui
