#include "ui/tray_controller.hpp"

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
#include <QTimer>

namespace tunlet::ui {

namespace {

constexpr int kMenuSessionIdleExpiryMs = 1500;

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

TrayController::TrayController(QObject *parent) : QObject(parent), m_trayIcon(new QSystemTrayIcon(QIcon(":/icons/tunlet.svg"), this)) {
    m_menu = new QMenu();
    m_menu->setObjectName("trayMenu");
    m_menu->setSeparatorsCollapsible(false);
    m_menu->setToolTipsVisible(true);
    m_trayIcon->setContextMenu(m_menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &TrayController::handleTrayActivation);
    connect(m_menu, &QMenu::aboutToShow, this, &TrayController::handleMenuAboutToShow);
    connect(m_menu, &QMenu::aboutToHide, this, &TrayController::handleMenuAboutToHide);
    connect(m_menu, &QMenu::hovered, this, [this](QAction *) {
        if (m_interactiveSessionActive || m_reopenRequested) {
            scheduleSessionExpiryIfMenuDoesNotReopen();
        }
    });
    connect(m_menu, &QMenu::triggered, this, [this](QAction *) {
        if (m_interactiveSessionActive || m_reopenRequested) {
            scheduleSessionExpiryIfMenuDoesNotReopen();
        }
    });

    m_interactiveRefreshTimer.setSingleShot(false);
    m_interactiveRefreshTimer.setInterval(m_config.interactiveRefreshIntervalMs);
    connect(&m_interactiveRefreshTimer, &QTimer::timeout, this, &TrayController::emitMenuRefreshIfIdle);

    m_reopenExpiryTimer.setSingleShot(true);
    m_reopenExpiryTimer.setInterval(kMenuSessionIdleExpiryMs);
    connect(&m_reopenExpiryTimer, &QTimer::timeout, this, [this]() {
        endInteractiveSession();
    });

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
    m_interactiveSessionActive = false;
    m_refreshRequestPending = false;
    m_switchRequestPending = false;
    m_reopenRequested = false;
    m_lastMenuAnchor = QPoint();
    m_interactiveRefreshTimer.setInterval(m_config.interactiveRefreshIntervalMs);
    m_reopenExpiryTimer.stop();
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
    if (m_interactiveSessionActive) {
        m_interactiveRefreshTimer.start(m_config.interactiveRefreshIntervalMs);
        scheduleSessionExpiryIfMenuDoesNotReopen();
    } else {
        m_interactiveRefreshTimer.stop();
        m_interactiveRefreshTimer.setInterval(m_config.interactiveRefreshIntervalMs);
    }
}

void TrayController::handleTrayActivation(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        endInteractiveSession();
        emit openMainWindowRequested();
        return;
    }

    if (reason == QSystemTrayIcon::Context) {
        m_lastMenuAnchor = menuPopupPosition();
    }
}

void TrayController::beginInteractiveSession() {
    m_interactiveSessionActive = true;
    m_reopenRequested = false;
    cancelPendingSessionExpiry();
    m_interactiveRefreshTimer.start(m_config.interactiveRefreshIntervalMs);
    scheduleSessionExpiryIfMenuDoesNotReopen();
}

void TrayController::endInteractiveSession() {
    m_interactiveSessionActive = false;
    m_reopenRequested = false;
    m_interactiveRefreshTimer.stop();
    cancelPendingSessionExpiry();
}

void TrayController::scheduleSessionExpiryIfMenuDoesNotReopen() {
    m_reopenExpiryTimer.start(kMenuSessionIdleExpiryMs);
}

void TrayController::cancelPendingSessionExpiry() {
    m_reopenExpiryTimer.stop();
}

void TrayController::handleMenuAboutToShow() {
    beginInteractiveSession();
    m_lastMenuAnchor = menuPopupPosition();
    refreshMenuPresentation();
    requestMenuRefresh();
}

void TrayController::handleMenuAboutToHide() {
    if (!m_reopenRequested) {
        endInteractiveSession();
        return;
    }

    const QPoint anchor = m_lastMenuAnchor.isNull() ? menuPopupPosition() : m_lastMenuAnchor;
    scheduleSessionExpiryIfMenuDoesNotReopen();
    QTimer::singleShot(0, this, [this, anchor]() {
        if (!m_menu) {
            return;
        }
        m_lastMenuAnchor = anchor;
        m_menu->popup(anchor);
    });
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
            connect(action, &QAction::triggered, this, [this, profileName = profile.name]() {
                m_reopenRequested = true;
                scheduleSessionExpiryIfMenuDoesNotReopen();
                requestModeSwitch(profileName);
            });
            m_modeActions.insert(profile.name, action);
        }
    }
    m_menu->addSeparator();

    m_ipSummaryAction = addStaticAction(m_menu, "IP: Unavailable");
    m_delaySummaryAction = addStaticAction(m_menu, "Delay: Unavailable");
    m_menu->addSeparator();

    m_openAction = m_menu->addAction("Open tunlet");
    connect(m_openAction, &QAction::triggered, this, [this]() {
        endInteractiveSession();
        emit openMainWindowRequested();
    });

    m_refreshAction = m_menu->addAction("Refresh");
    connect(m_refreshAction, &QAction::triggered, this, [this]() {
        m_reopenRequested = true;
        scheduleSessionExpiryIfMenuDoesNotReopen();
        requestMenuRefresh();
    });

    m_quitAction = m_menu->addAction("Quit");
    connect(m_quitAction, &QAction::triggered, this, [this]() {
        endInteractiveSession();
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

void TrayController::emitMenuRefreshIfIdle() {
    if (!m_interactiveSessionActive) {
        return;
    }
    requestMenuRefresh();
}

void TrayController::requestMenuRefresh() {
    if (m_refreshRequestPending || m_status.busy || m_diagnostics.runtimeRefreshInFlight) {
        return;
    }
    m_refreshRequestPending = true;
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
        return;
    }
    m_switchRequestPending = true;
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
    endInteractiveSession();
    m_menu->hide();
}

}  // namespace tunlet::ui
