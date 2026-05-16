#include "ui/main_window.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

namespace tunlet::ui {

namespace {

QString endpointLabelForConfig(const config::AppConfig &config) {
    return QString("%1:%2").arg(config.clashApi.host).arg(config.clashApi.port);
}

QString compactPath(const QString &path) {
    const QString expanded = QDir::cleanPath(path);
    const QString home = QDir::homePath();
    if (expanded.startsWith(home)) {
        return "~" + expanded.mid(home.size());
    }
    return expanded;
}

QString rulesDirectoryForConfig(const config::AppConfig &config) {
    const QFileInfo ruleInfo(config.ruleSets.autoProxyPath);
    return compactPath(ruleInfo.dir().absolutePath());
}

QString configRootForConfig(const config::AppConfig &config) {
    const QFileInfo configInfo(config.configPath);
    return compactPath(configInfo.dir().absolutePath());
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

QString joinOrUnknown(const QStringList &items) {
    return items.isEmpty() ? QString("unknown") : items.join(", ");
}

QString lastRefreshLabel(const QDateTime &time) {
    return time.isValid() ? time.toString("HH:mm:ss") : QString("Waiting");
}

QWidget *buildMetricItem(QWidget *parent, const QString &labelText, QLabel **valueLabel) {
    auto *item = new QWidget(parent);
    item->setObjectName("metricItem");

    auto *layout = new QVBoxLayout(item);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(6);

    auto *label = new QLabel(labelText, item);
    label->setObjectName("metricLabel");
    layout->addWidget(label);

    *valueLabel = new QLabel(item);
    (*valueLabel)->setObjectName("metricValue");
    (*valueLabel)->setWordWrap(true);
    (*valueLabel)->setTextFormat(Qt::PlainText);
    layout->addWidget(*valueLabel);

    return item;
}

QWidget *buildSummaryItem(QWidget *parent, const QString &labelText, QLabel **valueLabel, QLabel **detailLabel) {
    auto *item = new QWidget(parent);
    item->setObjectName("summaryItem");

    auto *layout = new QVBoxLayout(item);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *label = new QLabel(labelText, item);
    label->setObjectName("summaryKey");
    layout->addWidget(label);

    *valueLabel = new QLabel(item);
    (*valueLabel)->setObjectName("summaryValue");
    (*valueLabel)->setWordWrap(true);
    (*valueLabel)->setTextFormat(Qt::PlainText);
    layout->addWidget(*valueLabel);

    *detailLabel = new QLabel(item);
    (*detailLabel)->setObjectName("summarySub");
    (*detailLabel)->setWordWrap(true);
    (*detailLabel)->setTextFormat(Qt::PlainText);
    layout->addWidget(*detailLabel);

    return item;
}

QLabel *buildKeyValueRow(QGridLayout *layout, int row, const QString &key, QWidget *parent) {
    auto *keyLabel = new QLabel(key, parent);
    keyLabel->setObjectName("kvKey");
    auto *valueLabel = new QLabel(parent);
    valueLabel->setObjectName("kvValue");
    valueLabel->setWordWrap(true);
    valueLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(keyLabel, row, 0, Qt::AlignTop);
    layout->addWidget(valueLabel, row, 1);
    return valueLabel;
}

QLabel *buildTitleDot(QWidget *parent, const QString &tone) {
    auto *dot = new QLabel(parent);
    dot->setObjectName("titleDot");
    dot->setProperty("tone", tone);
    dot->setFixedSize(10, 10);
    return dot;
}

}  // namespace

MainWindow::MainWindow(const config::AppConfig &config,
                       clash::ModeController *modeController,
                       config::ConfigFileService *configFileService,
                       diagnostics::DiagnosticsService *diagnosticsService,
                       rules::RuleSetService *ruleSetService,
                       bool trayAvailable,
                       QWidget *parent)
    : QMainWindow(parent),
      m_config(config),
      m_modeController(modeController),
      m_configFileService(configFileService),
      m_diagnosticsService(diagnosticsService),
      m_ruleSetService(ruleSetService),
      m_trayAvailable(trayAvailable) {
    buildUi(trayAvailable);
    populateModeProfiles();
    populateRuleFiles();

    connect(m_modeController, &clash::ModeController::statusUpdated, this, &MainWindow::onStatusUpdated);
    connect(m_modeController, &clash::ModeController::profilesUpdated, this, [this](const QVector<config::ClashModeProfile> &) {
        populateModeProfiles();
        updateModeSelectionUi();
    });
    connect(m_modeController, &clash::ModeController::operationFailed, this, [this](const QString &message) {
        showActionMessage(message, 5000);
        updateDashboardCards();
    });
    connect(m_diagnosticsService, &diagnostics::DiagnosticsService::diagnosticsUpdated, this, &MainWindow::onDiagnosticsUpdated);

    onStatusUpdated(m_modeController->status());
    onDiagnosticsUpdated(m_diagnosticsService->snapshot());
}

void MainWindow::showAndRaise() {
    show();
    raise();
    activateWindow();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateWindowSizeLabel();
}

void MainWindow::buildUi(bool trayAvailable) {
    setWindowTitle("tunlet");
    resize(1040, 760);
    setMinimumSize(920, 640);

    auto *root = new QWidget(this);
    root->setObjectName("appRoot");
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(0);

    auto *windowFrame = new QWidget(root);
    windowFrame->setObjectName("windowFrame");
    auto *windowLayout = new QVBoxLayout(windowFrame);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(buildWindowTitleBar());

    auto *windowBody = new QWidget(windowFrame);
    windowBody->setObjectName("windowBody");
    auto *bodyLayout = new QHBoxLayout(windowBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(buildSidebar());

    auto *contentShell = new QWidget(windowBody);
    contentShell->setObjectName("contentShell");
    auto *contentLayout = new QVBoxLayout(contentShell);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_pages = new QStackedWidget(contentShell);
    m_pages->setObjectName("contentPages");
    m_pages->addWidget(buildDashboardPage());
    m_pages->addWidget(buildRuleSetsPage());
    m_pages->addWidget(buildSettingsInfoPage());
    contentLayout->addWidget(m_pages, 1);
    contentLayout->addWidget(buildHealthStrip());
    bodyLayout->addWidget(contentShell, 1);

    windowLayout->addWidget(windowBody, 1);
    rootLayout->addWidget(windowFrame, 1);
    setCentralWidget(root);

    auto *nextPageShortcut = new QShortcut(QKeySequence("Ctrl+Tab"), this);
    connect(nextPageShortcut, &QShortcut::activated, this, [this]() {
        if (m_navButtons.isEmpty()) {
            return;
        }
        const int next = (m_pages->currentIndex() + 1) % m_navButtons.size();
        setCurrentPage(next);
    });

    auto *prevPageShortcut = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
    connect(prevPageShortcut, &QShortcut::activated, this, [this]() {
        if (m_navButtons.isEmpty()) {
            return;
        }
        const int current = m_pages->currentIndex();
        const int prev = (current - 1 + m_navButtons.size()) % m_navButtons.size();
        setCurrentPage(prev);
    });

    statusBar()->hide();
    setCurrentPage(0);
    updateWindowSizeLabel();
    showActionMessage("Ready");
    reloadSettingsFile();

    Q_UNUSED(trayAvailable);
}

QWidget *MainWindow::buildWindowTitleBar() {
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName("windowTitleBar");
    auto *layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(16);

    auto *controls = new QWidget(titleBar);
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);
    controlsLayout->addWidget(buildTitleDot(controls, "danger"));
    controlsLayout->addWidget(buildTitleDot(controls, "warn"));
    controlsLayout->addWidget(buildTitleDot(controls, "ok"));
    layout->addWidget(controls, 0, Qt::AlignVCenter);

    auto *titleMeta = new QVBoxLayout();
    titleMeta->setSpacing(2);
    auto *title = new QLabel("tunlet", titleBar);
    title->setObjectName("titleBarTitle");
    auto *subtitle = new QLabel("Local Clash runtime controller", titleBar);
    subtitle->setObjectName("monoNote");
    titleMeta->addWidget(title);
    titleMeta->addWidget(subtitle);
    layout->addLayout(titleMeta);
    layout->addStretch(1);

    m_headerReachabilityLabel = new QLabel(titleBar);
    m_headerReachabilityLabel->setObjectName("statusPill");
    m_windowSizeLabel = new QLabel(titleBar);
    m_windowSizeLabel->setObjectName("monoNote");

    auto *right = new QHBoxLayout();
    right->setSpacing(10);
    right->addWidget(m_headerReachabilityLabel);
    right->addWidget(m_windowSizeLabel);
    layout->addLayout(right);

    return titleBar;
}

QWidget *MainWindow::buildSidebar() {
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(248);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(16, 18, 16, 16);
    layout->setSpacing(18);

    auto *brandBlock = new QWidget(sidebar);
    auto *brandLayout = new QHBoxLayout(brandBlock);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(12);

    auto *brandCopy = new QVBoxLayout();
    brandCopy->setSpacing(4);
    auto *eyebrow = new QLabel("Main control", brandBlock);
    eyebrow->setObjectName("eyebrow");
    auto *title = new QLabel("tunlet", brandBlock);
    title->setObjectName("sidebarTitle");
    brandCopy->addWidget(eyebrow);
    brandCopy->addWidget(title);
    brandLayout->addLayout(brandCopy, 1);

    m_sidebarStatusLabel = new QLabel(brandBlock);
    m_sidebarStatusLabel->setObjectName("statusPill");
    brandLayout->addWidget(m_sidebarStatusLabel, 0, Qt::AlignTop);
    layout->addWidget(brandBlock);

    auto *modeCard = new QWidget(sidebar);
    modeCard->setObjectName("sidebarCard");
    auto *modeLayout = new QVBoxLayout(modeCard);
    modeLayout->setContentsMargins(14, 14, 14, 14);
    modeLayout->setSpacing(6);
    auto *modeMeta = new QLabel("Current mode", modeCard);
    modeMeta->setObjectName("metaLabel");
    m_sidebarModeLabel = new QLabel(modeCard);
    m_sidebarModeLabel->setObjectName("summaryValue");
    m_sidebarModeDetailLabel = new QLabel(modeCard);
    m_sidebarModeDetailLabel->setObjectName("summarySub");
    m_sidebarModeDetailLabel->setWordWrap(true);
    modeLayout->addWidget(modeMeta);
    modeLayout->addWidget(m_sidebarModeLabel);
    modeLayout->addWidget(m_sidebarModeDetailLabel);
    layout->addWidget(modeCard);

    auto *navList = new QWidget(sidebar);
    auto *navLayout = new QVBoxLayout(navList);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(8);
    const QVector<QPair<QString, QString>> sections = {
        {"Main", "Runtime state and fast mode switching"},
        {"Rules", "Local routing files with validation and save control"},
        {"Settings / Info", "Runtime detail and live config editing"},
    };
    for (int index = 0; index < sections.size(); ++index) {
        auto *button = new QPushButton(QString("%1\n%2").arg(sections.at(index).first, sections.at(index).second), navList);
        button->setObjectName("sidebarTabButton");
        button->setProperty("active", false);
        button->setMinimumHeight(72);
        button->setCursor(Qt::PointingHandCursor);
        m_navButtons.push_back(button);
        connect(button, &QPushButton::clicked, this, [this, index]() {
            setCurrentPage(index);
        });
        navLayout->addWidget(button);
    }
    layout->addWidget(navList);

    auto *hostCard = new QWidget(sidebar);
    hostCard->setObjectName("sidebarCard");
    auto *hostLayout = new QVBoxLayout(hostCard);
    hostLayout->setContentsMargins(14, 14, 14, 14);
    hostLayout->setSpacing(6);
    auto *hostMeta = new QLabel("Host summary", hostCard);
    hostMeta->setObjectName("metaLabel");
    m_sidebarHostSummaryLabel = new QLabel(hostCard);
    m_sidebarHostSummaryLabel->setObjectName("summarySub");
    m_sidebarHostSummaryLabel->setWordWrap(true);
    hostLayout->addWidget(hostMeta);
    hostLayout->addWidget(m_sidebarHostSummaryLabel);
    layout->addStretch(1);
    layout->addWidget(hostCard);

    return sidebar;
}

QWidget *MainWindow::buildHealthStrip() {
    auto *strip = new QWidget(this);
    strip->setObjectName("healthStrip");

    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(14);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    const QStringList labels = {"Clash API", "Latency", "Mode", "Public IP", "Last refresh"};
    QLabel **targets[] = {
        &m_footerApiValue,
        &m_footerLatencyValue,
        &m_footerModeValue,
        &m_footerIpValue,
        &m_footerReloadValue,
    };

    for (int index = 0; index < labels.size(); ++index) {
        auto *item = new QWidget(strip);
        item->setObjectName("healthItem");
        auto *itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(12, 10, 12, 10);
        itemLayout->setSpacing(4);
        auto *meta = new QLabel(labels.at(index), item);
        meta->setObjectName("metaLabel");
        *targets[index] = new QLabel(item);
        (*targets[index])->setObjectName("healthValue");
        (*targets[index])->setWordWrap(true);
        itemLayout->addWidget(meta);
        itemLayout->addWidget(*targets[index]);
        grid->addWidget(item, 0, index);
    }

    auto *gridHost = new QWidget(strip);
    gridHost->setLayout(grid);
    layout->addWidget(gridHost, 1);

    auto *actionLog = new QWidget(strip);
    auto *actionLayout = new QVBoxLayout(actionLog);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(4);
    auto *meta = new QLabel("Recent action", actionLog);
    meta->setObjectName("metaLabel");
    m_recentActionLabel = new QLabel("Ready", actionLog);
    m_recentActionLabel->setObjectName("healthValue");
    m_recentActionLabel->setWordWrap(true);
    actionLayout->addWidget(meta);
    actionLayout->addWidget(m_recentActionLabel);
    layout->addWidget(actionLog, 0, Qt::AlignRight | Qt::AlignVCenter);

    return strip;
}

QWidget *MainWindow::buildDashboardPage() {
    auto *page = new QWidget(this);
    page->setObjectName("panelPage");
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(22, 22, 22, 22);
    pageLayout->setSpacing(16);

    auto *panelHead = new QWidget(page);
    auto *headLayout = new QHBoxLayout(panelHead);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(18);

    auto *headCopy = new QVBoxLayout();
    headCopy->setSpacing(6);
    auto *title = new QLabel("Runtime control", panelHead);
    title->setObjectName("panelTitle");
    auto *subtitle = new QLabel(
        "Immediate mode switching first, then connection health, local diagnostics, and runtime detail.",
        panelHead);
    subtitle->setObjectName("panelSubtitle");
    subtitle->setWordWrap(true);
    headCopy->addWidget(title);
    headCopy->addWidget(subtitle);
    headLayout->addLayout(headCopy, 1);

    m_mainPanelStatusLabel = new QLabel(panelHead);
    m_mainPanelStatusLabel->setObjectName("statusPill");
    headLayout->addWidget(m_mainPanelStatusLabel, 0, Qt::AlignTop);
    pageLayout->addWidget(panelHead);

    auto *mainGrid = new QGridLayout();
    mainGrid->setHorizontalSpacing(16);
    mainGrid->setVerticalSpacing(16);
    mainGrid->setColumnStretch(0, 12);
    mainGrid->setColumnStretch(1, 9);

    auto *stateCard = new QWidget(page);
    stateCard->setObjectName("card");
    auto *stateLayout = new QVBoxLayout(stateCard);
    stateLayout->setContentsMargins(18, 18, 18, 18);
    stateLayout->setSpacing(14);

    auto *stateHead = new QWidget(stateCard);
    auto *stateHeadLayout = new QHBoxLayout(stateHead);
    stateHeadLayout->setContentsMargins(0, 0, 0, 0);
    stateHeadLayout->setSpacing(12);
    auto *stateCopy = new QVBoxLayout();
    stateCopy->setSpacing(4);
    auto *stateTitle = new QLabel("Current state", stateHead);
    stateTitle->setObjectName("cardTitleStrong");
    auto *stateSubtitle = new QLabel("Local runtime snapshot", stateHead);
    stateSubtitle->setObjectName("cardSubtitle");
    stateCopy->addWidget(stateTitle);
    stateCopy->addWidget(stateSubtitle);
    stateHeadLayout->addLayout(stateCopy, 1);
    auto *statePill = new QLabel("Runtime", stateHead);
    statePill->setObjectName("statusPill");
    statePill->setProperty("tone", "neutral");
    stateHeadLayout->addWidget(statePill, 0, Qt::AlignTop);
    stateLayout->addWidget(stateHead);

    auto *summaryGrid = new QGridLayout();
    summaryGrid->setHorizontalSpacing(18);
    summaryGrid->setVerticalSpacing(14);
    summaryGrid->addWidget(buildSummaryItem(stateCard, "Service status", &m_serviceStatusValue, &m_serviceStatusDetail), 0, 0);
    summaryGrid->addWidget(buildSummaryItem(stateCard, "Active mode", &m_stateModeValue, &m_stateModeDetail), 0, 1);
    summaryGrid->addWidget(buildSummaryItem(stateCard, "Selected profile", &m_selectedProfileValue, &m_selectedProfileDetail), 1, 0);
    summaryGrid->addWidget(buildSummaryItem(stateCard, "Last refresh", &m_lastReloadValue, &m_lastReloadDetail), 1, 1);
    stateLayout->addLayout(summaryGrid);
    mainGrid->addWidget(stateCard, 0, 0);

    auto *selectorCard = new QWidget(page);
    selectorCard->setObjectName("card");
    auto *selectorLayout = new QVBoxLayout(selectorCard);
    selectorLayout->setContentsMargins(18, 18, 18, 18);
    selectorLayout->setSpacing(14);

    auto *selectorHead = new QWidget(selectorCard);
    auto *selectorHeadLayout = new QHBoxLayout(selectorHead);
    selectorHeadLayout->setContentsMargins(0, 0, 0, 0);
    selectorHeadLayout->setSpacing(12);
    auto *selectorCopy = new QVBoxLayout();
    selectorCopy->setSpacing(4);
    auto *selectorTitle = new QLabel("Primary mode selector", selectorHead);
    selectorTitle->setObjectName("cardTitleStrong");
    auto *selectorSubtitle = new QLabel("Direct, Proxy, Auto, plus any configured extra profiles", selectorHead);
    selectorSubtitle->setObjectName("cardSubtitle");
    selectorSubtitle->setWordWrap(true);
    selectorCopy->addWidget(selectorTitle);
    selectorCopy->addWidget(selectorSubtitle);
    selectorHeadLayout->addLayout(selectorCopy, 1);
    auto *selectorNote = new QLabel("Immediate action", selectorHead);
    selectorNote->setObjectName("metaChip");
    selectorHeadLayout->addWidget(selectorNote, 0, Qt::AlignTop);
    selectorLayout->addWidget(selectorHead);

    auto *modeMeta = new QLabel("Current mode", selectorCard);
    modeMeta->setObjectName("summaryKey");
    m_currentProfileLabel = new QLabel("Unknown", selectorCard);
    m_currentProfileLabel->setObjectName("heroMode");
    m_modeChipLabel = new QLabel("Mode value: unknown", selectorCard);
    m_modeChipLabel->setObjectName("summarySub");
    selectorLayout->addWidget(modeMeta);
    selectorLayout->addWidget(m_currentProfileLabel);
    selectorLayout->addWidget(m_modeChipLabel);

    m_profileCombo = new QComboBox(selectorCard);
    m_profileCombo->setObjectName("modeProfileCombo");
    m_profileCombo->setMinimumHeight(56);
    connect(m_profileCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (!m_profileCombo || index < 0) {
            return;
        }

        const QString profileName = m_profileCombo->itemData(index).toString();
        if (profileName.isEmpty()) {
            return;
        }

        setSelectedProfileName(profileName);
        if (!m_lastStatus.busy && profileName != m_lastStatus.currentProfileName) {
            applySelectedModeProfile();
        }
    });
    selectorLayout->addWidget(m_profileCombo);

