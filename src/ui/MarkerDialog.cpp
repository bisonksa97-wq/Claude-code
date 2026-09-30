#include "ui/MarkerDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include "ui/Theme.h"

namespace up::ui {

MarkerDialog::MarkerDialog(const Marker& marker, bool isClipMarker, QWidget* parent)
    : QDialog(parent), marker_(marker) {
    setWindowTitle(isClipMarker ? tr("Clip Marker") : tr("Marker"));
    auto* form = new QFormLayout(this);
    name_ = new QLineEdit(QString::fromStdString(marker.name), this);
    name_->setAccessibleName(tr("Marker name"));
    color_ = new QComboBox(this);
    const QString names[] = {tr("Red"), tr("Orange"), tr("Yellow"), tr("Green"), tr("Blue"), tr("Purple")};
    for (int i = 0; i < 6; ++i) {
        QPixmap swatch(12, 12);
        swatch.fill(markerColor(i));
        color_->addItem(QIcon(swatch), names[i]);
    }
    color_->setCurrentIndex(static_cast<int>(marker.color));
    comment_ = new QPlainTextEdit(QString::fromStdString(marker.comment), this);
    comment_->setAccessibleName(tr("Marker comment"));
    duration_ = new QSpinBox(this);
    duration_->setRange(0, 10'000'000);
    duration_->setSuffix(tr(" frames"));
    duration_->setValue(static_cast<int>(marker.duration));
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Colour"), color_);
    form->addRow(tr("Length"), duration_);
    form->addRow(tr("Comment"), comment_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* remove = buttons->addButton(tr("Delete Marker"), QDialogButtonBox::DestructiveRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(remove, &QPushButton::clicked, this, [this] { done(DeleteRequested); });
    form->addRow(buttons);
}

Marker MarkerDialog::marker() const {
    Marker m = marker_;
    m.name = name_->text().trimmed().toStdString();
    m.color = static_cast<MarkerColor>(color_->currentIndex());
    m.comment = comment_->toPlainText().toStdString();
    m.duration = duration_->value();
    return m;
}

}  // namespace up::ui
