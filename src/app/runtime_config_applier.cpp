#include "app/runtime_config_applier.hpp"

#include "clash/clash_api_client.hpp"
#include "clash/mode_controller.hpp"
#include "config/config_file_service.hpp"
#include "diagnostics/diagnostics_service.hpp"
#include "logging/logging_service.hpp"
#include "rules/ruleset_service.hpp"
#include "theme/theme_loader.hpp"
#include "ui/tray_controller.hpp"

#include <QApplication>

namespace tunlet::app {

RuntimeConfigApplier::RuntimeConfigApplier(QApplication *application,
                                           clash::ClashApiClient *clashClient,
                                           clash::ModeController *modeController,
                                           config::ConfigFileService *configFileService,
                                           rules::RuleSetService *ruleSetService,
                                           diagnostics::DiagnosticsService *diagnosticsService,
                                           logging::LoggingService *loggingService,
                                           ui::TrayController *trayController,
                                           QObject *parent)
    : QObject(parent),
      m_application(application),
      m_clashClient(clashClient),
      m_modeController(modeController),
      m_configFileService(configFileService),
      m_ruleSetService(ruleSetService),
      m_diagnosticsService(diagnosticsService),
      m_loggingService(loggingService),
      m_trayController(trayController) {}

RuntimeConfigApplyResult RuntimeConfigApplier::apply(const config::AppConfig &config) {
    RuntimeConfigApplyResult result;

    if (m_loggingService) {
        m_loggingService->updateConfig(config);
    }

    if (m_application && m_trayController) {
        const bool keepRunningInTray = m_trayController->isTrayAvailable() && config.tray.keepRunningWithoutWindow;
        m_application->setQuitOnLastWindowClosed(!keepRunningInTray);
        m_trayController->updateConfig(config.tray);
    }

    QString qssError;
    if (m_application &&
        !theme::ThemeLoader::applyTheme(*m_application, config.theme, &qssError) &&
        !qssError.isEmpty()) {
        result.warning = qssError;
        if (m_loggingService) {
            m_loggingService->logWarning("config.apply",
                                         "Runtime config applied with theme warning",
                                         qssError,
                                         {{"theme_path", config.theme.themePath},
                                          {"template_path", config.theme.templatePath},
                                          {"qss_path", config.theme.qssPath}});
        }
    }

    if (m_clashClient) {
        m_clashClient->setTimeoutMs(config.diagnostics.requestTimeoutMs);
    }
    if (m_configFileService) {
        m_configFileService->updateEditingConfig(config.editing);
    }
    if (m_ruleSetService) {
        m_ruleSetService->updateEditingConfig(config.editing);
    }
    if (m_modeController) {
        m_modeController->updateConfig(config);
        m_modeController->refreshStatusFromConfigApply();
    }
    if (m_diagnosticsService) {
        m_diagnosticsService->updateConfig(config);
        m_diagnosticsService->refreshFromConfigApply();
    }

    if (m_loggingService && result.warning.trimmed().isEmpty()) {
        m_loggingService->logInfo("config.apply",
                                  "Applied runtime configuration",
                                  {},
                                  {{"config_path", config.configPath}});
    }

    emit configApplied(config);

    return result;
}

}  // namespace tunlet::app