    m_profileDescriptionLabel = new QLabel("Choose a profile to switch immediately.", selectorCard);
    m_profileDescriptionLabel->setObjectName("cardSubtitle");
    m_profileDescriptionLabel->setWordWrap(true);
    selectorLayout->addWidget(m_profileDescriptionLabel);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);
    auto *refreshButton = new QPushButton("Refresh runtime", selectorCard);
    refreshButton->setObjectName("ghostButton");
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        m_modeController->refreshStatus();
        m_diagnosticsService->refreshNow();
        showActionMessage("Requested runtime refresh", 3000);
    });
    auto *reloadButton = new QPushButton("Reload config", selectorCard);
    reloadButton->setObjectName("ghostButton");
    connect(reloadButton, &QPushButton::clicked, this, &MainWindow::reloadSettingsFile);
    actionRow->addWidget(refreshButton);
    actionRow->addWidget(reloadButton);
    actionRow->addStretch(1);
    selectorLayout->addLayout(actionRow);
    mainGrid->addWidget(selectorCard, 0, 1);

    auto *connectionCard = new QWidget(page);
    connectionCard->setObjectName("card");
    auto *connectionLayout = new QVBoxLayout(connectionCard);
    connectionLayout->setContentsMargins(18, 18, 18, 18);
    connectionLayout->setSpacing(14);
    auto *connectionTitle = new QLabel("Connection info", connectionCard);
    connectionTitle->setObjectName("cardTitleStrong");
    auto *connectionSubtitle = new QLabel("Fast scanning for controller health", connectionCard);
    connectionSubtitle->setObjectName("cardSubtitle");
    connectionLayout->addWidget(connectionTitle);
    connectionLayout->addWidget(connectionSubtitle);

    auto *metricGrid = new QGridLayout();
    metricGrid->setHorizontalSpacing(12);
    metricGrid->setVerticalSpacing(12);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Clash API", &m_connectionEndpointValue), 0, 0);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Diagnostics", &m_connectionDiagnosticsValue), 0, 1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Public IPs", &m_connectionIpValue), 1, 0);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Location", &m_connectionLocationValue), 1, 1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Routing summary", &m_connectionRoutingValue), 2, 0);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Supported modes", &m_connectionModesValue), 2, 1);
    connectionLayout->addLayout(metricGrid);
    mainGrid->addWidget(connectionCard, 1, 0);

    auto *notesCard = new QWidget(page);
    notesCard->setObjectName("card");
    auto *notesLayout = new QVBoxLayout(notesCard);
    notesLayout->setContentsMargins(18, 18, 18, 18);
    notesLayout->setSpacing(14);
    auto *notesTitle = new QLabel("Operational notes", notesCard);
    notesTitle->setObjectName("cardTitleStrong");
    auto *notesSubtitle = new QLabel("Tray-friendly clarity and local file context", notesCard);
    notesSubtitle->setObjectName("cardSubtitle");
    notesLayout->addWidget(notesTitle);
    notesLayout->addWidget(notesSubtitle);
    notesLayout->addWidget(buildMetricItem(notesCard, "Rules directory", &m_rulesDirectoryValue));
    notesLayout->addWidget(buildMetricItem(notesCard, "Config root", &m_configRootValue));
    notesLayout->addWidget(buildMetricItem(notesCard, "Profile hint", &m_profileHintValue));
    notesLayout->addWidget(buildMetricItem(notesCard, "Tray state", &m_traySummaryValue));
    mainGrid->addWidget(notesCard, 1, 1);

    pageLayout->addLayout(mainGrid);
    pageLayout->addStretch(1);
    return page;
}

