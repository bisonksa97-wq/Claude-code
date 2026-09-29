#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QWidget;

namespace up::ui {

// Centralised design tokens. Widgets read colours and metrics from here instead
// of hard-coding them, so themes (dark / light / high contrast) and UI scaling
// apply everywhere.
struct DesignTokens {
    QString name;
    QColor window;
    QColor panel;
    QColor panelAlt;
    QColor border;
    QColor text;
    QColor textMuted;
    QColor accent;
    QColor accentText;
    QColor videoClip;
    QColor audioClip;
    QColor clipText;
    QColor selection;
    QColor playhead;
    QColor offline;
    QColor trackLocked;
    QColor toggleOn;
    QColor warning;  // text/icons for offline media and errors
};

// Layout metrics derived from the font size so the UI scales with it.
struct Metrics {
    int trackHeaderWidth;
    int rulerHeight;
    int trackHeight;
    int trimHandle;
    int snapDistance;
};

enum class ThemeKind { Dark, Light, HighContrast };

DesignTokens tokensFor(ThemeKind kind);
const DesignTokens& currentTokens();
Metrics metricsFor(const QWidget* widget);

void applyTheme(QApplication& app, ThemeKind kind);
ThemeKind currentThemeKind();

}  // namespace up::ui
