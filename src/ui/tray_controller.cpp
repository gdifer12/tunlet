#include "ui/tray_controller.hpp"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

namespace tunlet::ui {

namespace {

bool isBuiltInProfile(const QString &profileName) {
    return profileName == "direct" || profileName == "proxy" || profileName == "auto";
}

QString currentModeLabel(const clash::ModeStatus &status) {
    if (!status.currentProfileName.isEmpty() && status.currentProfileName != "unknown") {
        return status.currentProfileName;
    }
    return status.currentModeValue.isEmpty() ? QString("unknown") : status.currentModeValue;
}

}  // namespace

TrayController::TrayController(QObject *parent)
    : QObject(parent),
      m_trayIcon(new QSystemTrayIcon(QIcon(":/icons/tunlet.svg"), this)),
      m_menu(new QMenu()),
      m_modeMenu(new QMenu("More Profiles", m_menu)) {}

TrayController::~TrayController() = default;

void TrayController::setup(const clash::ModeStatus &status, const QVector<config::ClashModeProfile> &profiles) {
    m_status = status;
    m_profiles = profiles;
    rebuildModeMenu();
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
    rebuildModeMenu();
}

void TrayController::updateProfiles(const QVector<config::ClashModeProfile> &profiles) {
    m_profiles = profiles;
    rebuildModeMenu();
}

void TrayController::rebuildModeMenu() {
    m_menu->clear();
    m_modeMenu->clear();

    const QString modeLabel = currentModeLabel(m_status);
    const QString statusLabel = m_status.busy ? "refreshing" : (m_status.reachable ? "api reachable" : "api down");
    auto *summaryAction = m_menu->addAction(QString("Mode: %1 | %2").arg(modeLabel, statusLabel));
    summaryAction->setEnabled(false);

    m_menu->addSeparator();

    auto addBuiltInAction = [this](const QString &profileName, const QString &label) {
        for (const auto &profile : m_profiles) {
            if (profile.name != profileName) {
                continue;
            }
            const QString current = profile.name == m_status.currentProfileName ? QString("%1 [current]").arg(label) : label;
            auto *action = makeSwitchAction(m_menu, profile.name, current);
            action->setEnabled(!m_status.busy && profile.name != m_status.currentProfileName);
            return;
        }
    };

    addBuiltInAction("direct", "Direct");
    addBuiltInAction("proxy", "Proxy");
    addBuiltInAction("auto", "Auto");

    bool hasExtraProfiles = false;
    for (const auto &profile : m_profiles) {
        if (isBuiltInProfile(profile.name)) {
            continue;
        }
        hasExtraProfiles = true;
        const QString label = profile.name == m_status.currentProfileName
                                  ? QString("%1 [current]").arg(profile.name)
                                  : profile.name;
        auto *action = makeSwitchAction(m_modeMenu, profile.name, label);
        action->setEnabled(!m_status.busy && profile.name != m_status.currentProfileName);
    }

    if (hasExtraProfiles) {
        m_menu->addSeparator();
        m_menu->addMenu(m_modeMenu);
    }

    m_menu->addSeparator();
    auto *openAction = m_menu->addAction("Open tunlet");
    connect(openAction, &QAction::triggered, this, &TrayController::openMainWindowRequested);

    auto *refreshAction = m_menu->addAction("Refresh");
    refreshAction->setEnabled(!m_status.busy);
    connect(refreshAction, &QAction::triggered, this, &TrayController::refreshRequested);

    auto *quitAction = m_menu->addAction("Quit");
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);

    m_trayIcon->setContextMenu(m_menu);
    m_trayIcon->setToolTip(QString("tunlet\nMode: %1\nStatus: %2")
                               .arg(modeLabel, m_status.reachable ? "API reachable" : "API unavailable"));
}

QAction *TrayController::makeSwitchAction(QMenu *menu, const QString &profileName, const QString &label) {
    auto *action = menu->addAction(label);
    connect(action, &QAction::triggered, this, [this, profileName]() {
        if (!profileName.isEmpty()) {
            emit switchRequested(profileName);
        }
    });
    return action;
}

}  // namespace tunlet::ui