QWidget *MainWindow::buildRuleSetsPage() {
    auto *page = new QWidget(this);
    page->setObjectName("panelPage");
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(22, 22, 22, 22);
    pageLayout->setSpacing(16);

    auto *panelHead = new QWidget(page);
    auto *headLayout = new QHBoxLayout(panelHead);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(18);

    auto *headCopy = new QVBoxLayout();
    headCopy->setSpacing(6);
    auto *title = new QLabel("Local rule files", panelHead);
    title->setObjectName("panelTitle");
    auto *subtitle = new QLabel(
        "Routing rules stay explicit: file list on the left, editor on the right, validation before save.",
        panelHead);
    subtitle->setObjectName("panelSubtitle");
    subtitle->setWordWrap(true);
    headCopy->addWidget(title);
    headCopy->addWidget(subtitle);
    headLayout->addLayout(headCopy, 1);

    m_rulePageStatusLabel = new QLabel(panelHead);
    m_rulePageStatusLabel->setObjectName("statusPill");
    headLayout->addWidget(m_rulePageStatusLabel, 0, Qt::AlignTop);
    pageLayout->addWidget(panelHead);

    auto *rulesLayout = new QHBoxLayout();
    rulesLayout->setSpacing(16);

    auto *filesCard = new QWidget(page);
    filesCard->setObjectName("card");
    filesCard->setFixedWidth(286);
    auto *filesLayout = new QVBoxLayout(filesCard);
    filesLayout->setContentsMargins(18, 18, 18, 18);
    filesLayout->setSpacing(12);
    auto *filesTitle = new QLabel("Rule set", filesCard);
    filesTitle->setObjectName("cardTitleStrong");
    auto *filesHint = new QLabel(QString("Located under %1").arg(rulesDirectoryForConfig(m_config)), filesCard);
    filesHint->setObjectName("cardSubtitle");
    filesHint->setWordWrap(true);
    m_ruleFileList = new QListWidget(filesCard);
    m_ruleFileList->setObjectName("ruleFileList");
    connect(m_ruleFileList, &QListWidget::currentRowChanged, this, &MainWindow::onRuleFileSelectionChanged);
    filesLayout->addWidget(filesTitle);
    filesLayout->addWidget(filesHint);
    filesLayout->addWidget(m_ruleFileList, 1);
    rulesLayout->addWidget(filesCard);

    auto *editorCard = new QWidget(page);
    editorCard->setObjectName("card");
    auto *editorLayout = new QVBoxLayout(editorCard);
    editorLayout->setContentsMargins(18, 18, 18, 18);
    editorLayout->setSpacing(14);

    auto *toolbar = new QWidget(editorCard);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(12);
    auto *titleCopy = new QVBoxLayout();
    titleCopy->setSpacing(4);
    m_ruleEditorTitleLabel = new QLabel("Select a rule file", toolbar);
    m_ruleEditorTitleLabel->setObjectName("cardTitleStrong");
    m_ruleEditorPathLabel = new QLabel("No file selected", toolbar);
    m_ruleEditorPathLabel->setObjectName("monoNote");
    titleCopy->addWidget(m_ruleEditorTitleLabel);
    titleCopy->addWidget(m_ruleEditorPathLabel);
    toolbarLayout->addLayout(titleCopy, 1);

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    auto *saveButton = new QPushButton("Save", toolbar);
    saveButton->setObjectName("primaryButton");
    auto *reloadButton = new QPushButton("Revert", toolbar);
    reloadButton->setObjectName("ghostButton");
    auto *validateButton = new QPushButton("Validate", toolbar);
    validateButton->setObjectName("ghostButton");
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveCurrentRuleFile);
    connect(reloadButton, &QPushButton::clicked, this, &MainWindow::reloadCurrentRuleFile);
    connect(validateButton, &QPushButton::clicked, this, &MainWindow::validateCurrentEditorText);
    buttons->addWidget(saveButton);
    buttons->addWidget(reloadButton);
    buttons->addWidget(validateButton);
    toolbarLayout->addLayout(buttons);
    editorLayout->addWidget(toolbar);

    auto *banner = new QWidget(editorCard);
    banner->setObjectName("editorBanner");
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(12, 10, 12, 10);
    bannerLayout->setSpacing(12);
    auto *bannerCopy = new QVBoxLayout();
    bannerCopy->setSpacing(4);
    m_ruleBannerTitleLabel = new QLabel("Ready", banner);
    m_ruleBannerTitleLabel->setObjectName("bannerTitle");
    m_ruleBannerMessageLabel = new QLabel("Select a rule-set file to begin.", banner);
    m_ruleBannerMessageLabel->setObjectName("bannerMessage");
    m_ruleBannerMessageLabel->setWordWrap(true);
    bannerCopy->addWidget(m_ruleBannerTitleLabel);
    bannerCopy->addWidget(m_ruleBannerMessageLabel);
    bannerLayout->addLayout(bannerCopy, 1);
    m_ruleBannerStateLabel = new QLabel(banner);
    m_ruleBannerStateLabel->setObjectName("statusPill");
    bannerLayout->addWidget(m_ruleBannerStateLabel, 0, Qt::AlignTop);
    editorLayout->addWidget(banner);

    auto *editorFrame = new QWidget(editorCard);
    editorFrame->setObjectName("editorFrame");
    auto *frameLayout = new QHBoxLayout(editorFrame);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(0);
    m_ruleLineNumbersLabel = new QLabel("1", editorFrame);
    m_ruleLineNumbersLabel->setObjectName("lineNumbers");
    m_ruleLineNumbersLabel->setAlignment(Qt::AlignTop | Qt::AlignRight);
    m_editor = new QPlainTextEdit(editorFrame);
    m_editor->setObjectName("ruleEditor");
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    connect(m_editor, &QPlainTextEdit::textChanged, this, &MainWindow::onRuleEditorTextChanged);
    frameLayout->addWidget(m_ruleLineNumbersLabel);
    frameLayout->addWidget(m_editor, 1);
    editorLayout->addWidget(editorFrame, 1);
    rulesLayout->addWidget(editorCard, 1);

    pageLayout->addLayout(rulesLayout, 1);
    return page;
}

