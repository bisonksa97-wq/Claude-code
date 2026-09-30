#include "ui/ExportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "core/Timecode.h"
#include "ui/Theme.h"

namespace up::ui {

ExportDialog::ExportDialog(std::vector<render::ExportPreset> presets, const Timeline& timeline, const QString& defaultOutput,
                           QWidget* parent)
    : QDialog(parent), presets_(std::move(presets)), timeline_(timeline) {
    setWindowTitle(tr("Export"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    preset_ = new QComboBox(this);
    preset_->setAccessibleName(tr("Preset"));
    auto* model = qobject_cast<QStandardItemModel*>(preset_->model());
    for (const auto& p : presets_) {
        const std::string missing = render::presetUnavailableReason(p);
        preset_->addItem(QString::fromStdString(p.name) + (p.builtIn ? "" : tr(" (user)")), QString::fromStdString(p.id));
        if (!missing.empty() && model) {
            QStandardItem* item = model->item(preset_->count() - 1);
            item->setEnabled(false);
            item->setToolTip(tr("Unavailable: %1").arg(QString::fromStdString(missing)));
        }
    }
    form->addRow(tr("Preset"), preset_);
    description_ = new QLabel(this);
    description_->setWordWrap(true);
    form->addRow(QString(), description_);

    auto* outputRow = new QHBoxLayout;
    output_ = new QLineEdit(defaultOutput, this);
    output_->setAccessibleName(tr("Output"));
    auto* browse = new QPushButton(tr("Browse…"), this);
    outputRow->addWidget(output_, 1);
    outputRow->addWidget(browse);
    form->addRow(tr("Output"), outputRow);

    auto* rangeRow = new QHBoxLayout;
    whole_ = new QRadioButton(tr("Whole timeline (%1)").arg(QString::fromStdString(formatTimecode(timeline.duration(), timeline.frameRate))), this);
    marks_ = new QRadioButton(this);
    whole_->setChecked(true);
    const bool hasMarks = timeline.markIn || timeline.markOut;
    marks_->setEnabled(hasMarks);
    if (hasMarks) {
        const FrameIndex in = timeline.markIn.value_or(0);
        const FrameIndex out = timeline.markOut.value_or(timeline.duration());
        marks_->setText(tr("In to out (%1 – %2)")
                            .arg(QString::fromStdString(formatTimecode(in, timeline.frameRate)),
                                 QString::fromStdString(formatTimecode(out, timeline.frameRate))));
    } else {
        marks_->setText(tr("In to out (no marks set)"));
    }
    rangeRow->addWidget(whole_);
    rangeRow->addWidget(marks_);
    form->addRow(tr("Range"), rangeRow);

    hdr_ = new QWidget(this);
    auto* hdrForm = new QFormLayout(hdr_);
    hdrForm->setContentsMargins(0, 0, 0, 0);
    peak_ = new QSpinBox(hdr_);
    peak_->setRange(100, 10000);
    peak_->setValue(1000);
    peak_->setSuffix(tr(" cd/m²"));
    maxCll_ = new QSpinBox(hdr_);
    maxCll_->setRange(0, 10000);
    maxCll_->setSpecialValueText(tr("Not written"));
    maxCll_->setSuffix(tr(" cd/m²"));
    maxFall_ = new QSpinBox(hdr_);
    maxFall_->setRange(0, 10000);
    maxFall_->setSpecialValueText(tr("Not written"));
    maxFall_->setSuffix(tr(" cd/m²"));
    hdrForm->addRow(tr("Mastering peak"), peak_);
    hdrForm->addRow(tr("MaxCLL"), maxCll_);
    hdrForm->addRow(tr("MaxFALL"), maxFall_);
    auto* hdrNote = new QLabel(tr("HDR10 metadata for the %1 output. MaxCLL/MaxFALL are not measured; enter them from your QC tools.")
                                   .arg(QString::fromStdString(timeline.outputSpace().displayName())),
                               hdr_);
    hdrNote->setWordWrap(true);
    hdrForm->addRow(hdrNote);
    form->addRow(hdr_);
    hdr_->setVisible(isHdr(timeline.outputSpace().transfer));

    problem_ = new QLabel(this);
    problem_->setWordWrap(true);
    problem_->setStyleSheet(QString("color: %1").arg(currentTokens().warning.name()));
    layout->addWidget(problem_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Add to Render Queue"));
    layout->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(preset_, &QComboBox::currentIndexChanged, this, &ExportDialog::presetChanged);
    connect(output_, &QLineEdit::textChanged, this, &ExportDialog::validate);

    // Start on the first available preset.
    for (int i = 0; i < preset_->count(); ++i) {
        if (!model || model->item(i)->isEnabled()) {
            preset_->setCurrentIndex(i);
            break;
        }
    }
    presetChanged();
}

const render::ExportPreset* ExportDialog::current() const {
    return render::findPreset(presets_, preset_->currentData().toString().toStdString());
}

void ExportDialog::selectPreset(const QString& id) {
    const int index = preset_->findData(id);
    if (index >= 0) preset_->setCurrentIndex(index);
}

bool ExportDialog::hdrControlsVisible() const { return !hdr_->isHidden(); }

void ExportDialog::presetChanged() {
    const render::ExportPreset* p = current();
    if (!p) return;
    description_->setText(QString::fromStdString(p->description));
    // Keep the name, swap the extension (image sequences are a folder: no extension).
    QFileInfo info(output_->text());
    QString base = info.path() + "/" + info.completeBaseName();
    if (info.suffix().isEmpty()) base = output_->text();
    output_->setText(p->imageSequence() ? base : base + QString::fromStdString(p->extension));
    hdr_->setVisible(p->video && isHdr(timeline_.outputSpace().transfer));
    validate();
}

void ExportDialog::browse() {
    const render::ExportPreset* p = current();
    if (!p) return;
    const QString ext = QString::fromStdString(p->extension);
    const QString chosen = p->imageSequence()
                               ? QFileDialog::getSaveFileName(this, tr("Folder for the image sequence"), output_->text())
                               : QFileDialog::getSaveFileName(this, tr("Export to"), output_->text(), tr("%1 files (*%2)").arg(ext.mid(1).toUpper(), ext));
    if (!chosen.isEmpty()) output_->setText(chosen);
}

void ExportDialog::validate() {
    QString problem;
    const render::ExportPreset* p = current();
    if (!p) problem = tr("Choose a preset.");
    else if (auto missing = render::presetUnavailableReason(*p); !missing.empty()) problem = tr("This preset cannot be used: %1.").arg(QString::fromStdString(missing));
    else if (output_->text().trimmed().isEmpty()) problem = tr("Choose where to write the export.");
    else if (p->imageSequence() && QFileInfo::exists(output_->text())) problem = tr("That folder already exists; image sequences go into a new folder.");
    else if (timeline_.duration() == 0) problem = tr("The timeline is empty.");
    else if (!p->video && !p->audio) problem = tr("The preset writes nothing.");
    problem_->setText(problem);
    problem_->setVisible(!problem.isEmpty());
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

render::ExportOptions ExportDialog::options() const {
    render::ExportOptions o;
    o.output = output_->text().toStdString();
    if (const render::ExportPreset* p = current()) o.preset = *p;
    if (marks_->isChecked()) {
        o.inFrame = timeline_.markIn.value_or(0);
        o.outFrame = timeline_.markOut.value_or(0);
    }
    o.masteringMaxLuminance = peak_->value();
    o.maxCll = maxCll_->value();
    o.maxFall = maxFall_->value();
    return o;
}

QString ExportDialog::jobName() const {
    return QFileInfo(output_->text()).fileName();
}

}  // namespace up::ui
