#pragma once

#include "clash/mode_controller.hpp"
#include "diagnostics/diagnostics_service.hpp"

#include <QPoint>
#include <QHash>
#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>

class QAction;
class QActionGroup;
class QMenu;

namespace tunlet::ui {

class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(QObject *parent = nullptr);
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
    void handleTrayActivation(QSystemTrayIcon::ActivationReason reason);
    void beginInteractiveSession();
    void endInteractiveSession();
    void scheduleSessionExpiryIfMenuDoesNotReopen();
    void cancelPendingSessionExpiry();
    QPoint menuPopupPosition() const;
    void syncVisibleStatus();
    void syncVisibleDiagnostics();
    void reconcilePendingState();
    void rebuildMenu();
    void refreshMenuPresentation();
    void updateToolTip();
    void requestMenuRefresh();
    void requestModeSwitch(const QString &profileName);

private slots:
    void handleMenuAboutToShow();
    void handleMenuAboutToHide();
    void showContextMenu();
    void hideContextMenu();
    void emitMenuRefreshIfIdle();

private:
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_menu = nullptr;
    clash::ModeStatus m_status;
    clash::ModeStatus m_visibleStatus;
    QVector<config::ClashModeProfile> m_profiles;
    diagnostics::DiagnosticsSnapshot m_diagnostics;
    diagnostics::DiagnosticsSnapshot m_visibleDiagnostics;
    config::TrayConfig m_config;
    QTimer m_interactiveRefreshTimer;
    QTimer m_reopenExpiryTimer;
    bool m_interactiveSessionActive = false;
    bool m_refreshRequestPending = false;
    bool m_switchRequestPending = false;
    bool m_reopenRequested = false;
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