QWidget *MainWindow::buildSettingsInfoPage() {
    auto *page = new QWidget(this);
    page->setObjectName("panelPage");
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(22, 22, 22, 22);
    pageLayout->setSpacing(16);

    auto *panelHead = new QWidget(page);
    auto *headLayout = new QHBoxLayout(panelHead);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(18);

    auto *headCopy = new QVBoxLayout();
    headCopy->setSpacing(6);
    auto *title = new QLabel("Settings / Info", panelHead);
    title->setObjectName("panelTitle");
    auto *subtitle = new QLabel(
        "Technical detail first, then live application configuration editing for the local controller.",
        panelHead);
    subtitle->setObjectName("panelSubtitle");
    subtitle->setWordWrap(true);
    headCopy->addWidget(title);
    headCopy->addWidget(subtitle);
    headLayout->addLayout(headCopy, 1);

    m_settingsPageStatusLabel = new QLabel(panelHead);
    m_settingsPageStatusLabel->setObjectName("statusPill");
    headLayout->addWidget(m_settingsPageStatusLabel, 0, Qt::AlignTop);
    pageLayout->addWidget(panelHead);

    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scrollArea);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(16);

    auto *appInfoCard = new QWidget(content);
    appInfoCard->setObjectName("card");
    auto *appInfoLayout = new QVBoxLayout(appInfoCard);
    appInfoLayout->setContentsMargins(18, 18, 18, 18);
    appInfoLayout->setSpacing(14);
    auto *appInfoTitle = new QLabel("Application info", appInfoCard);
    appInfoTitle->setObjectName("cardTitleStrong");
    auto *appInfoSubtitle = new QLabel("Paths, build metadata, and diagnostic cadence", appInfoCard);
    appInfoSubtitle->setObjectName("cardSubtitle");
    appInfoLayout->addWidget(appInfoTitle);
    appInfoLayout->addWidget(appInfoSubtitle);
    auto *appInfoGrid = new QGridLayout();
    appInfoGrid->setHorizontalSpacing(14);
    appInfoGrid->setVerticalSpacing(10);
    appInfoGrid->setColumnStretch(1, 1);
    m_appBuildValue = buildKeyValueRow(appInfoGrid, 0, "Application", appInfoCard);
    m_infoConfigPathValue = buildKeyValueRow(appInfoGrid, 1, "Config path", appInfoCard);
    m_infoRulesDirValue = buildKeyValueRow(appInfoGrid, 2, "Rules directory", appInfoCard);
    m_infoEndpointValue = buildKeyValueRow(appInfoGrid, 3, "API endpoint", appInfoCard);
    m_infoDiagnosticsValue = buildKeyValueRow(appInfoGrid, 4, "Diagnostics", appInfoCard);
    m_infoProfilesValue = buildKeyValueRow(appInfoGrid, 5, "Profiles", appInfoCard);
    m_infoThemeValue = buildKeyValueRow(appInfoGrid, 6, "Theme source", appInfoCard);
    appInfoLayout->addLayout(appInfoGrid);
    contentLayout->addWidget(appInfoCard);

    auto *stateCard = new QWidget(content);
    stateCard->setObjectName("card");
    auto *stateLayout = new QVBoxLayout(stateCard);
    stateLayout->setContentsMargins(18, 18, 18, 18);
    stateLayout->setSpacing(14);
    auto *stateTitle = new QLabel("Service and system state", stateCard);
    stateTitle->setObjectName("cardTitleStrong");
    auto *stateSubtitle = new QLabel("Live runtime signals available to the current build", stateCard);
    stateSubtitle->setObjectName("cardSubtitle");
    stateLayout->addWidget(stateTitle);
    stateLayout->addWidget(stateSubtitle);
    auto *stateGrid = new QGridLayout();
    stateGrid->setHorizontalSpacing(14);
    stateGrid->setVerticalSpacing(10);
    stateGrid->setColumnStretch(1, 1);
    m_stateApiStatusValue = buildKeyValueRow(stateGrid, 0, "API status", stateCard);
    m_stateCurrentModeValue = buildKeyValueRow(stateGrid, 1, "Current mode", stateCard);
    m_stateLastRefreshValue = buildKeyValueRow(stateGrid, 2, "Last refresh", stateCard);
    m_stateLastDetailValue = buildKeyValueRow(stateGrid, 3, "Last detail", stateCard);
    m_stateExternalIpValue = buildKeyValueRow(stateGrid, 4, "External IP", stateCard);
    m_stateTrafficValue = buildKeyValueRow(stateGrid, 5, "Traffic", stateCard);
    m_stateModeListValue = buildKeyValueRow(stateGrid, 6, "Supported modes", stateCard);
    stateLayout->addLayout(stateGrid);
    contentLayout->addWidget(stateCard);

    auto *editorCard = new QWidget(content);
    editorCard->setObjectName("card");
    editorCard->setMinimumHeight(520);
    auto *editorLayout = new QVBoxLayout(editorCard);
    editorLayout->setContentsMargins(18, 18, 18, 18);
    editorLayout->setSpacing(14);

    auto *editorHead = new QWidget(editorCard);
    auto *editorHeadLayout = new QHBoxLayout(editorHead);
    editorHeadLayout->setContentsMargins(0, 0, 0, 0);
    editorHeadLayout->setSpacing(12);
    auto *editorCopy = new QVBoxLayout();
    editorCopy->setSpacing(4);
    auto *editorTitle = new QLabel("App config", editorHead);
    editorTitle->setObjectName("cardTitleStrong");
    auto *editorSubtitle = new QLabel("Live YAML editor for tunlet config", editorHead);
    editorSubtitle->setObjectName("cardSubtitle");
    editorCopy->addWidget(editorTitle);
    editorCopy->addWidget(editorSubtitle);
    editorHeadLayout->addLayout(editorCopy, 1);
    auto *editorButtons = new QHBoxLayout();
    editorButtons->setSpacing(10);
    auto *saveButton = new QPushButton("Save and apply", editorHead);
    saveButton->setObjectName("primaryButton");
    auto *reloadButton = new QPushButton("Reload", editorHead);
    reloadButton->setObjectName("ghostButton");
    auto *validateButton = new QPushButton("Validate", editorHead);
    validateButton->setObjectName("ghostButton");
    connect(saveButton, &QPushButton::clicked, this, &MainWindow::saveSettingsFile);
    connect(reloadButton, &QPushButton::clicked, this, &MainWindow::reloadSettingsFile);
    connect(validateButton, &QPushButton::clicked, this, &MainWindow::validateSettingsText);
    editorButtons->addWidget(saveButton);
    editorButtons->addWidget(reloadButton);
    editorButtons->addWidget(validateButton);
    editorHeadLayout->addLayout(editorButtons);
    editorLayout->addWidget(editorHead);

    auto *statusFrame = new QWidget(editorCard);
    statusFrame->setObjectName("editorBanner");
    auto *statusLayout = new QHBoxLayout(statusFrame);
    statusLayout->setContentsMargins(12, 10, 12, 10);
    statusLayout->setSpacing(0);
    m_settingsStatusLabel = new QLabel(statusFrame);
    m_settingsStatusLabel->setObjectName("bannerMessage");
    m_settingsStatusLabel->setWordWrap(true);
    statusLayout->addWidget(m_settingsStatusLabel);
    editorLayout->addWidget(statusFrame);

    m_settingsEditor = new QPlainTextEdit(editorCard);
    m_settingsEditor->setObjectName("settingsEditor");
    m_settingsEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_settingsEditor->setMinimumHeight(360);
    connect(m_settingsEditor, &QPlainTextEdit::textChanged, this, &MainWindow::onSettingsEditorTextChanged);
    editorLayout->addWidget(m_settingsEditor, 1);
    contentLayout->addWidget(editorCard, 1);

    scrollArea->setWidget(content);
    pageLayout->addWidget(scrollArea, 1);
    return page;
}

