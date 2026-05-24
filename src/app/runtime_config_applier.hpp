#pragma once

#include "config/app_config.hpp"

#include <QObject>
#include <QString>

class QApplication;

namespace tunlet::clash {
class ClashApiClient;
class ModeController;
}

namespace tunlet::config {
class ConfigFileService;
}

namespace tunlet::diagnostics {
class DiagnosticsService;
}

namespace tunlet::logging {
class LoggingService;
}

namespace tunlet::rules {
class RuleSetService;
}

namespace tunlet::ui {
class TrayController;
}

namespace tunlet::app {

struct RuntimeConfigApplyResult {
    QString warning;
};

class RuntimeConfigApplier : public QObject {
    Q_OBJECT

public:
    RuntimeConfigApplier(QApplication *application,
                         clash::ClashApiClient *clashClient,
                         clash::ModeController *modeController,
                         config::ConfigFileService *configFileService,
                         rules::RuleSetService *ruleSetService,
                         diagnostics::DiagnosticsService *diagnosticsService,
                         logging::LoggingService *loggingService,
                         ui::TrayController *trayController,
                         QObject *parent = nullptr);

    RuntimeConfigApplyResult apply(const config::AppConfig &config);

signals:
    void configApplied(const config::AppConfig &config);

private:
    QApplication *m_application = nullptr;
    clash::ClashApiClient *m_clashClient = nullptr;
    clash::ModeController *m_modeController = nullptr;
    config::ConfigFileService *m_configFileService = nullptr;
    rules::RuleSetService *m_ruleSetService = nullptr;
    diagnostics::DiagnosticsService *m_diagnosticsService = nullptr;
    logging::LoggingService *m_loggingService = nullptr;
    ui::TrayController *m_trayController = nullptr;
};

}  // namespace tunlet::app
