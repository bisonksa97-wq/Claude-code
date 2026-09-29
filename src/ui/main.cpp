#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include "core/Log.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Ultimate Post");
    QApplication::setOrganizationName("UltimatePost");
    QApplication::setApplicationVersion("0.1.0");

    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
    QDir().mkpath(logDir);
    up::log::openLogFile((logDir + "/ultimatepost.log").toStdString());
    UP_LOG_INFO(up::log::sub::App, "Ultimate Post starting");

    const int theme = QSettings().value("ui/theme", static_cast<int>(up::ui::ThemeKind::Dark)).toInt();
    up::ui::applyTheme(app, static_cast<up::ui::ThemeKind>(theme));

    up::ui::MainWindow window;
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) window.openProjectFile(args.at(1));
    window.show();
    return QApplication::exec();
}
