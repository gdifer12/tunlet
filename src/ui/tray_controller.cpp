#include "ui/tray_controller.hpp"

#include "logging/logging_service.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QScreen>
#include <QSignalBlocker>
#include <QSystemTrayIcon>

namespace tunlet::ui {

namespace {

QString currentModeLabel(const clash::ModeStatus &status) {
    if (!status.currentProfileName.isEmpty() && status.currentProfileName != "unknown") {
        return status.currentProfileName;
    }
    return status.currentModeValue.isEmpty() ? QString("unknown") : status.currentModeValue;
}

QString displayModeName(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) {
        return "Unknown";
    }
    value.replace('-', ' ');
    value.replace('_', ' ');
    value[0] = value.front().toUpper();
    return value;
}

QString apiStatusSummary(const clash::ModeStatus &status) {
    return status.reachable ? "Reachable" : "Unavailable";
}

QString apiStatusTooltip(const clash::ModeStatus &status) {
    return status.reachable ? "API reachable" : "API unavailable";
}

QString ipSummaryLabel(const diagnostics::DiagnosticsSnapshot &snapshot) {
    const QString publicIp = snapshot.publicIp.trimmed();
    if (publicIp.isEmpty()) {
        return "Unavailable";
    }

    const QString countryCode = snapshot.locationCountryCode.trimmed().toUpper();
    return countryCode.isEmpty() ? publicIp : QString("%1 | %2").arg(publicIp, countryCode);
}

QString delaySummaryLabel(const diagnostics::DiagnosticsSnapshot &snapshot) {
    if (snapshot.delayTotalMs < 0) {
        return "Unavailable";
    }
    return QString("%1 ms").arg(snapshot.delayTotalMs);
}

QString tooltipIpLabel(const diagnostics::DiagnosticsSnapshot &snapshot) {
    const QString publicIp = snapshot.publicIp.trimmed();
    if (publicIp.isEmpty()) {
        return "Unavailable";
    }

    const QString countryCode = snapshot.locationCountryCode.trimmed().toUpper();
    return countryCode.isEmpty() ? publicIp : QString("%1 (%2)").arg(publicIp, countryCode);
}

QAction *addStaticAction(QMenu *menu, const QString &text) {
    QAction *action = menu->addAction(text);
    action->setEnabled(false);
    return action;
}

}  // namespace

TrayController::TrayController(logging::LoggingService *loggingService, QObject *parent)
    : QObject(parent), m_logger(loggingService), m_trayIcon(new QSystemTrayIcon(QIcon(":/icons/tunlet.svg"), this)) {
    m_menu = new QMenu();
    m_menu->setObjectName("trayMenu");
    m_menu->setSeparatorsCollapsible(false);
    m_menu->setToolTipsVisible(true);
    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &TrayController::handleTrayActivation);
    connect(m_menu, &QMenu::aboutToShow, this, &TrayController::handleMenuAboutToShow);

    rebuildMenu();
    updateToolTip();
}

TrayController::~TrayController() {
    if (m_menu) {
        m_menu->hide();
        delete m_menu;
        m_menu = nullptr;
    }
}

void TrayController::setup(const clash::ModeStatus &status,
                           const QVector<config::ClashModeProfile> &profiles,
                           const diagnostics::DiagnosticsSnapshot &diagnostics,
                           const config::TrayConfig &config) {
    m_status = status;
    m_visibleStatus = status;
    m_profiles = profiles;
    m_diagnostics = diagnostics;
    m_visibleDiagnostics = diagnostics;
    m_config = config;
    m_refreshRequestPending = false;
    m_switchRequestPending = false;
    m_lastMenuAnchor = QPoint();
    rebuildMenu();
    refreshMenuPresentation();
}

void TrayController::show() {
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon->show();
    }
}

bool TrayController::isTrayAvailable() const {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::updateStatus(const clash::ModeStatus &status) {
    m_status = status;
    syncVisibleStatus();
    reconcilePendingState();
    refreshMenuPresentation();
}

void TrayController::updateProfiles(const QVector<config::ClashModeProfile> &profiles) {
    m_profiles = profiles;
    rebuildMenu();
    refreshMenuPresentation();
}

void TrayController::updateDiagnostics(const diagnostics::DiagnosticsSnapshot &snapshot) {
    m_diagnostics = snapshot;
    syncVisibleDiagnostics();
    reconcilePendingState();
    refreshMenuPresentation();
}

void TrayController::updateConfig(const config::TrayConfig &config) {
    m_config = config;
}

void TrayController::handleTrayActivation(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        emit openMainWindowRequested();
        return;
    }

    if (reason == QSystemTrayIcon::Context) {
        m_lastMenuAnchor = menuPopupPosition();
    }
}

