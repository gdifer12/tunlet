#include "app/window_manager.hpp"

#include "app/runtime_config_applier.hpp"
#include "config/config_file_service.hpp"
#include "diagnostics/diagnostics_service.hpp"
#include "logging/logging_service.hpp"
#include "rules/ruleset_service.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QWidget>

namespace tunlet::app {

namespace {

QString backendName(bool hyprland) {
    if (hyprland) {
        return "hyprland";
    }
    return "portable";
}

}  // namespace

WindowManager::WindowManager(const config::AppConfig &config,
                             clash::ModeController *modeController,
                             config::ConfigFileService *configFileService,
                             diagnostics::DiagnosticsService *diagnosticsService,
                             rules::RuleSetService *ruleSetService,
                             RuntimeConfigApplier *runtimeConfigApplier,
                             logging::LoggingService *loggingService,
                             bool trayAvailable,
                             QObject *parent)
    : QObject(parent),
      m_config(config),
      m_modeController(modeController),
      m_configFileService(configFileService),
      m_diagnosticsService(diagnosticsService),
      m_ruleSetService(ruleSetService),
      m_runtimeConfigApplier(runtimeConfigApplier),
      m_loggingService(loggingService),
      m_trayAvailable(trayAvailable) {}

ui::MainWindow *WindowManager::openWindow(OpenReason reason) {
    pruneClosedWindows();

    QString detail;
    ui::MainWindow *target = nullptr;
    const Resolution resolution = resolveBackend();
    if (resolution.backend == ActivationBackend::Hyprland) {
        target = firstWindowOnCurrentHyprlandWorkspace(&detail);
        if (!target && detail.startsWith("hyprland_fallback=")) {
            target = activeManagedWindow();
        }
    } else {
        target = activeManagedWindow();
    }

    if (target) {
        target->showAndRaise();
        logOpenDecision(reason,
                        "Focused existing tunlet window",
                        detail.isEmpty()
                            ? QString("backend=%1").arg(backendName(resolution.backend == ActivationBackend::Hyprland))
                            : QString("backend=%1 %2")
                                  .arg(backendName(resolution.backend == ActivationBackend::Hyprland), detail));
        return target;
    }

    target = firstHiddenWindow();
    if (target) {
        target->showAndRaise();
        logOpenDecision(reason,
                        "Reused hidden tunlet window",
                        QString("backend=%1").arg(backendName(resolution.backend == ActivationBackend::Hyprland)));
        return target;
    }

    target = createWindow();
    target->showAndRaise();
    logOpenDecision(reason,
                    "Opened new tunlet window",
                    detail.isEmpty()
                        ? QString("backend=%1").arg(backendName(resolution.backend == ActivationBackend::Hyprland))
                        : QString("backend=%1 %2")
                              .arg(backendName(resolution.backend == ActivationBackend::Hyprland), detail));
    return target;
}

bool WindowManager::shouldHideWindowOnClose(const ui::MainWindow *window) const {
    Q_UNUSED(window);
    if (!(m_trayAvailable && m_config.tray.keepRunningWithoutWindow)) {
        return false;
    }

    pruneClosedWindows();
    int visibleWindowCount = 0;
    for (const auto &candidate : m_windows) {
        if (candidate && candidate->isVisible()) {
            ++visibleWindowCount;
        }
    }
    return visibleWindowCount <= 1;
}

void WindowManager::applyConfig(const config::AppConfig &config) {
    m_config = config;
    pruneClosedWindows();
    for (const auto &window : m_windows) {
        if (window) {
            window->applyRuntimeConfig(config);
        }
    }
}

void WindowManager::openWindowFromTray() {
    openWindow(OpenReason::Tray);
}

void WindowManager::openWindowFromSecondaryInstance() {
    openWindow(OpenReason::SecondaryInstance);
}

ui::MainWindow *WindowManager::createWindow() {
    auto *window = new ui::MainWindow(
        m_config,
        m_modeController,
        m_configFileService,
        m_diagnosticsService,
        m_ruleSetService,
        m_runtimeConfigApplier,
        m_loggingService,
        m_trayAvailable,
        m_nextWindowInstanceId++);
    connect(window, &QObject::destroyed, this, [this, window]() {
        m_windows.removeAll(window);
    });
    m_windows.push_back(window);
    return window;
}

void WindowManager::pruneClosedWindows() const {
    for (qsizetype index = m_windows.size() - 1; index >= 0; --index) {
        if (!m_windows.at(index)) {
            m_windows.removeAt(index);
        }
    }
}

ui::MainWindow *WindowManager::activeManagedWindow() const {
    QWidget *activeWidget = QApplication::activeWindow();
    if (!activeWidget) {
        return nullptr;
    }

    QWidget *topLevel = activeWidget->window();
    for (const auto &window : m_windows) {
        if (window && window.data() == topLevel) {
            return window;
        }
    }
    return nullptr;
}

ui::MainWindow *WindowManager::firstHiddenWindow() const {
    for (const auto &window : m_windows) {
        if (window && !window->isVisible()) {
            return window;
        }
    }
    return nullptr;
}

ui::MainWindow *WindowManager::firstWindowOnCurrentHyprlandWorkspace(QString *detail) const {
    const HyprlandWindowLocator::Snapshot snapshot = m_hyprlandWindowLocator.snapshotForCurrentProcess();
    if (!snapshot.ok) {
        if (detail) {
            *detail = QString("hyprland_fallback=%1").arg(snapshot.error);
        }
        return nullptr;
    }

    for (const auto &title : snapshot.currentWorkspaceWindowTitles) {
        for (const auto &window : m_windows) {
            if (window && window->windowTitle() == title) {
                if (detail) {
                    *detail = QString("workspace_id=%1").arg(snapshot.activeWorkspaceId);
                }
                return window;
            }
        }
    }

    if (detail) {
        *detail = QString("workspace_id=%1 no_window_on_workspace").arg(snapshot.activeWorkspaceId);
    }
    return nullptr;
}

WindowManager::Resolution WindowManager::resolveBackend() const {
    Resolution resolution;
    switch (m_config.ui.windowActivation.mode) {
    case config::UiWindowActivationMode::Portable:
        resolution.backend = ActivationBackend::Portable;
        resolution.detail = "portable_requested";
        return resolution;
    case config::UiWindowActivationMode::Hyprland:
        if (m_hyprlandWindowLocator.isAvailable()) {
            resolution.backend = ActivationBackend::Hyprland;
            resolution.detail = "hyprland_requested";
        } else {
            resolution.backend = ActivationBackend::Portable;
            resolution.detail = "hyprland_unavailable_fallback";
        }
        return resolution;
    case config::UiWindowActivationMode::Auto:
    default:
        if (m_hyprlandWindowLocator.isAvailable()) {
            resolution.backend = ActivationBackend::Hyprland;
            resolution.detail = "auto_hyprland";
        } else {
            resolution.backend = ActivationBackend::Portable;
            resolution.detail = "auto_portable";
        }
        return resolution;
    }
}

void WindowManager::logOpenDecision(OpenReason reason, const QString &action, const QString &detail) const {
    if (!m_loggingService) {
        return;
    }

    logging::LogContext context{{"reason", openReasonName(reason)}};
    const Resolution resolution = resolveBackend();
    context.insert("backend", backendName(resolution.backend == ActivationBackend::Hyprland));
    if (!resolution.detail.isEmpty()) {
        context.insert("backend_resolution", resolution.detail);
    }
    m_loggingService->logInfo("window.open", action, detail, context);
}

QString WindowManager::openReasonName(OpenReason reason) const {
    switch (reason) {
    case OpenReason::Tray:
        return "tray";
    case OpenReason::SecondaryInstance:
        return "secondary_instance";
    case OpenReason::InitialShow:
    default:
        return "initial_show";
    }
}

}  // namespace tunlet::app
