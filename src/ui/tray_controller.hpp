#pragma once

#include "clash/mode_controller.hpp"
#include "diagnostics/diagnostics_service.hpp"

#include <QPoint>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QActionGroup;
class QMenu;

namespace tunlet::logging {
class LoggingService;
using LogContext = QMap<QString, QString>;
}

namespace tunlet::ui {

class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(logging::LoggingService *loggingService = nullptr, QObject *parent = nullptr);
    ~TrayController() override;

    void setup(const clash::ModeStatus &status,
               const QVector<config::ClashModeProfile> &profiles,
               const diagnostics::DiagnosticsSnapshot &diagnostics,
               const config::TrayConfig &config);
    void show();
    bool isTrayAvailable() const;
    void updateStatus(const clash::ModeStatus &status);
    void updateProfiles(const QVector<config::ClashModeProfile> &profiles);
    void updateDiagnostics(const diagnostics::DiagnosticsSnapshot &snapshot);
    void updateConfig(const config::TrayConfig &config);

signals:
    void openMainWindowRequested();
    void refreshRequested();
    void switchRequested(const QString &profileName);
    void quitRequested();

private:
    enum class MenuRefreshOrigin {
        MenuOpen,
        MenuAction,
    };

    void handleTrayActivation(QSystemTrayIcon::ActivationReason reason);
    QString menuRefreshOriginName(MenuRefreshOrigin origin) const;
    void logTrayInfo(const QString &summary, const QString &detail = {}, const logging::LogContext &context = {}) const;
    QPoint menuPopupPosition() const;
    void syncVisibleStatus();
    void syncVisibleDiagnostics();
    void reconcilePendingState();
    void rebuildMenu();
    void refreshMenuPresentation();
    void updateTrayIcon();
    void updateToolTip();
    void requestMenuRefresh(MenuRefreshOrigin origin);
    void requestModeSwitch(const QString &profileName);

private slots:
    void handleMenuAboutToShow();
    void showContextMenu();
    void hideContextMenu();

private:
    logging::LoggingService *m_logger = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_menu = nullptr;
    clash::ModeStatus m_status;
    clash::ModeStatus m_visibleStatus;
    QVector<config::ClashModeProfile> m_profiles;
    diagnostics::DiagnosticsSnapshot m_diagnostics;
    diagnostics::DiagnosticsSnapshot m_visibleDiagnostics;
    config::TrayConfig m_config;
    bool m_refreshRequestPending = false;
    bool m_switchRequestPending = false;
    QPoint m_lastMenuAnchor;
    QAction *m_modeSummaryAction = nullptr;
    QAction *m_apiSummaryAction = nullptr;
    QAction *m_ipSummaryAction = nullptr;
    QAction *m_delaySummaryAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_refreshAction = nullptr;
    QAction *m_quitAction = nullptr;
    QActionGroup *m_modeActionGroup = nullptr;
    QHash<QString, QAction *> m_modeActions;
};

}  // namespace tunlet::ui