void TrayController::handleMenuAboutToShow() {
    m_lastMenuAnchor = menuPopupPosition();
    refreshMenuPresentation();
    requestMenuRefresh(MenuRefreshOrigin::MenuOpen);
}

QPoint TrayController::menuPopupPosition() const {
    const QRect trayGeometry = m_trayIcon->geometry();
    const QPoint anchor = trayGeometry.isValid() ? trayGeometry.center() : QCursor::pos();
    QScreen *screen = trayGeometry.isValid() ? QGuiApplication::screenAt(trayGeometry.center())
                                             : QGuiApplication::screenAt(anchor);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return anchor;
    }

    const QRect available = screen->availableGeometry();
    const QSize menuSize = m_menu ? m_menu->sizeHint() : QSize(280, 240);

    int x = anchor.x() - menuSize.width() / 2;
    x = qBound(available.left() + 8, x, available.right() - menuSize.width() - 8);

    int y = anchor.y() + 8;
    const bool preferAbove = trayGeometry.isValid() && trayGeometry.center().y() > available.center().y();
    if (preferAbove) {
        y = trayGeometry.top() - menuSize.height() - 8;
    } else if (trayGeometry.isValid()) {
        y = trayGeometry.bottom() + 8;
    }
    y = qBound(available.top() + 8, y, available.bottom() - menuSize.height() - 8);
    return QPoint(x, y);
}

void TrayController::syncVisibleStatus() {
    if (!m_status.busy) {
        m_visibleStatus = m_status;
    }
}

void TrayController::syncVisibleDiagnostics() {
    if (!m_diagnostics.runtimeRefreshInFlight) {
        m_visibleDiagnostics = m_diagnostics;
    }
}

void TrayController::reconcilePendingState() {
    if (m_refreshRequestPending && !m_status.busy && !m_diagnostics.runtimeRefreshInFlight) {
        m_refreshRequestPending = false;
    }

    if (m_switchRequestPending && !m_status.switchInFlight) {
        m_switchRequestPending = false;
    }
}

void TrayController::rebuildMenu() {
    if (!m_menu) {
        return;
    }

    if (m_modeActionGroup) {
        delete m_modeActionGroup;
        m_modeActionGroup = nullptr;
    }
    m_modeActions.clear();

    m_menu->clear();

    m_modeSummaryAction = addStaticAction(m_menu, "Mode: Unknown");
    m_apiSummaryAction = addStaticAction(m_menu, "API: Unavailable");
    m_menu->addSeparator();

    m_modeActionGroup = new QActionGroup(this);
    m_modeActionGroup->setExclusive(true);
    if (m_profiles.isEmpty()) {
        addStaticAction(m_menu, "No profiles configured");
    } else {
        for (const auto &profile : m_profiles) {
            QAction *action = m_menu->addAction(displayModeName(profile.name));
            action->setCheckable(true);
            const QString toolTip = profile.desc.isEmpty() ? QString("Backend mode: %1").arg(profile.mode) : profile.desc;
            action->setToolTip(toolTip);
            action->setStatusTip(toolTip);
            m_modeActionGroup->addAction(action);
            connect(action, &QAction::triggered, this, [this, profileName = profile.name]() { requestModeSwitch(profileName); });
            m_modeActions.insert(profile.name, action);
        }
    }
    m_menu->addSeparator();

    m_ipSummaryAction = addStaticAction(m_menu, "IP: Unavailable");
    m_delaySummaryAction = addStaticAction(m_menu, "Delay: Unavailable");
    m_menu->addSeparator();

    m_openAction = m_menu->addAction("Open tunlet");
    connect(m_openAction, &QAction::triggered, this, [this]() {
        emit openMainWindowRequested();
    });

    m_refreshAction = m_menu->addAction("Refresh");
    connect(m_refreshAction, &QAction::triggered, this, [this]() { requestMenuRefresh(MenuRefreshOrigin::MenuAction); });

    m_quitAction = m_menu->addAction("Quit");
    connect(m_quitAction, &QAction::triggered, this, [this]() {
        emit quitRequested();
    });
}

