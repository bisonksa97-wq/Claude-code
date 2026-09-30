#pragma once

#include <QDialog>

#include "timeline/Timeline.h"

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;

namespace up::ui {

// Edits a marker's name, colour, comment and length. The dialog only edits a copy;
// the caller applies the result through EditorSession::updateMarker (undoable).
class MarkerDialog : public QDialog {
    Q_OBJECT
public:
    enum Outcome { Cancelled = 0, Saved = 1, DeleteRequested = 2 };

    MarkerDialog(const Marker& marker, bool isClipMarker, QWidget* parent = nullptr);
    Marker marker() const;

private:
    Marker marker_;
    QLineEdit* name_ = nullptr;
    QComboBox* color_ = nullptr;
    QPlainTextEdit* comment_ = nullptr;
    QSpinBox* duration_ = nullptr;
};

}  // namespace up::ui
