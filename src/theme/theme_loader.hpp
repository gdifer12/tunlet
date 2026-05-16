#pragma once

#include <QString>

class QApplication;

namespace tunlet::theme {

class ThemeLoader {
public:
    static bool applyOptionalStylesheet(QApplication &application, const QString &path, QString *errorMessage = nullptr);
};

}  // namespace tunlet::theme