void MainWindow::setCurrentPage(int index) {
    if (!m_pages || index < 0 || index >= m_pages->count()) {
        return;
    }

    m_pages->setCurrentIndex(index);
    for (int buttonIndex = 0; buttonIndex < m_navButtons.size(); ++buttonIndex) {
        if (!m_navButtons.at(buttonIndex)) {
            continue;
        }
        auto *button = m_navButtons.at(buttonIndex).data();
        button->setProperty("active", buttonIndex == index);
        repolish(button);
    }
}

void MainWindow::applyConfig(const config::AppConfig &config) {
    m_config = config;
    m_modeController->updateConfig(m_config);
    m_diagnosticsService->updateConfig(m_config);

    populateModeProfiles();
    populateRuleFiles();
    updateDashboardCards();
    m_modeController->refreshStatus();
    m_diagnosticsService->refreshNow();
}

void MainWindow::populateModeProfiles() {
    const QString preferredSelection = m_selectedProfileName.isEmpty() ? m_lastStatus.currentProfileName : m_selectedProfileName;

    if (m_profileCombo) {
        QSignalBlocker blocker(m_profileCombo);
        m_profileCombo->clear();
        for (const auto &profile : m_modeController->profiles()) {
            m_profileCombo->addItem(displayModeName(profile.name), profile.name);
            const int index = m_profileCombo->count() - 1;
            m_profileCombo->setItemData(index, profile.mode, Qt::UserRole + 1);
            m_profileCombo->setItemData(index, profile.desc, Qt::ToolTipRole);
        }
        m_profileCombo->setEnabled(m_profileCombo->count() > 0 && !m_lastStatus.busy);
    }

    if (!preferredSelection.isEmpty() && hasProfile(preferredSelection)) {
        setSelectedProfileName(preferredSelection);
    } else if (hasProfile(m_lastStatus.currentProfileName)) {
        setSelectedProfileName(m_lastStatus.currentProfileName);
    } else if (hasProfile("auto")) {
        setSelectedProfileName("auto");
    } else if (!m_modeController->profiles().isEmpty()) {
        setSelectedProfileName(m_modeController->profiles().first().name);
    } else {
        setSelectedProfileName({});
    }
}

