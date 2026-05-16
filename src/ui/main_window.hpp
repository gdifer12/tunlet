#pragma once

#include "clash/mode_controller.hpp"
#include "config/app_config.hpp"
#include "config/config_file_service.hpp"
#include "config/config_loader.hpp"
#include "diagnostics/diagnostics_service.hpp"
#include "rules/ruleset_service.hpp"

#include <QMainWindow>
#include <QPointer>
#include <QVector>

class QAbstractButton;
class QEvent;
class QLabel;
class QObject;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;
class QToolButton;
class QWidget;

namespace tunlet::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(const config::AppConfig &config,
               clash::ModeController *modeController,
               config::ConfigFileService *configFileService,
               diagnostics::DiagnosticsService *diagnosticsService,
               rules::RuleSetService *ruleSetService,
               bool trayAvailable,
               QWidget *parent = nullptr);

public slots:
    void showAndRaise();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onStatusUpdated(const clash::ModeStatus &status);
    void onDiagnosticsUpdated(const tunlet::diagnostics::DiagnosticsSnapshot &snapshot);
    void onRuleFileSelectionChanged();
    void onRuleEditorTextChanged();
    void onSettingsEditorTextChanged();
    void validateCurrentEditorText();
    void saveCurrentRuleFile();
    void reloadCurrentRuleFile();
    void applySelectedModeProfile();
    void validateSettingsText();
    void saveSettingsFile();
    void reloadSettingsFile();

