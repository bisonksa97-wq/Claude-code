#pragma once

#include <QDialog>
#include <optional>
#include <vector>

#include "render/ExportJob.h"
#include "render/ExportPresets.h"

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class QWidget;

namespace up::ui {

// Chooses a delivery preset, the output file (or folder, for image sequences), the
// range, and HDR metadata for PQ/HLG outputs. The result is queued, not run here.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    // `defaultOutput` is used without its extension; the preset's extension is added.
    ExportDialog(std::vector<render::ExportPreset> presets, const Timeline& timeline, const QString& defaultOutput,
                 QWidget* parent = nullptr);

    render::ExportOptions options() const;
    QString jobName() const;

    // For tests and automation.
    QComboBox* presetBox() const { return preset_; }
    QLineEdit* outputEdit() const { return output_; }
    QRadioButton* marksRange() const { return marks_; }
    bool hdrControlsVisible() const;
    void selectPreset(const QString& id);

private:
    void presetChanged();
    void browse();
    void validate();
    const render::ExportPreset* current() const;

    std::vector<render::ExportPreset> presets_;
    const Timeline& timeline_;
    QComboBox* preset_ = nullptr;
    QLabel* description_ = nullptr;
    QLineEdit* output_ = nullptr;
    QRadioButton* whole_ = nullptr;
    QRadioButton* marks_ = nullptr;
    QWidget* hdr_ = nullptr;
    QSpinBox* peak_ = nullptr;
    QSpinBox* maxCll_ = nullptr;
    QSpinBox* maxFall_ = nullptr;
    QLabel* problem_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

}  // namespace up::ui