void MainWindow::populateRuleFiles() {
    const auto *currentFile = selectedRuleFile();
    const QString currentPath = currentFile ? currentFile->path : QString{};

    m_ruleFiles = {
        {"force-proxy", m_config.ruleSets.forceProxyPath, "Default force-proxy rule-set"},
        {"force-direct", m_config.ruleSets.forceDirectPath, "Default force-direct rule-set"},
        {"auto-proxy", m_config.ruleSets.autoProxyPath, "Default auto-proxy rule-set"},
        {"auto-direct", m_config.ruleSets.autoDirectPath, "Default auto-direct rule-set"},
    };

    for (const auto &extra : m_config.ruleSets.extraFiles) {
        m_ruleFiles.push_back({extra.name, extra.path, extra.description});
    }

    if (!m_ruleFileList) {
        return;
    }

    int targetRow = -1;
    {
        QSignalBlocker blocker(m_ruleFileList);
        m_ruleFileList->clear();
        for (const auto &ruleFile : m_ruleFiles) {
            const QString description = ruleFile.description.isEmpty() ? "Editable local rule-set" : ruleFile.description;
            m_ruleFileList->addItem(QString("%1\n%2\n%3").arg(ruleFile.name, description, compactPath(ruleFile.path)));
        }

        for (int row = 0; row < m_ruleFiles.size(); ++row) {
            if (m_ruleFiles.at(row).path == currentPath) {
                targetRow = row;
                break;
            }
        }

        if (targetRow < 0 && !m_ruleFiles.isEmpty()) {
            targetRow = 0;
        }

        if (targetRow >= 0) {
            m_ruleFileList->setCurrentRow(targetRow);
        }
    }

    if (targetRow >= 0) {
        onRuleFileSelectionChanged();
    } else {
        if (m_editor) {
            QSignalBlocker editorBlocker(m_editor);
            m_editor->clear();
        }
        m_loadedRuleText.clear();
        updateRuleLineNumbers();
        setRuleBanner("No files configured", "Add rule-set paths in config.yaml to edit them here.", "warn");
    }
}

void MainWindow::setSelectedProfileName(const QString &profileName) {
    if (!profileName.isEmpty() && !hasProfile(profileName)) {
        return;
    }

    m_selectedProfileName = profileName;
    if (m_profileCombo) {
        QSignalBlocker blocker(m_profileCombo);
        int selectedIndex = -1;
        for (int index = 0; index < m_profileCombo->count(); ++index) {
            if (m_profileCombo->itemData(index).toString() == profileName) {
                selectedIndex = index;
                break;
            }
        }
        m_profileCombo->setCurrentIndex(selectedIndex);
    }
    updateModeSelectionUi();
}

bool MainWindow::hasProfile(const QString &profileName) const {
    for (const auto &profile : m_modeController->profiles()) {
        if (profile.name == profileName) {
            return true;
        }
    }
    return false;
}

QString MainWindow::selectedModeProfileName() const {
    return m_selectedProfileName;
}

const MainWindow::NamedRuleFile *MainWindow::selectedRuleFile() const {
    if (!m_ruleFileList) {
        return nullptr;
    }

    const int row = m_ruleFileList->currentRow();
    if (row < 0 || row >= m_ruleFiles.size()) {
        return nullptr;
    }

    return &m_ruleFiles.at(row);
}

void MainWindow::updateModeSelectionUi() {
    QString profileDescription = "Choose a profile to switch immediately.";
    QString targetModeValue;

    for (const auto &profile : m_modeController->profiles()) {
        if (profile.name == m_selectedProfileName) {
            targetModeValue = profile.mode;
            profileDescription = profile.desc.isEmpty()
                                     ? QString("Switches to backend mode '%1'.").arg(profile.mode)
                                     : QString("%1\nBackend mode: %2").arg(profile.desc, profile.mode);
            break;
        }
    }

    if (!targetModeValue.isEmpty()) {
        profileDescription += QString("\nSupported backend modes: %1").arg(joinOrUnknown(m_lastStatus.supportedModes));
    }

    if (m_profileDescriptionLabel) {
        m_profileDescriptionLabel->setText(profileDescription);
    }
    if (m_profileCombo) {
        m_profileCombo->setEnabled(m_profileCombo->count() > 0 && !m_lastStatus.busy);
    }
}

void MainWindow::updateWindowSizeLabel() {
    if (!m_windowSizeLabel) {
        return;
    }
    m_windowSizeLabel->setText(QString("%1 × %2").arg(width()).arg(height()));
}

void MainWindow::updateRuleLineNumbers() {
    if (!m_editor || !m_ruleLineNumbersLabel) {
        return;
    }

    const int lineCount = qMax(1, m_editor->document()->blockCount());
    QStringList lines;
    lines.reserve(lineCount);
    for (int line = 1; line <= lineCount; ++line) {
        lines.push_back(QString::number(line));
    }
    m_ruleLineNumbersLabel->setText(lines.join('\n'));
}

void MainWindow::showActionMessage(const QString &message, int timeoutMs) {
    if (!message.trimmed().isEmpty() && m_recentActionLabel) {
        m_recentActionLabel->setText(message);
    }
    statusBar()->showMessage(message, timeoutMs);
}

void MainWindow::setStatusPill(QLabel *label, const QString &text, const QString &tone) {
    if (!label) {
        return;
    }
    label->setText(text);
    label->setProperty("tone", tone);
    repolish(label);
}

void MainWindow::setRuleBanner(const QString &title, const QString &message, const QString &tone) {
    if (m_ruleBannerTitleLabel) {
        m_ruleBannerTitleLabel->setText(title);
    }
    if (m_ruleBannerMessageLabel) {
        m_ruleBannerMessageLabel->setText(message);
    }
    setStatusPill(m_ruleBannerStateLabel, tone == "danger" ? "Needs fix" : tone == "warn" ? "Unsaved" : tone == "ok" ? "Valid" : "Ready", tone);
    if (m_ruleBannerTitleLabel && m_ruleBannerTitleLabel->parentWidget()) {
        m_ruleBannerTitleLabel->parentWidget()->setProperty("tone", tone);
        repolish(m_ruleBannerTitleLabel->parentWidget());
    }
}

void MainWindow::setSettingsBanner(const QString &text, const QString &tone) {
    if (m_settingsStatusLabel) {
        m_settingsStatusLabel->setText(text);
    }
    if (m_settingsStatusLabel && m_settingsStatusLabel->parentWidget()) {
        m_settingsStatusLabel->parentWidget()->setProperty("tone", tone);
        repolish(m_settingsStatusLabel->parentWidget());
    }
}

