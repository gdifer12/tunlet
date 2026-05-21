#pragma once

#include "config/app_config.hpp"

#include <QString>

class QApplication;

namespace tunlet::theme {

class ThemeLoader {
public:
    static bool buildStylesheet(const config::ThemeConfig &config,
                                QString *stylesheet,
                                QString *errorMessage = nullptr);
    static bool applyTheme(QApplication &application,
                           const config::ThemeConfig &config,
                           QString *errorMessage = nullptr);
};

}  // namespace tunlet::theme
