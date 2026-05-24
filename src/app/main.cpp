#include "app/application_paths.hpp"
#include "app/instance_controller.hpp"
#include "app/runtime_config_applier.hpp"
#include "app/window_manager.hpp"
#include "clash/clash_api_client.hpp"
#include "clash/mode_controller.hpp"
#include "config/config_file_service.hpp"
#include "config/config_loader.hpp"
#include "diagnostics/diagnostics_service.hpp"
#include "logging/logging_service.hpp"
#include "rules/ruleset_service.hpp"
#include "theme/theme_loader.hpp"
#include "ui/main_window.hpp"
#include "ui/tray_controller.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("tunlet");
    app.setOrganizationName("tunlet");
    app.setWindowIcon(QIcon(":/icons/tunlet.svg"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Control sing-box Clash API modes and edit rule-set files");
    parser.addHelpOption();
    QCommandLineOption configOption({"c", "config"}, "Path to tunlet config.yaml", "path");
    parser.addOption(configOption);
    parser.process(app);

    const QString configPath = parser.isSet(configOption)
                                   ? parser.value(configOption)
                                   : tunlet::app::defaultConfigPath();

    tunlet::config::AppConfig config;
    try {
        config = tunlet::config::ConfigLoader::loadFromPath(configPath);
    } catch (const std::exception &ex) {
        QMessageBox::critical(nullptr, "tunlet config error", ex.what());
        return 1;
    }

    tunlet::logging::LoggingService loggingService(config);
    loggingService.logInfo("app.bootstrap",
                           "Loaded tunlet configuration",
                           {},
                           {{"config_path", config.configPath}});

    tunlet::app::InstanceController instanceController(&loggingService, &app);
    QString instanceError;
    const auto instanceResult = instanceController.acquireOrForward(config.configPath, &instanceError);
    if (instanceResult == tunlet::app::InstanceController::AcquireResult::ForwardedToPrimary) {
        return 0;
    }
    if (instanceResult == tunlet::app::InstanceController::AcquireResult::Failed) {
        const QString error = QString("Failed to initialize single-instance server: %1").arg(instanceError);
        loggingService.logError("app.instance", "Failed to initialize single-instance server", instanceError);
        QMessageBox::critical(nullptr, "tunlet instance error", error);
        return 1;
    }

    QString qssError;
    if (!tunlet::theme::ThemeLoader::applyTheme(app, config.theme, &qssError) &&
        !qssError.isEmpty()) {
        loggingService.logWarning("app.bootstrap",
                                  "Failed to apply theme configuration",
                                  qssError,
                                  {{"theme_path", config.theme.themePath},
                                   {"template_path", config.theme.templatePath},
                                   {"qss_path", config.theme.qssPath}});
        QMessageBox::warning(nullptr, "tunlet theme warning", qssError);
    }

    tunlet::clash::ClashApiClient clashClient(config.diagnostics.requestTimeoutMs);
    tunlet::clash::ModeController modeController(config, &clashClient, &loggingService);
    tunlet::config::ConfigFileService configFileService(config.editing);
    tunlet::rules::RuleSetService ruleSetService(config.editing);
    tunlet::diagnostics::DiagnosticsService diagnosticsService(config, &clashClient, &loggingService);
    tunlet::ui::TrayController trayController(&loggingService);
    tunlet::app::RuntimeConfigApplier runtimeConfigApplier(
        &app,
        &clashClient,
        &modeController,
        &configFileService,
        &ruleSetService,
        &diagnosticsService,
        &loggingService,
        &trayController,
        &app);
    tunlet::app::WindowManager windowManager(
        config,
        &modeController,
        &configFileService,
        &diagnosticsService,
        &ruleSetService,
        &runtimeConfigApplier,
        &loggingService,
        trayController.isTrayAvailable(),
        &app);
    const bool keepRunningInTray = trayController.isTrayAvailable() && config.tray.keepRunningWithoutWindow;
    const bool startHiddenInTray = trayController.isTrayAvailable() && config.tray.startHidden;

    app.setQuitOnLastWindowClosed(!keepRunningInTray);

    trayController.setup(modeController.status(), modeController.profiles(), diagnosticsService.snapshot(), config.tray);
    QObject::connect(&instanceController,
                     &tunlet::app::InstanceController::openWindowRequested,
                     &windowManager,
                     &tunlet::app::WindowManager::openWindowFromSecondaryInstance);
    for (int pendingOpenRequests = instanceController.takePendingOpenWindowRequests(); pendingOpenRequests > 0; --pendingOpenRequests) {
        windowManager.openWindowFromSecondaryInstance();
    }
    QObject::connect(&runtimeConfigApplier,
                     &tunlet::app::RuntimeConfigApplier::configApplied,
                     &windowManager,
                     &tunlet::app::WindowManager::applyConfig);
    QObject::connect(&trayController,
                     &tunlet::ui::TrayController::openMainWindowRequested,
                     &windowManager,
                     &tunlet::app::WindowManager::openWindowFromTray);
    QObject::connect(&trayController,
                     &tunlet::ui::TrayController::refreshRequested,
                     &modeController,
                     &tunlet::clash::ModeController::refreshStatusFromTray);
    QObject::connect(&trayController,
                     &tunlet::ui::TrayController::refreshRequested,
                     &diagnosticsService,
                     &tunlet::diagnostics::DiagnosticsService::refreshFromTray);
    QObject::connect(&trayController, &tunlet::ui::TrayController::switchRequested, &modeController, &tunlet::clash::ModeController::switchMode);
    QObject::connect(&trayController, &tunlet::ui::TrayController::quitRequested, &app, &QApplication::quit);
    QObject::connect(&modeController, &tunlet::clash::ModeController::statusUpdated, &trayController, &tunlet::ui::TrayController::updateStatus);
    QObject::connect(&modeController, &tunlet::clash::ModeController::statusUpdated, &diagnosticsService, &tunlet::diagnostics::DiagnosticsService::observeModeStatus);
    QObject::connect(&modeController, &tunlet::clash::ModeController::profilesUpdated, &trayController, &tunlet::ui::TrayController::updateProfiles);
    QObject::connect(&diagnosticsService,
                     &tunlet::diagnostics::DiagnosticsService::diagnosticsUpdated,
                     &trayController,
                     &tunlet::ui::TrayController::updateDiagnostics);

    trayController.show();
    if (!startHiddenInTray) {
        windowManager.openWindow(tunlet::app::WindowManager::OpenReason::InitialShow);
    }
    loggingService.logInfo("app.bootstrap",
                           "Initialized runtime services",
                           {},
                           {{"tray_available", trayController.isTrayAvailable() ? "true" : "false"},
                            {"start_hidden", startHiddenInTray ? "true" : "false"}});
    modeController.refreshStatusForStartup();
    diagnosticsService.start();

    return app.exec();
}
