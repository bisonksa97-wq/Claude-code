#include "ui/EffectsDialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/EditorSession.h"

namespace up::ui {

EffectsDialog::EffectsDialog(EditorSession* session, std::string trackId, QWidget* parent)
    : QDialog(parent), session_(session), trackId_(std::move(trackId)) {
    const Track* track = session_->timeline().track(trackId_);
    setWindowTitle(tr("Effects — %1").arg(track ? QString::fromStdString(track->name) : QString()));
    auto* layout = new QHBoxLayout(this);

    auto* left = new QVBoxLayout;
    list_ = new QListWidget(this);
    list_->setAccessibleName(tr("Effect chain"));
    auto* add = new QPushButton(tr("Add"), this);
    auto* menu = new QMenu(add);
    for (const auto& type : audio::effectTypes()) {
        const std::string id = type.type;
        menu->addAction(QString::fromStdString(type.label), this, [this, id] { addEffect(id); });
    }
    add->setMenu(menu);
    auto* remove = new QPushButton(tr("Remove"), this);
    auto* up = new QPushButton(tr("Move Up"), this);
    auto* down = new QPushButton(tr("Move Down"), this);
    auto* buttons = new QHBoxLayout;
    for (QPushButton* b : {add, remove, up, down}) buttons->addWidget(b);
    left->addWidget(new QLabel(tr("Processed top to bottom, before track gain and pan."), this));
    left->addWidget(list_, 1);
    left->addLayout(buttons);
    layout->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    paramsHost_ = new QWidget(this);
    paramsForm_ = new QFormLayout(paramsHost_);
    right->addWidget(paramsHost_, 1);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(close);
    layout->addLayout(right, 1);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { showParameters(); });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (updating_) return;
        const Track* t = session_->timeline().track(trackId_);
        const int row = list_->row(item);
        if (!t || row < 0 || row >= static_cast<int>(t->effects.size())) return;
        audio::EffectSpec fx = t->effects[static_cast<std::size_t>(row)];
        fx.enabled = item->checkState() == Qt::Checked;
        report(session_->updateTrackEffect(trackId_, fx));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const Track* t = session_->timeline().track(trackId_);
        const int row = list_->currentRow();
        if (!t || row < 0) return;
        report(session_->removeTrackEffect(trackId_, t->effects[static_cast<std::size_t>(row)].id));
        reloadList();
    });
    auto move = [this](int delta) {
        const Track* t = session_->timeline().track(trackId_);
        const int row = list_->currentRow();
        if (!t || row < 0 || row + delta < 0 || row + delta >= static_cast<int>(t->effects.size())) return;
        report(session_->moveTrackEffect(trackId_, t->effects[static_cast<std::size_t>(row)].id, row + delta));
        reloadList();
        list_->setCurrentRow(row + delta);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    reloadList();
}

void EffectsDialog::addEffect(const std::string& type) {
    auto r = session_->addTrackEffect(trackId_, type);
    if (!r.ok()) {
        report(r.error());
        return;
    }
    reloadList();
    list_->setCurrentRow(list_->count() - 1);
}

void EffectsDialog::selectEffect(int row) { list_->setCurrentRow(row); }

void EffectsDialog::reloadList() {
    updating_ = true;
    const int row = list_->currentRow();
    list_->clear();
    if (const Track* t = session_->timeline().track(trackId_)) {
        for (const auto& fx : t->effects) {
            const audio::EffectTypeInfo* info = audio::effectType(fx.type);
            auto* item = new QListWidgetItem(info ? QString::fromStdString(info->label) : QString::fromStdString(fx.type), list_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(fx.enabled ? Qt::Checked : Qt::Unchecked);
        }
    }
    list_->setCurrentRow(std::min(row, list_->count() - 1));
    updating_ = false;
    showParameters();
}

void EffectsDialog::showParameters() {
    while (paramsForm_->rowCount() > 0) paramsForm_->removeRow(0);
    const Track* t = session_->timeline().track(trackId_);
    const int row = list_->currentRow();
    if (!t || row < 0 || row >= static_cast<int>(t->effects.size())) return;
    const audio::EffectSpec& fx = t->effects[static_cast<std::size_t>(row)];
    const audio::EffectTypeInfo* info = audio::effectType(fx.type);
    if (!info) return;
    const std::string effectId = fx.id;
    for (const auto& p : info->params) {
        auto* spin = new QDoubleSpinBox(paramsHost_);
        spin->setRange(p.minimum, p.maximum);
        spin->setDecimals(p.maximum - p.minimum > 100 ? 0 : 2);
        spin->setSuffix(p.unit.empty() ? QString() : " " + QString::fromStdString(p.unit));
        spin->setValue(fx.param(p.id));
        spin->setKeyboardTracking(false);
        spin->setAccessibleName(QString::fromStdString(p.label));
        const std::string paramId = p.id;
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, effectId, paramId](double v) {
            const Track* track = session_->timeline().track(trackId_);
            if (!track) return;
            for (const auto& e : track->effects) {
                if (e.id != effectId) continue;
                audio::EffectSpec updated = e;
                updated.params[paramId] = v;
                report(session_->updateTrackEffect(trackId_, updated));
            }
        });
        paramsForm_->addRow(QString::fromStdString(p.label), spin);
    }
}

void EffectsDialog::report(const Status& status) {
    if (!status.ok()) emit errorOccurred(QString::fromStdString(status.error().message));
}

}  // namespace up::ui
