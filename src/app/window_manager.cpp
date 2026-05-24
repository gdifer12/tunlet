#include "app/window_manager.hpp"

#include "app/runtime_config_applier.hpp"
#include "config/config_file_service.hpp"
#include "diagnostics/diagnostics_service.hpp"
#include "logging/logging_service.hpp"
#include "rules/ruleset_service.hpp"
#include "ui/main_window.hpp"

#include <QApplication>
#include <QTimer>
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
    HyprlandWindowMatch hyprlandMatch;
    const Resolution resolution = resolveBackend();
    if (resolution.backend == ActivationBackend::Hyprland) {
        hyprlandMatch = firstHyprlandWindowOnCurrentWorkspace();
        target = hyprlandMatch.window;
        detail = hyprlandMatch.detail;
        if (!target && detail.startsWith("hyprland_fallback=")) {
            target = activeManagedWindow();
        }
    } else {
        target = activeManagedWindow();
    }

    if (target) {
        target->showAndRaise();
        if (reason == OpenReason::SecondaryInstance && resolution.backend == ActivationBackend::Hyprland) {
            if (tryFocusWindowViaHyprland(hyprlandMatch, reason, detail)) {
                logOpenDecision(reason,
                                "Focused existing tunlet window",
                                detail.isEmpty()
                                    ? QString("backend=%1").arg(backendName(resolution.backend == ActivationBackend::Hyprland))
                                    : QString("backend=%1 %2")
                                          .arg(backendName(resolution.backend == ActivationBackend::Hyprland), detail));
                return target;
            }
        }
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
        if (reason == OpenReason::SecondaryInstance && resolution.backend == ActivationBackend::Hyprland) {
            scheduleHyprlandFocusFollowup(target, reason);
        }
        logOpenDecision(reason,
                        "Reused hidden tunlet window",
                        QString("backend=%1").arg(backendName(resolution.backend == ActivationBackend::Hyprland)));
        return target;
    }

    target = createWindow();
    target->showAndRaise();
    if (reason == OpenReason::SecondaryInstance && resolution.backend == ActivationBackend::Hyprland) {
        scheduleHyprlandFocusFollowup(target, reason);
    }
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
    const HyprlandWindowMatch match = firstHyprlandWindowOnCurrentWorkspace();
    if (detail) {
        *detail = match.detail;
    }
    return match.window;
}

WindowManager::HyprlandWindowMatch WindowManager::firstHyprlandWindowOnCurrentWorkspace() const {
    HyprlandWindowMatch match;
    const HyprlandWindowLocator::Snapshot snapshot = m_hyprlandWindowLocator.snapshotForCurrentProcess();
    if (!snapshot.ok) {
        match.detail = QString("hyprland_fallback=%1").arg(snapshot.error);
        return match;
    }

    for (const auto &client : snapshot.currentWorkspaceClients) {
        for (const auto &window : m_windows) {
            if (window && window->windowTitle() == client.title) {
                match.window = window;
                match.address = client.address;
                match.detail = QString("workspace_id=%1 address=%2").arg(snapshot.activeWorkspaceId).arg(client.address);
                return match;
            }
        }
    }

    match.detail = QString("workspace_id=%1 no_window_on_workspace").arg(snapshot.activeWorkspaceId);
    return match;
}

bool WindowManager::tryFocusWindowViaHyprland(const HyprlandWindowMatch &match,
                                              OpenReason reason,
                                              const QString &selectionDetail) const {
    if (!match.window || match.address.trimmed().isEmpty()) {
        if (m_loggingService && reason == OpenReason::SecondaryInstance) {
            m_loggingService->logWarning("window.focus",
                                         "Skipped Hyprland focus dispatch for secondary-instance open",
                                         "Window match has no Hyprland address",
                                         {{"reason", openReasonName(reason)}, {"selection_detail", selectionDetail}});
        }
        return false;
    }

    QString error;
    if (!m_hyprlandWindowLocator.focusWindowByAddress(match.address, &error)) {
        if (m_loggingService) {
            m_loggingService->logWarning("window.focus",
                                         "Hyprland focus dispatch failed",
                                         error,
                                         {{"reason", openReasonName(reason)},
                                          {"address", match.address},
                                          {"selection_detail", selectionDetail}});
        }
        return false;
    }

    if (m_loggingService) {
        m_loggingService->logInfo("window.focus",
                                  "Focused tunlet window through Hyprland IPC",
                                  {},
                                  {{"reason", openReasonName(reason)},
                                   {"address", match.address},
                                   {"selection_detail", selectionDetail}});
    }
    return true;
}

void WindowManager::scheduleHyprlandFocusFollowup(ui::MainWindow *window, OpenReason reason, int remainingAttempts) const {
    if (!window || remainingAttempts <= 0) {
        if (window && m_loggingService) {
            m_loggingService->logWarning("window.focus",
                                         "Timed out waiting for Hyprland window mapping",
                                         {},
                                         {{"reason", openReasonName(reason)},
                                          {"window_title", window->windowTitle()}});
        }
        return;
    }

    QPointer<ui::MainWindow> guardedWindow(window);
    QTimer::singleShot(60, this, [this, guardedWindow, reason, remainingAttempts]() {
        if (!guardedWindow) {
            return;
        }

        HyprlandWindowMatch match;
        const HyprlandWindowLocator::Snapshot snapshot = m_hyprlandWindowLocator.snapshotForCurrentProcess();
        if (snapshot.ok) {
            for (const auto &client : snapshot.currentWorkspaceClients) {
                if (client.title == guardedWindow->windowTitle()) {
                    match.window = guardedWindow;
                    match.address = client.address;
                    match.detail = QString("workspace_id=%1 address=%2").arg(snapshot.activeWorkspaceId).arg(client.address);
                    break;
                }
            }
        }

        if (!match.address.trimmed().isEmpty() && tryFocusWindowViaHyprland(match, reason, match.detail)) {
            return;
        }

        scheduleHyprlandFocusFollowup(guardedWindow, reason, remainingAttempts - 1);
    });
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
