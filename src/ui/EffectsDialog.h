#pragma once

#include <QDialog>
#include <string>

#include "core/Result.h"

class QFormLayout;
class QListWidget;
class QWidget;

namespace up {
class EditorSession;
}

namespace up::ui {

// Edits one audio track's insert effect chain: add, remove, reorder, enable, and
// parameters. Every change is applied immediately through EditorSession (undoable),
// so the result can be heard while the dialog is open.
class EffectsDialog : public QDialog {
    Q_OBJECT
public:
    EffectsDialog(EditorSession* session, std::string trackId, QWidget* parent = nullptr);

    void addEffect(const std::string& type);
    void selectEffect(int row);

signals:
    void errorOccurred(const QString& message);

private:
    void reloadList();
    void showParameters();
    void report(const Status& status);

    EditorSession* session_;
    std::string trackId_;
    QListWidget* list_ = nullptr;
    QWidget* paramsHost_ = nullptr;
    QFormLayout* paramsForm_ = nullptr;
    bool updating_ = false;
};

}  // namespace up::ui
