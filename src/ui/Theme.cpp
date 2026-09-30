#include "ui/Theme.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPalette>
#include <QWidget>
#include <algorithm>

namespace up::ui {
namespace {

DesignTokens g_tokens = tokensFor(ThemeKind::Dark);
ThemeKind g_kind = ThemeKind::Dark;

}  // namespace

DesignTokens tokensFor(ThemeKind kind) {
    switch (kind) {
        case ThemeKind::Light:
            return {"Light",
                    QColor("#eceef1"), QColor("#f7f8fa"), QColor("#e2e5ea"), QColor("#c5cad3"),
                    QColor("#1d2128"), QColor("#5c6470"), QColor("#2f6fde"), QColor("#ffffff"),
                    QColor("#5b8def"), QColor("#3fae7c"), QColor("#0d1117"), QColor("#f0b429"),
                    QColor("#d6334a"), QColor("#b3261e"), QColor("#d4d7dd"), QColor("#2f6fde"), QColor("#b3261e")};
        case ThemeKind::HighContrast:
            return {"High Contrast",
                    QColor("#000000"), QColor("#000000"), QColor("#1a1a1a"), QColor("#ffffff"),
                    QColor("#ffffff"), QColor("#ffff00"), QColor("#00ffff"), QColor("#000000"),
                    QColor("#0050ff"), QColor("#00a000"), QColor("#ffffff"), QColor("#ffff00"),
                    QColor("#ff00ff"), QColor("#ff0000"), QColor("#404040"), QColor("#00ffff"), QColor("#ff0000")};
        case ThemeKind::Dark:
        default:
            return {"Dark",
                    QColor("#16181c"), QColor("#1e2127"), QColor("#262a31"), QColor("#343a44"),
                    QColor("#e3e6eb"), QColor("#8b93a1"), QColor("#4c8dff"), QColor("#ffffff"),
                    QColor("#3d6fc2"), QColor("#2f8f65"), QColor("#f5f7fa"), QColor("#f0b429"),
                    QColor("#ff4d5e"), QColor("#8c1428"), QColor("#2b2f36"), QColor("#4c8dff"), QColor("#ff6b6b")};
    }
}

const DesignTokens& currentTokens() { return g_tokens; }

QColor markerColor(int index) {
    // red, orange, yellow, green, blue, purple (matches up::MarkerColor)
    static const QColor colors[] = {QColor("#e5484d"), QColor("#f76b15"), QColor("#ffc53d"),
                                    QColor("#30a46c"), QColor("#3e8ef7"), QColor("#8e4ec6")};
    return colors[std::clamp(index, 0, 5)];
}
ThemeKind currentThemeKind() { return g_kind; }

Metrics metricsFor(const QWidget* widget) {
    const QFontMetrics fm = widget ? widget->fontMetrics() : QFontMetrics(QApplication::font());
    const int h = fm.height();
    return Metrics{h * 7, h * 2, h * 3 + h / 2, std::max(4, h / 2), std::max(6, h / 2)};
}

void applyTheme(QApplication& app, ThemeKind kind) {
    g_kind = kind;
    g_tokens = tokensFor(kind);
    const DesignTokens& t = g_tokens;
    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.panel);
    p.setColor(QPalette::AlternateBase, t.panelAlt);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::PlaceholderText, t.textMuted);
    p.setColor(QPalette::Button, t.panelAlt);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, t.accentText);
    p.setColor(QPalette::ToolTipBase, t.panelAlt);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::Mid, t.border);
    p.setColor(QPalette::Disabled, QPalette::Text, t.textMuted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, t.textMuted);
    app.setPalette(p);
    app.setStyleSheet(QString("QDockWidget::title { background: %1; padding: 4px; }"
                              "QMainWindow::separator { background: %2; width: 3px; height: 3px; }"
                              "QToolButton:checked { background: %3; color: %4; }"
                              "QWidget:focus { outline: none; }")
                          .arg(t.panelAlt.name(), t.border.name(), t.accent.name(), t.accentText.name()));
    for (QWidget* w : QApplication::allWidgets()) w->update();
}

}  // namespace up::ui
