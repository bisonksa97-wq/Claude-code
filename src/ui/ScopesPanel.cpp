#include "ui/ScopesPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ui/ViewerPanel.h"

namespace up::ui {
namespace {

constexpr int kMaxScopeWidth = 480;

// Log-scaled brightness so sparse traces stay visible next to dense ones.
double level(uint32_t count, double logMax) {
    if (count == 0 || logMax <= 0) return 0.0;
    return std::clamp(0.25 + 0.75 * std::log1p(count) / logMax, 0.0, 1.0);
}

double logMaxOf(const std::vector<uint32_t>& counts) {
    const uint32_t m = counts.empty() ? 0 : *std::max_element(counts.begin(), counts.end());
    return std::log1p(m);
}

void drawTrace(QImage& image, int xOffset, int columns, const std::vector<uint32_t>& counts, QColor tint) {
    const double logMax = logMaxOf(counts);
    for (int levelIndex = 0; levelIndex < 256; ++levelIndex) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(255 - levelIndex));
        for (int x = 0; x < columns; ++x) {
            const double k = level(counts[static_cast<std::size_t>(levelIndex) * columns + x], logMax);
            if (k <= 0) continue;
            line[xOffset + x] = qRgb(static_cast<int>(tint.red() * k), static_cast<int>(tint.green() * k),
                                     static_cast<int>(tint.blue() * k));
        }
    }
}

VideoFrame toFrame(const QImage& source) {
    QImage image = source.width() > kMaxScopeWidth ? source.scaledToWidth(kMaxScopeWidth, Qt::FastTransformation) : source;
    image = image.convertToFormat(QImage::Format_RGBA8888);
    VideoFrame frame(image.width(), image.height());
    for (int y = 0; y < image.height(); ++y)
        std::copy_n(image.constScanLine(y), static_cast<std::size_t>(image.width()) * 4, frame.row(y));
    return frame;
}

}  // namespace

QImage renderScopeImage(const render::Scopes& s, ScopeMode mode) {
    if (s.columns <= 0) return {};
    switch (mode) {
        case ScopeMode::Waveform: {
            QImage image(s.columns, 256, QImage::Format_RGB32);
            image.fill(Qt::black);
            drawTrace(image, 0, s.columns, s.waveformLuma, QColor(170, 255, 170));
            return image;
        }
        case ScopeMode::Parade: {
            QImage image(s.columns * 3, 256, QImage::Format_RGB32);
            image.fill(Qt::black);
            const std::array<QColor, 3> tints = {QColor(255, 80, 80), QColor(80, 255, 80), QColor(100, 140, 255)};
            for (int c = 0; c < 3; ++c) drawTrace(image, c * s.columns, s.columns, s.parade[static_cast<std::size_t>(c)], tints[static_cast<std::size_t>(c)]);
            return image;
        }
        case ScopeMode::Vectorscope: {
            QImage image(256, 256, QImage::Format_RGB32);
            image.fill(Qt::black);
            const double logMax = logMaxOf(s.vectorscope);
            for (int cr = 0; cr < 256; ++cr) {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine(255 - cr));  // Cr up
                for (int cb = 0; cb < 256; ++cb) {
                    const double k = level(s.vectorscope[static_cast<std::size_t>(cr) * 256 + cb], logMax);
                    if (k > 0) line[cb] = qRgb(static_cast<int>(200 * k), static_cast<int>(255 * k), static_cast<int>(200 * k));
                }
            }
            return image;
        }
        case ScopeMode::Histogram: {
            QImage image(256, 256, QImage::Format_RGB32);
            image.fill(Qt::black);
            uint32_t peak = 1;
            for (const auto& h : s.histogramRgb) peak = std::max(peak, *std::max_element(h.begin(), h.end()));
            QPainter p(&image);
            p.setCompositionMode(QPainter::CompositionMode_Plus);
            const std::array<QColor, 3> tints = {QColor(200, 40, 40), QColor(40, 200, 40), QColor(50, 80, 220)};
            for (int c = 0; c < 3; ++c) {
                for (int bin = 0; bin < 256; ++bin) {
                    const int h = static_cast<int>(std::lround(255.0 * s.histogramRgb[static_cast<std::size_t>(c)][static_cast<std::size_t>(bin)] / peak));
                    if (h > 0) p.fillRect(bin, 256 - h, 1, h, tints[static_cast<std::size_t>(c)]);
                }
            }
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.setPen(QColor(230, 230, 230));
            for (int bin = 1; bin < 256; ++bin) {
                const auto y = [&](int b) { return 255 - static_cast<int>(std::lround(255.0 * s.histogramLuma[static_cast<std::size_t>(b)] / peak)); };
                p.drawLine(bin - 1, std::min(255, y(bin - 1)), bin, std::min(255, y(bin)));
            }
            return image;
        }
    }
    return {};
}

void ScopeView::setImage(QImage image, ScopeMode mode) {
    image_ = std::move(image);
    mode_ = mode;
    update();
}

