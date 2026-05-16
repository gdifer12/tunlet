#pragma once

#include "clash/mode_controller.hpp"

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace tunlet::ui {

class TrayController : public QObject {
    Q_OBJECT

public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;

    void setup(const clash::ModeStatus &status, const QVector<config::ClashModeProfile> &profiles);
    void show();
    bool isTrayAvailable() const;
    void updateStatus(const clash::ModeStatus &status);
    void updateProfiles(const QVector<config::ClashModeProfile> &profiles);

signals:
    void openMainWindowRequested();
    void refreshRequested();
    void switchRequested(const QString &profileName);
    void quitRequested();

private:
    void rebuildModeMenu();
    QAction *makeSwitchAction(QMenu *menu, const QString &profileName, const QString &label);

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_menu = nullptr;
    clash::ModeStatus m_status;
    QVector<config::ClashModeProfile> m_profiles;
};

}  // namespace tunlet::ui
