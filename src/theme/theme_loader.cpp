#include "theme/theme_loader.hpp"

#include <QApplication>
#include <QFile>

namespace tunlet::theme {

bool ThemeLoader::applyOptionalStylesheet(QApplication &application, const QString &path, QString *errorMessage) {
    QFile defaultFile(":/styles/default.qss");
    QString combinedStylesheet;
    if (defaultFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        combinedStylesheet += QString::fromUtf8(defaultFile.readAll());
    }

    if (!path.trimmed().isEmpty()) {
        QFile file(path);
        if (!file.exists()) {
            application.setStyleSheet(combinedStylesheet);
            if (errorMessage) {
                *errorMessage = QString("QSS file not found: %1").arg(path);
            }
            return false;
        }
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            application.setStyleSheet(combinedStylesheet);
            if (errorMessage) {
                *errorMessage = QString("failed to open QSS file: %1").arg(file.errorString());
            }
            return false;
        }

        if (!combinedStylesheet.isEmpty()) {
            combinedStylesheet += "\n";
        }
        combinedStylesheet += QString::fromUtf8(file.readAll());
    }

    application.setStyleSheet(combinedStylesheet);
    return true;
}

}  // namespace tunlet::theme