void ScopeView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    QRect target = rect().adjusted(4, 4, -4, -4);
    if (mode_ == ScopeMode::Vectorscope) {  // square
        const int side = std::min(target.width(), target.height());
        target = QRect(target.center().x() - side / 2, target.center().y() - side / 2, side, side);
    }
    if (target.width() <= 0 || target.height() <= 0) return;
    if (!image_.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, image_);
    }
    const QColor grid(255, 255, 255, 60);
    p.setPen(grid);
    p.setRenderHint(QPainter::Antialiasing);
    auto yAt = [&](double fraction) { return target.bottom() - fraction * target.height(); };
    if (mode_ == ScopeMode::Waveform || mode_ == ScopeMode::Parade) {
        for (int pct = 0; pct <= 100; pct += 25) {
            const double y = yAt(pct / 100.0);
            p.drawLine(QPointF(target.left(), y), QPointF(target.right(), y));
            p.drawText(QPointF(target.left() + 2, y - 2), QString::number(pct));
        }
        if (mode_ == ScopeMode::Parade) {
            for (int i = 1; i < 3; ++i) {
                const double x = target.left() + target.width() * i / 3.0;
                p.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
            }
        }
    } else if (mode_ == ScopeMode::Vectorscope) {
        const QPointF centre = QRectF(target).center();
        const double r = target.width() / 2.0;
        p.drawEllipse(centre, r, r);
        p.drawLine(QPointF(target.left(), centre.y()), QPointF(target.right(), centre.y()));
        p.drawLine(QPointF(centre.x(), target.top()), QPointF(centre.x(), target.bottom()));
        // 75% colour-bar targets.
        struct Target {
            const char* name;
            uint8_t r, g, b;
        };
        const Target targets[] = {{"R", 191, 0, 0}, {"Yl", 191, 191, 0}, {"G", 0, 191, 0},
                                  {"Cy", 0, 191, 191}, {"B", 0, 0, 191}, {"Mg", 191, 0, 191}};
        for (const auto& t : targets) {
            const auto pos = render::vectorscopePosition(t.r, t.g, t.b);
            const QPointF at(target.left() + (pos[0] + 0.5) / 256.0 * target.width(),
                             target.bottom() - (pos[1] + 0.5) / 256.0 * target.height());
            p.drawRect(QRectF(at.x() - 4, at.y() - 4, 8, 8));
            p.drawText(at + QPointF(6, -6), t.name);
        }
    }
}

ScopesPanel::ScopesPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* top = new QHBoxLayout;
    modeBox_ = new QComboBox(this);
    modeBox_->addItems({tr("Waveform"), tr("RGB Parade"), tr("Vectorscope"), tr("Histogram")});
    modeBox_->setAccessibleName(tr("Scope"));
    top->addWidget(modeBox_);
    stats_ = new QLabel(this);
    top->addWidget(stats_, 1);
    layout->addLayout(top);
    view_ = new ScopeView(this);
    layout->addWidget(view_, 1);
    connect(modeBox_, &QComboBox::currentIndexChanged, this, [this] {
        view_->setImage(renderScopeImage(scopes_, mode()), mode());
    });
    timer_ = new QTimer(this);
    timer_->setInterval(125);
    connect(timer_, &QTimer::timeout, this, &ScopesPanel::poll);
    timer_->start();
}

void ScopesPanel::setViewer(const ViewerPanel* viewer) {
    viewer_ = viewer;
    lastKey_ = -1;
}

void ScopesPanel::setMode(ScopeMode mode) { modeBox_->setCurrentIndex(static_cast<int>(mode)); }

ScopeMode ScopesPanel::mode() const { return static_cast<ScopeMode>(std::max(0, modeBox_->currentIndex())); }

const QImage& ScopesPanel::scopeImage() const { return view_->image(); }

void ScopesPanel::poll() {
    if (!isVisible() || !viewer_) return;
    if (viewer_->currentImage().cacheKey() == lastKey_) return;
    updateNow();
}

void ScopesPanel::updateNow() {
    if (!viewer_) return;
    const QImage& image = viewer_->currentImage();
    lastKey_ = image.cacheKey();
    const VideoFrame frame = image.isNull() ? VideoFrame{} : toFrame(image);
    scopes_ = render::computeScopes(frame, std::clamp(frame.width, 1, 256));
    view_->setImage(renderScopeImage(scopes_, mode()), mode());
    if (frame.empty()) {
        stats_->setText(tr("No picture"));
        return;
    }
    stats_->setText(tr("R %1–%2  G %3–%4  B %5–%6  mean luma %7%")
                        .arg(scopes_.minimum[0]).arg(scopes_.maximum[0])
                        .arg(scopes_.minimum[1]).arg(scopes_.maximum[1])
                        .arg(scopes_.minimum[2]).arg(scopes_.maximum[2])
                        .arg(scopes_.meanLuma * 100.0, 0, 'f', 1));
}

}  // namespace up::ui