private:
    struct NamedRuleFile {
        QString name;
        QString path;
        QString description;
    };

    void buildUi(bool trayAvailable);
    QWidget *buildWindowTitleBar();
    QWidget *buildTopRuntimeStrip();
    QWidget *buildHealthStrip();
    QWidget *buildBottomNav();
    QWidget *buildBottomStatusLine();
    QWidget *buildDashboardPage();
    QWidget *buildRuleSetsPage();
    QWidget *buildSettingsInfoPage();
    QScrollArea *wrapPageInScrollArea(QWidget *content, const QString &objectName);
    void applyConfig(const config::AppConfig &config);
    void populateRuleFiles();
    void populateModeProfiles();
    void updateDashboardCards();
    void updateModeSelectionUi();
    void updateWindowSizeLabel();
    void updateRuleLineNumbers();
    void refreshRecentActionLabel();
    void closeSelectorPopup();
    void openModePopup();
    void openRuleFilePopup();
    void positionSelectorPopup(QWidget *trigger);
    void updateRuleFileTrigger();
    void showActionMessage(const QString &message, int timeoutMs = 0);
    void setStatusPill(QLabel *label, const QString &text, const QString &tone);
    void setRuleBanner(const QString &title, const QString &message, const QString &tone);
    void setSettingsBanner(const QString &text, const QString &tone);
    void repolish(QWidget *widget);
    void setCurrentPage(int index);
    void setSelectedProfileName(const QString &profileName);
    bool hasProfile(const QString &profileName) const;
    QString selectedModeProfileName() const;
    const NamedRuleFile *selectedRuleFile() const;

    config::AppConfig m_config;
    clash::ModeController *m_modeController = nullptr;
    config::ConfigFileService *m_configFileService = nullptr;
    diagnostics::DiagnosticsService *m_diagnosticsService = nullptr;
    rules::RuleSetService *m_ruleSetService = nullptr;
    QVector<NamedRuleFile> m_ruleFiles;
    clash::ModeStatus m_lastStatus;
    diagnostics::DiagnosticsSnapshot m_lastDiagnostics;
    QString m_selectedProfileName;
    QString m_selectedRuleFilePath;
    QString m_loadedRuleText;
    QString m_loadedSettingsText;
    QString m_recentActionText = "Ready";

    QStackedWidget *m_pages = nullptr;
    QVector<QPointer<QAbstractButton>> m_navButtons;

    QLabel *m_headerReachabilityLabel = nullptr;
    QLabel *m_windowSizeLabel = nullptr;
    QLabel *m_topRuntimeSummaryLabel = nullptr;
    QLabel *m_topRuntimeStatusLabel = nullptr;

    QLabel *m_mainPanelStatusLabel = nullptr;
    QLabel *m_currentProfileLabel = nullptr;
    QLabel *m_modeChipLabel = nullptr;
    QLabel *m_profileDescriptionLabel = nullptr;
    QLabel *m_serviceStatusValue = nullptr;
    QLabel *m_serviceStatusDetail = nullptr;
    QLabel *m_stateModeValue = nullptr;
    QLabel *m_stateModeDetail = nullptr;
    QLabel *m_selectedProfileValue = nullptr;
    QLabel *m_selectedProfileDetail = nullptr;
    QLabel *m_lastReloadValue = nullptr;
    QLabel *m_lastReloadDetail = nullptr;
    QLabel *m_connectionEndpointValue = nullptr;
    QLabel *m_connectionDiagnosticsValue = nullptr;
    QLabel *m_connectionTunValue = nullptr;
    QLabel *m_connectionDnsValue = nullptr;
    QLabel *m_connectionRoutingValue = nullptr;
    QLabel *m_controllerAddressValue = nullptr;
    QLabel *m_rulesDirectoryValue = nullptr;
    QLabel *m_configRootValue = nullptr;
    QLabel *m_profileHintValue = nullptr;
    QPushButton *m_modeTriggerButton = nullptr;
    QLabel *m_modeTriggerValueLabel = nullptr;
    QLabel *m_modeTriggerSubLabel = nullptr;
    QLabel *m_modeTriggerCaretLabel = nullptr;

    QLabel *m_rulePageStatusLabel = nullptr;
    QLabel *m_ruleFileDescriptionLabel = nullptr;
    QLabel *m_ruleFilePathLabel = nullptr;
    QLabel *m_ruleEditorTitleLabel = nullptr;
    QLabel *m_ruleEditorPathLabel = nullptr;
    QLabel *m_ruleBannerTitleLabel = nullptr;
    QLabel *m_ruleBannerMessageLabel = nullptr;
    QLabel *m_ruleBannerStateLabel = nullptr;
    QLabel *m_ruleLineNumbersLabel = nullptr;
    QPushButton *m_ruleFileTriggerButton = nullptr;
    QLabel *m_ruleFileTriggerNameLabel = nullptr;
    QLabel *m_ruleFileTriggerDescriptionLabel = nullptr;
    QLabel *m_ruleFileTriggerPathLabel = nullptr;
    QLabel *m_ruleFileTriggerCaretLabel = nullptr;
    QPlainTextEdit *m_editor = nullptr;

    QLabel *m_settingsPageStatusLabel = nullptr;
    QLabel *m_appBuildValue = nullptr;
    QLabel *m_infoConfigPathValue = nullptr;
    QLabel *m_infoRulesDirValue = nullptr;
    QLabel *m_infoEndpointValue = nullptr;
    QLabel *m_infoDiagnosticsValue = nullptr;
    QLabel *m_infoProfilesValue = nullptr;
    QLabel *m_infoThemeValue = nullptr;
    QLabel *m_stateApiStatusValue = nullptr;
    QLabel *m_stateCurrentModeValue = nullptr;
    QLabel *m_stateLastRefreshValue = nullptr;
    QLabel *m_stateLastDetailValue = nullptr;
    QLabel *m_stateExternalIpValue = nullptr;
    QLabel *m_stateTrafficValue = nullptr;
    QLabel *m_stateModeListValue = nullptr;
    QLabel *m_settingsStatusLabel = nullptr;
    QPlainTextEdit *m_settingsEditor = nullptr;

    QLabel *m_footerApiValue = nullptr;
    QLabel *m_footerLatencyValue = nullptr;
    QLabel *m_footerTunValue = nullptr;
    QLabel *m_footerDnsValue = nullptr;
    QLabel *m_footerReloadValue = nullptr;
    QLabel *m_recentActionLabel = nullptr;
    QWidget *m_selectorPopup = nullptr;
    QWidget *m_selectorPopupTrigger = nullptr;

    bool m_trayAvailable = false;
};

}  // namespace tunlet::ui