void TrayController::refreshMenuPresentation() {
    if (m_modeSummaryAction) {
        m_modeSummaryAction->setText(QString("Mode: %1").arg(displayModeName(currentModeLabel(m_visibleStatus))));
    }
    if (m_apiSummaryAction) {
        m_apiSummaryAction->setText(QString("API: %1").arg(apiStatusSummary(m_visibleStatus)));
    }
    if (m_ipSummaryAction) {
        m_ipSummaryAction->setText(QString("IP: %1").arg(ipSummaryLabel(m_visibleDiagnostics)));
    }
    if (m_delaySummaryAction) {
        m_delaySummaryAction->setText(QString("Delay: %1").arg(delaySummaryLabel(m_visibleDiagnostics)));
    }

    for (const auto &profile : m_profiles) {
        QAction *action = m_modeActions.value(profile.name);
        if (!action) {
            continue;
        }
        const QSignalBlocker blocker(action);
        action->setChecked(profile.name == m_visibleStatus.currentProfileName);
    }

    updateToolTip();
}

void TrayController::updateToolTip() {
    const QString modeLabel = displayModeName(currentModeLabel(m_visibleStatus));
    m_trayIcon->setToolTip(QString("tunlet\nMode: %1\nStatus: %2\nIP: %3\nDelay: %4")
                               .arg(modeLabel,
                                    apiStatusTooltip(m_visibleStatus),
                                    tooltipIpLabel(m_visibleDiagnostics),
                                    delaySummaryLabel(m_visibleDiagnostics)));
}

QString TrayController::menuRefreshOriginName(MenuRefreshOrigin origin) const {
    switch (origin) {
    case MenuRefreshOrigin::MenuOpen:
        return "menu_open";
    case MenuRefreshOrigin::MenuAction:
    default:
        return "menu_action";
    }
}

void TrayController::logTrayInfo(const QString &summary, const QString &detail, const logging::LogContext &context) const {
    if (m_logger) {
        m_logger->logInfo("tray", summary, detail, context);
    }
}

void TrayController::requestMenuRefresh(MenuRefreshOrigin origin) {
    if (m_refreshRequestPending || m_status.busy || m_diagnostics.runtimeRefreshInFlight) {
        QString blockedBy = "unknown";
        if (m_refreshRequestPending) {
            blockedBy = "refresh_pending";
        } else if (m_status.busy) {
            blockedBy = "mode_busy";
        } else if (m_diagnostics.runtimeRefreshInFlight) {
            blockedBy = "diagnostics_busy";
        }
        logTrayInfo("Skipped tray runtime refresh request",
                    {},
                    {{"origin", menuRefreshOriginName(origin)}, {"blocked_by", blockedBy}});
        return;
    }
    m_refreshRequestPending = true;
    logTrayInfo("Requested tray runtime refresh", {}, {{"origin", menuRefreshOriginName(origin)}});
    emit refreshRequested();
}

void TrayController::requestModeSwitch(const QString &profileName) {
    if (profileName.trimmed().isEmpty()) {
        return;
    }
    if (profileName == m_visibleStatus.currentProfileName) {
        return;
    }
    if (m_switchRequestPending || m_status.busy || m_status.switchInFlight) {
        logTrayInfo("Skipped tray mode switch request",
                    {},
                    {{"profile", profileName},
                     {"blocked_by", m_switchRequestPending ? "switch_pending"
                                                           : (m_status.busy ? "mode_busy" : "switch_in_flight")}});
        return;
    }
    m_switchRequestPending = true;
    logTrayInfo("Requested tray mode switch", {}, {{"profile", profileName}});
    emit switchRequested(profileName);
}

void TrayController::showContextMenu() {
    if (!m_menu) {
        return;
    }
    m_lastMenuAnchor = menuPopupPosition();
    m_menu->popup(m_lastMenuAnchor);
}

void TrayController::hideContextMenu() {
    if (!m_menu) {
        return;
    }
    m_menu->hide();
}

}  // namespace tunlet::ui
