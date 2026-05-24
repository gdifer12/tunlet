#pragma once

#include "app/hyprland_window_locator.hpp"
#include "config/app_config.hpp"

#include <QObject>
#include <QPointer>
#include <QVector>

class QWidget;

namespace tunlet::app {

class RuntimeConfigApplier;
}

namespace tunlet::clash {
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
class MainWindow;
}

namespace tunlet::app {

class WindowManager : public QObject {
    Q_OBJECT

public:
    enum class OpenReason {
        InitialShow,
        Tray,
        SecondaryInstance,
    };

    WindowManager(const config::AppConfig &config,
                  clash::ModeController *modeController,
                  config::ConfigFileService *configFileService,
                  diagnostics::DiagnosticsService *diagnosticsService,
                  rules::RuleSetService *ruleSetService,
                  RuntimeConfigApplier *runtimeConfigApplier,
                  logging::LoggingService *loggingService,
                  bool trayAvailable,
                  QObject *parent = nullptr);

    ui::MainWindow *openWindow(OpenReason reason);
    bool shouldHideWindowOnClose(const ui::MainWindow *window) const;
    void applyConfig(const config::AppConfig &config);

public slots:
    void openWindowFromTray();
    void openWindowFromSecondaryInstance();

private:
    enum class ActivationBackend {
        Portable,
        Hyprland,
    };

    struct Resolution {
        ActivationBackend backend = ActivationBackend::Portable;
        QString detail;
    };

    struct HyprlandWindowMatch {
        QPointer<ui::MainWindow> window;
        QString address;
        QString detail;
    };

    ui::MainWindow *createWindow();
    void pruneClosedWindows() const;
    ui::MainWindow *activeManagedWindow() const;
    ui::MainWindow *firstHiddenWindow() const;
    ui::MainWindow *firstWindowOnCurrentHyprlandWorkspace(QString *detail) const;
    HyprlandWindowMatch firstHyprlandWindowOnCurrentWorkspace() const;
    bool tryFocusWindowViaHyprland(const HyprlandWindowMatch &match, OpenReason reason, const QString &selectionDetail) const;
    void scheduleHyprlandFocusFollowup(ui::MainWindow *window, OpenReason reason, int remainingAttempts = 8) const;
    Resolution resolveBackend() const;
    void logOpenDecision(OpenReason reason, const QString &action, const QString &detail = {}) const;
    QString openReasonName(OpenReason reason) const;

    config::AppConfig m_config;
    clash::ModeController *m_modeController = nullptr;
    config::ConfigFileService *m_configFileService = nullptr;
    diagnostics::DiagnosticsService *m_diagnosticsService = nullptr;
    rules::RuleSetService *m_ruleSetService = nullptr;
    RuntimeConfigApplier *m_runtimeConfigApplier = nullptr;
    logging::LoggingService *m_loggingService = nullptr;
    bool m_trayAvailable = false;
    mutable QVector<QPointer<ui::MainWindow>> m_windows;
    mutable HyprlandWindowLocator m_hyprlandWindowLocator;
    int m_nextWindowInstanceId = 1;
};

}  // namespace tunlet::app