void MainWindow::repolish(QWidget *widget) {
    if (!widget) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void MainWindow::updateDashboardCards() {
    const QString endpoint = m_lastStatus.endpointLabel.isEmpty() ? endpointLabelForConfig(m_config) : m_lastStatus.endpointLabel;
    const QString activeProfile = m_lastStatus.currentProfileName.isEmpty() ? "unknown" : m_lastStatus.currentProfileName;
    const QString rawMode = m_lastStatus.currentModeValue.isEmpty() ? "unknown" : m_lastStatus.currentModeValue;
    const bool reachable = m_lastStatus.reachable;
    const bool busy = m_lastStatus.busy;
    const QString tone = busy ? "neutral" : reachable ? "ok" : "warn";
    const QString apiSummary = busy ? "Refreshing" : reachable ? "API reachable" : "API unavailable";
    const QString detailText = m_lastStatus.detail.isEmpty() ? "Waiting for first refresh." : m_lastStatus.detail;
    const QDateTime lastRefresh = m_lastDiagnostics.lastUpdated.isValid() ? m_lastDiagnostics.lastUpdated : m_lastStatus.lastUpdated;
    const QString refreshText = lastRefreshLabel(lastRefresh);
    const QString proxyIp = !m_lastDiagnostics.proxyIp.isEmpty() ? m_lastDiagnostics.proxyIp : !m_lastDiagnostics.ipv4.isEmpty() ? m_lastDiagnostics.ipv4 : "-";
    const QString locationText = m_lastDiagnostics.location.isEmpty() ? "Location unavailable" : m_lastDiagnostics.location;
    const QString latencyText = m_lastDiagnostics.apiLatencyMs >= 0 ? QString("%1 ms").arg(m_lastDiagnostics.apiLatencyMs) : QString("Unavailable");
    const QString profileHint = m_profileDescriptionLabel ? m_profileDescriptionLabel->text().split('\n').first() : QString("No profile selected");
    const QString trafficText =
        m_lastDiagnostics.trafficAvailable ? QString("%1\n%2").arg(m_lastDiagnostics.trafficSummary, m_lastDiagnostics.trafficDetail)
                                           : m_lastDiagnostics.trafficDetail;
    const QString diagnosticsText = m_config.diagnostics.enabled
                                        ? QString("Enabled · every %1 s").arg(m_config.diagnostics.refreshIntervalMs / 1000.0, 0, 'f', 0)
                                        : QString("Disabled");

    setStatusPill(m_headerReachabilityLabel, apiSummary, tone);
    setStatusPill(m_sidebarStatusLabel, busy ? "Syncing" : reachable ? "Running" : "Offline", tone);
    setStatusPill(m_mainPanelStatusLabel, busy ? "Runtime syncing" : reachable ? "Controller healthy" : "Controller degraded", tone);
    setStatusPill(m_rulePageStatusLabel, "Safe local edits", "warn");
    setStatusPill(m_settingsPageStatusLabel, "Local-only", "neutral");

    if (m_sidebarModeLabel) {
        m_sidebarModeLabel->setText(displayModeName(activeProfile));
    }
    if (m_sidebarModeDetailLabel) {
        m_sidebarModeDetailLabel->setText(QString("%1\nEndpoint %2").arg(detailText, endpoint));
    }
    if (m_sidebarHostSummaryLabel) {
        m_sidebarHostSummaryLabel->setText(
            QString("%1 at %2\nConfig root %3").arg(m_trayAvailable ? "Tray companion available" : "Main window only", endpoint, configRootForConfig(m_config)));
    }

    if (m_currentProfileLabel) {
        m_currentProfileLabel->setText(displayModeName(activeProfile));
    }
    if (m_modeChipLabel) {
        m_modeChipLabel->setText(QString("Mode value: %1").arg(rawMode));
    }

    if (m_serviceStatusValue) {
        m_serviceStatusValue->setText(apiSummary);
    }
    if (m_serviceStatusDetail) {
        m_serviceStatusDetail->setText(QString("%1 · %2").arg(endpoint, detailText));
    }
    if (m_stateModeValue) {
        m_stateModeValue->setText(displayModeName(activeProfile));
    }
    if (m_stateModeDetail) {
        m_stateModeDetail->setText(QString("Backend mode: %1").arg(rawMode));
    }
    if (m_selectedProfileValue) {
        m_selectedProfileValue->setText(displayModeName(m_selectedProfileName.isEmpty() ? activeProfile : m_selectedProfileName));
    }
    if (m_selectedProfileDetail) {
        m_selectedProfileDetail->setText(joinOrUnknown(m_lastStatus.supportedModes));
    }
    if (m_lastReloadValue) {
        m_lastReloadValue->setText(refreshText);
    }
    if (m_lastReloadDetail) {
        m_lastReloadDetail->setText(detailText);
    }

    if (m_connectionEndpointValue) {
        m_connectionEndpointValue->setText(QString("%1 · %2").arg(apiSummary, endpoint));
    }
    if (m_connectionDiagnosticsValue) {
        m_connectionDiagnosticsValue->setText(QString("%1\n%2").arg(latencyText, m_lastDiagnostics.apiDetail));
    }
    if (m_connectionIpValue) {
        const QString ipValue = m_config.diagnostics.externalIp.enabled
                                    ? QString("Proxy %1\nIPv4 %2\nIPv6 %3")
                                          .arg(m_lastDiagnostics.proxyIp.isEmpty() ? "-" : m_lastDiagnostics.proxyIp,
                                               m_lastDiagnostics.ipv4.isEmpty() ? "-" : m_lastDiagnostics.ipv4,
                                               m_lastDiagnostics.ipv6.isEmpty() ? "-" : m_lastDiagnostics.ipv6)
                                    : QString("External IP checks disabled");
        m_connectionIpValue->setText(ipValue);
    }
    if (m_connectionLocationValue) {
        m_connectionLocationValue->setText(QString("%1\n%2").arg(locationText, m_lastDiagnostics.locationDetail));
    }
    if (m_connectionRoutingValue) {
        m_connectionRoutingValue->setText(QString("%1 local rule files active").arg(m_ruleFiles.size()));
    }
    if (m_connectionModesValue) {
        m_connectionModesValue->setText(joinOrUnknown(m_lastStatus.supportedModes));
    }

    if (m_rulesDirectoryValue) {
        m_rulesDirectoryValue->setText(rulesDirectoryForConfig(m_config));
    }
    if (m_configRootValue) {
        m_configRootValue->setText(configRootForConfig(m_config));
    }
    if (m_profileHintValue) {
        m_profileHintValue->setText(profileHint);
    }
    if (m_traySummaryValue) {
        m_traySummaryValue->setText(m_trayAvailable ? "Tray menu available for quick switching" : "No tray host detected on this session");
    }

    if (m_appBuildValue) {
        const QString appValue = QCoreApplication::applicationVersion().isEmpty()
                                     ? QString("%1 · local build").arg(QCoreApplication::applicationName())
                                     : QString("%1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion());
        m_appBuildValue->setText(appValue);
    }
    if (m_infoConfigPathValue) {
        m_infoConfigPathValue->setText(compactPath(m_config.configPath));
    }
    if (m_infoRulesDirValue) {
        m_infoRulesDirValue->setText(rulesDirectoryForConfig(m_config));
    }
    if (m_infoEndpointValue) {
        m_infoEndpointValue->setText(endpoint);
    }
    if (m_infoDiagnosticsValue) {
        m_infoDiagnosticsValue->setText(diagnosticsText);
    }
    if (m_infoProfilesValue) {
        m_infoProfilesValue->setText(QString("%1 available").arg(m_modeController->profiles().size()));
    }
    if (m_infoThemeValue) {
        m_infoThemeValue->setText(m_config.theme.qssPath.isEmpty() ? "Built-in QSS theme" : compactPath(m_config.theme.qssPath));
    }

    if (m_stateApiStatusValue) {
        m_stateApiStatusValue->setText(apiSummary);
    }
    if (m_stateCurrentModeValue) {
        m_stateCurrentModeValue->setText(QString("%1 (%2)").arg(displayModeName(activeProfile), rawMode));
    }
    if (m_stateLastRefreshValue) {
        m_stateLastRefreshValue->setText(refreshText);
    }
    if (m_stateLastDetailValue) {
        m_stateLastDetailValue->setText(detailText);
    }
    if (m_stateExternalIpValue) {
        m_stateExternalIpValue->setText(proxyIp == "-" ? "Unavailable" : QString("%1 · %2").arg(proxyIp, locationText));
    }
    if (m_stateTrafficValue) {
        m_stateTrafficValue->setText(trafficText);
    }
    if (m_stateModeListValue) {
        m_stateModeListValue->setText(joinOrUnknown(m_lastStatus.supportedModes));
    }

    if (m_footerApiValue) {
        m_footerApiValue->setText(apiSummary);
    }
    if (m_footerLatencyValue) {
        m_footerLatencyValue->setText(latencyText);
    }
    if (m_footerModeValue) {
        m_footerModeValue->setText(displayModeName(activeProfile));
    }
    if (m_footerIpValue) {
        m_footerIpValue->setText(proxyIp);
    }
    if (m_footerReloadValue) {
        m_footerReloadValue->setText(refreshText);
    }

    updateModeSelectionUi();
}

void MainWindow::onStatusUpdated(const clash::ModeStatus &status) {
    m_lastStatus = status;
    updateDashboardCards();
}

void MainWindow::onDiagnosticsUpdated(const tunlet::diagnostics::DiagnosticsSnapshot &snapshot) {
    m_lastDiagnostics = snapshot;
    updateDashboardCards();
}

void MainWindow::onRuleFileSelectionChanged() {
    const auto *file = selectedRuleFile();
    if (!file) {
        return;
    }

    const auto result = m_ruleSetService->loadFile(file->path);
    if (!result.ok) {
        if (m_editor) {
            QSignalBlocker blocker(m_editor);
            m_editor->clear();
        }
        m_loadedRuleText.clear();
        updateRuleLineNumbers();
        setRuleBanner("Load failed", result.error, "danger");
        showActionMessage(result.error, 5000);
        return;
    }

    m_loadedRuleText = result.text;
    if (m_editor) {
        QSignalBlocker blocker(m_editor);
        m_editor->setPlainText(result.text);
    }
    updateRuleLineNumbers();
    if (m_ruleEditorTitleLabel) {
        m_ruleEditorTitleLabel->setText(file->name);
    }
    if (m_ruleEditorPathLabel) {
        m_ruleEditorPathLabel->setText(compactPath(file->path));
    }
    setRuleBanner("Loaded file", file->description.isEmpty() ? compactPath(file->path) : file->description, "neutral");
    showActionMessage(QString("Loaded %1").arg(file->name), 3000);
}

void MainWindow::onRuleEditorTextChanged() {
    updateRuleLineNumbers();
    if (!m_editor) {
        return;
    }

    const bool dirty = m_editor->toPlainText() != m_loadedRuleText;
    if (dirty) {
        setRuleBanner("Unsaved changes", "Validate before save to confirm formatting and JSON syntax.", "warn");
    } else {
        setRuleBanner("Saved copy", "Editor content matches the last loaded or saved file.", "neutral");
    }
}

void MainWindow::validateCurrentEditorText() {
    if (!m_editor) {
        return;
    }

    const auto result = m_ruleSetService->validateJson(m_editor->toPlainText());
    if (!result.ok) {
        setRuleBanner("Invalid JSON", result.error, "danger");
        showActionMessage(result.error, 5000);
        return;
    }

    {
        QSignalBlocker blocker(m_editor);
        m_editor->setPlainText(result.formattedText);
    }
    updateRuleLineNumbers();
    const bool dirty = result.formattedText != m_loadedRuleText;
    setRuleBanner("Valid JSON", dirty ? "JSON is valid. Save to write the formatted version." : "JSON is valid and matches disk.", "ok");
    showActionMessage("JSON validated", 3000);
}

void MainWindow::saveCurrentRuleFile() {
    const auto *file = selectedRuleFile();
    if (!file || !m_editor) {
        return;
    }

    const auto validation = m_ruleSetService->validateJson(m_editor->toPlainText());
    if (!validation.ok) {
        setRuleBanner("Invalid JSON", validation.error, "danger");
        showActionMessage(validation.error, 5000);
        QMessageBox::warning(this, "Save failed", validation.error);
        return;
    }

    const auto result = m_ruleSetService->saveFile(file->path, validation.formattedText);
    if (!result.ok) {
        setRuleBanner("Save failed", result.error, "danger");
        showActionMessage(result.error, 5000);
        QMessageBox::warning(this, "Save failed", result.error);
        return;
    }

    m_loadedRuleText = validation.formattedText;
    {
        QSignalBlocker blocker(m_editor);
        m_editor->setPlainText(validation.formattedText);
    }
    updateRuleLineNumbers();
    setRuleBanner("Saved", QString("Wrote %1").arg(compactPath(file->path)), "ok");
    showActionMessage("Rule-set saved", 3000);
}

void MainWindow::reloadCurrentRuleFile() {
    onRuleFileSelectionChanged();
}

void MainWindow::applySelectedModeProfile() {
    const QString name = selectedModeProfileName();
    if (!name.isEmpty()) {
        m_modeController->switchMode(name);
        showActionMessage(QString("Switching to %1").arg(displayModeName(name)), 3000);
    }
}

void MainWindow::onSettingsEditorTextChanged() {
    if (!m_settingsEditor) {
        return;
    }

    const bool dirty = m_settingsEditor->toPlainText() != m_loadedSettingsText;
    if (dirty) {
        setSettingsBanner("Unsaved configuration changes. Validate before applying.", "warn");
    } else {
        setSettingsBanner("Configuration matches the last loaded or saved file.", "neutral");
    }
}

void MainWindow::validateSettingsText() {
    if (!m_settingsEditor) {
        return;
    }

    const auto result = m_configFileService->validateConfigText(m_config.configPath, m_settingsEditor->toPlainText());
    if (!result.ok) {
        setSettingsBanner(result.error, "danger");
        showActionMessage(result.error, 5000);
        return;
    }

    setSettingsBanner("Config is valid and ready to apply.", "ok");
    showActionMessage("Config validated", 3000);
}

void MainWindow::saveSettingsFile() {
    if (!m_settingsEditor) {
        return;
    }

    const QString text = m_settingsEditor->toPlainText();
    const auto validation = m_configFileService->validateConfigText(m_config.configPath, text);
    if (!validation.ok) {
        setSettingsBanner(validation.error, "danger");
        showActionMessage(validation.error, 5000);
        QMessageBox::warning(this, "Config save failed", validation.error);
        return;
    }

    const auto result = m_configFileService->saveFile(m_config.configPath, text);
    if (!result.ok) {
        setSettingsBanner(result.error, "danger");
        showActionMessage(result.error, 5000);
        QMessageBox::warning(this, "Config save failed", result.error);
        return;
    }

    try {
        applyConfig(config::ConfigLoader::loadFromData(text, m_config.configPath));
    } catch (const std::exception &ex) {
        const QString error = QString("Config saved but failed to apply: %1").arg(ex.what());
        setSettingsBanner(error, "warn");
        showActionMessage(error, 5000);
        return;
    }

    m_loadedSettingsText = text;
    setSettingsBanner("Config saved and applied.", "ok");
    showActionMessage("Config saved", 3000);
}

void MainWindow::reloadSettingsFile() {
    if (!m_settingsEditor) {
        return;
    }

    const auto result = m_configFileService->loadFile(m_config.configPath);
    if (!result.ok) {
        QSignalBlocker blocker(m_settingsEditor);
        m_settingsEditor->clear();
        m_loadedSettingsText.clear();
        setSettingsBanner(result.error, "danger");
        showActionMessage(result.error, 5000);
        return;
    }

    {
        QSignalBlocker blocker(m_settingsEditor);
        m_settingsEditor->setPlainText(result.text);
    }
    m_loadedSettingsText = result.text;

    try {
        applyConfig(config::ConfigLoader::loadFromData(result.text, m_config.configPath));
        setSettingsBanner("Loaded and applied current config.yaml.", "ok");
        showActionMessage("Config reloaded", 3000);
    } catch (const std::exception &ex) {
        const QString error = QString("Loaded config text but failed to apply: %1").arg(ex.what());
        setSettingsBanner(error, "warn");
        showActionMessage(error, 5000);
    }
}

}  // namespace tunlet::ui
