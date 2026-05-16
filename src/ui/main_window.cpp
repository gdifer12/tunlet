#include "ui/main_window.hpp"
#include "ui/editor_highlighting.hpp"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QTimer>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFormat>
#include <QVBoxLayout>
#include <QWheelEvent>
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

QString commandName(const config::DiagnosticsCommandConfig &command) {
    const QFileInfo info(command.executable);
    return info.fileName().isEmpty() ? command.executable : info.fileName();
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

QString formatRefreshInterval(int refreshIntervalMs) {
    if (refreshIntervalMs > 0 && refreshIntervalMs % 60000 == 0) {
        return QString("%1 min").arg(refreshIntervalMs / 60000);
    }
    if (refreshIntervalMs > 0 && refreshIntervalMs % 1000 == 0) {
        return QString("%1 s").arg(refreshIntervalMs / 1000);
    }
    return QString("%1 ms").arg(refreshIntervalMs);
}

QString formatDelayValue(qint64 valueMs) {
    return valueMs >= 0 ? QString("%1 ms").arg(valueMs) : QString("Unavailable");
}

QString buildDelayBreakdown(const diagnostics::DiagnosticsSnapshot &snapshot) {
    QStringList parts;
    if (snapshot.delayDnsMs >= 0) {
        parts.push_back(QString("DNS %1 ms").arg(snapshot.delayDnsMs));
    }
    if (snapshot.delayConnectMs >= 0) {
        parts.push_back(QString("Connect %1 ms").arg(snapshot.delayConnectMs));
    }
    if (snapshot.delayTlsMs >= 0) {
        parts.push_back(QString("TLS %1 ms").arg(snapshot.delayTlsMs));
    }
    return parts.isEmpty() ? snapshot.delayDetail : parts.join(" · ");
}

QString compactProbeCommands(const config::AppConfig &config) {
    return QString("IP %1 · Delay %2 · DNS %3")
        .arg(commandName(config.diagnostics.connection.ipv4),
             commandName(config.diagnostics.connection.timing),
             commandName(config.diagnostics.connection.dns));
}

QList<QTextEdit::ExtraSelection> buildEditorErrorSelections(QPlainTextEdit *editor, int line, int column) {
    QList<QTextEdit::ExtraSelection> selections;
    if (!editor || line <= 0) {
        return selections;
    }

    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return selections;
    }

    QTextEdit::ExtraSelection lineSelection;
    lineSelection.format.setBackground(QColor(220, 38, 38, 52));
    lineSelection.format.setProperty(QTextFormat::FullWidthSelection, true);
    lineSelection.cursor = QTextCursor(block);
    lineSelection.cursor.clearSelection();
    selections.push_back(lineSelection);

    if (column > 0) {
        const int blockLength = qMax(0, block.length() - 1);
        const int columnIndex = qBound(0, column - 1, blockLength);
        QTextCursor caretCursor(block);
        caretCursor.setPosition(block.position() + columnIndex);
        if (columnIndex < blockLength) {
            caretCursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        }

        QTextEdit::ExtraSelection caretSelection;
        caretSelection.cursor = caretCursor;
        caretSelection.format.setBackground(QColor(220, 38, 38, 96));
        caretSelection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        caretSelection.format.setUnderlineColor(QColor("#f87171"));
        selections.push_back(caretSelection);
    }

    return selections;
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
    layout->setContentsMargins(12, 10, 12, 10);
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (m_settingsEditor && watched == m_settingsEditor->viewport() && event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        auto *scrollBar = m_settingsEditor->verticalScrollBar();
        if (!scrollBar || scrollBar->maximum() <= scrollBar->minimum()) {
            wheelEvent->accept();
            return true;
        }

        const int deltaY = !wheelEvent->pixelDelta().isNull() ? wheelEvent->pixelDelta().y() : wheelEvent->angleDelta().y();
        if ((deltaY > 0 && scrollBar->value() <= scrollBar->minimum()) ||
            (deltaY < 0 && scrollBar->value() >= scrollBar->maximum())) {
            wheelEvent->accept();
            return true;
        }
    }

    if (m_selectorPopup &&
        (watched == m_selectorPopup || qobject_cast<QAbstractButton *>(watched) != nullptr) &&
        event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const auto buttons =
            m_selectorPopup->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly);
        if (buttons.isEmpty()) {
            if (keyEvent->key() == Qt::Key_Escape) {
                closeSelectorPopup();
                return true;
            }
            return QMainWindow::eventFilter(watched, event);
        }

        auto *currentButton = qobject_cast<QAbstractButton *>(watched);
        if (!currentButton) {
            currentButton = qobject_cast<QAbstractButton *>(m_selectorPopup->focusWidget());
        }

        int currentIndex = buttons.indexOf(currentButton);
        if (currentIndex < 0) {
            currentIndex = 0;
        }

        switch (keyEvent->key()) {
        case Qt::Key_Escape:
            closeSelectorPopup();
            return true;
        case Qt::Key_Down:
        case Qt::Key_Right:
            buttons.at((currentIndex + 1) % buttons.size())->setFocus();
            return true;
        case Qt::Key_Up:
        case Qt::Key_Left:
            buttons.at((currentIndex - 1 + buttons.size()) % buttons.size())->setFocus();
            return true;
        case Qt::Key_Home:
            buttons.first()->setFocus();
            return true;
        case Qt::Key_End:
            buttons.last()->setFocus();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (currentIndex >= 0 && currentIndex < buttons.size()) {
                buttons.at(currentIndex)->click();
                return true;
            }
            break;
        default:
            break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    closeSelectorPopup();
    updateWindowSizeLabel();
    refreshRecentActionLabel();
}

void MainWindow::buildUi(bool trayAvailable) {
    setWindowTitle("tunlet");
    resize(700, 700);
    setMinimumSize(640, 620);

    auto *root = new QWidget(this);
    root->setObjectName("appRoot");
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *windowFrame = new QWidget(root);
    windowFrame->setObjectName("windowFrame");
    auto *windowLayout = new QVBoxLayout(windowFrame);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(buildWindowTitleBar());

    auto *windowBody = new QWidget(windowFrame);
    windowBody->setObjectName("windowBody");
    auto *bodyLayout = new QVBoxLayout(windowBody);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto *contentShell = new QWidget(windowBody);
    contentShell->setObjectName("contentShell");
    auto *contentLayout = new QVBoxLayout(contentShell);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(buildTopRuntimeStrip());

    m_pages = new QStackedWidget(contentShell);
    m_pages->setObjectName("contentPages");
    m_pages->addWidget(buildDashboardPage());
    m_pages->addWidget(buildRuleSetsPage());
    m_pages->addWidget(buildSettingsInfoPage());
    contentLayout->addWidget(m_pages, 1);
    contentLayout->addWidget(buildHealthStrip());
    contentLayout->addWidget(buildBottomNav());
    contentLayout->addWidget(buildBottomStatusLine());
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

    auto *titleMeta = new QVBoxLayout();
    titleMeta->setSpacing(2);
    auto *title = new QLabel("tunlet", titleBar);
    title->setObjectName("titleBarTitle");
    auto *subtitle = new QLabel("Wayland-first local Clash runtime controller", titleBar);
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

QWidget *MainWindow::buildTopRuntimeStrip() {
    auto *shell = new QWidget(this);
    shell->setObjectName("topRuntimeStrip");
    auto *layout = new QVBoxLayout(shell);
    layout->setContentsMargins(14, 8, 14, 6);
    layout->setSpacing(0);

    auto *runtimeRow = new QWidget(shell);
    runtimeRow->setObjectName("topRuntime");
    auto *runtimeLayout = new QHBoxLayout(runtimeRow);
    runtimeLayout->setContentsMargins(12, 9, 12, 9);
    runtimeLayout->setSpacing(10);

    auto *runtimeCopy = new QVBoxLayout();
    runtimeCopy->setSpacing(2);
    auto *runtimeTitle = new QLabel("Primary action: switch Clash mode fast", runtimeRow);
    runtimeTitle->setObjectName("runtimeTitle");
    m_topRuntimeSummaryLabel = new QLabel(runtimeRow);
    m_topRuntimeSummaryLabel->setObjectName("runtimeSummary");
    m_topRuntimeSummaryLabel->setWordWrap(true);
    runtimeCopy->addWidget(runtimeTitle);
    runtimeCopy->addWidget(m_topRuntimeSummaryLabel);
    runtimeLayout->addLayout(runtimeCopy, 1);

    m_topRuntimeStatusLabel = new QLabel(runtimeRow);
    m_topRuntimeStatusLabel->setObjectName("statusPill");
    runtimeLayout->addWidget(m_topRuntimeStatusLabel, 0, Qt::AlignTop);
    layout->addWidget(runtimeRow);
    return shell;
}

QWidget *MainWindow::buildHealthStrip() {
    auto *strip = new QWidget(this);
    strip->setObjectName("healthStrip");

    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(10, 5, 10, 5);
    layout->setSpacing(0);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);

    const QStringList labels = {"Delay", "IP", "DNS", "Last reload"};
    QLabel **targets[] = {
        &m_footerLatencyValue,
        &m_footerIpValue,
        &m_footerDnsValue,
        &m_footerReloadValue,
    };

    for (int index = 0; index < labels.size(); ++index) {
        auto *item = new QWidget(strip);
        item->setObjectName("healthItem");
        auto *itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(10, 7, 10, 7);
        itemLayout->setSpacing(2);
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

    auto *recheckButton = new QToolButton(strip);
    recheckButton->setObjectName("ghostButton");
    recheckButton->setCursor(Qt::PointingHandCursor);
    recheckButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    recheckButton->setIconSize(QSize(16, 16));
    recheckButton->setToolTip("Recheck diagnostics");
    recheckButton->setAutoRaise(false);
    recheckButton->setFixedSize(QSize(34, 34));
    connect(recheckButton, &QToolButton::clicked, this, [this]() {
        m_modeController->refreshStatus();
        m_diagnosticsService->refreshNow();
        showActionMessage("Requested diagnostics recheck", 3000);
    });
    layout->addWidget(recheckButton, 0, Qt::AlignVCenter);
    return strip;
}

QWidget *MainWindow::buildBottomNav() {
    auto *nav = new QWidget(this);
    nav->setObjectName("bottomNav");

    auto *layout = new QHBoxLayout(nav);
    layout->setContentsMargins(12, 5, 12, 5);
    layout->setSpacing(6);

    const struct {
        QString title;
        QString iconPath;
    } sections[] = {
        {"Main", ":/icons/nav-main.svg"},
        {"Rules", ":/icons/nav-rules.svg"},
        {"Settings", ":/icons/nav-settings.svg"},
    };

    constexpr int sectionCount = 3;
    for (int index = 0; index < sectionCount; ++index) {
        auto *button = new QToolButton(nav);
        button->setObjectName("navTabButton");
        button->setProperty("active", false);
        button->setText(sections[index].title);
        button->setIcon(QIcon(sections[index].iconPath));
        button->setIconSize(QSize(16, 16));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setAutoRaise(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_navButtons.push_back(button);
        connect(button, &QToolButton::clicked, this, [this, index]() {
            setCurrentPage(index);
        });
        layout->addWidget(button, 1);
    }

    return nav;
}

QWidget *MainWindow::buildBottomStatusLine() {
    auto *line = new QWidget(this);
    line->setObjectName("bottomStatusLine");
    auto *layout = new QHBoxLayout(line);
    layout->setContentsMargins(14, 4, 14, 5);
    layout->setSpacing(6);

    auto *meta = new QLabel("Recent action", line);
    meta->setObjectName("metaLabel");
    m_recentActionLabel = new QLabel("Ready", line);
    m_recentActionLabel->setObjectName("recentActionValue");
    m_recentActionLabel->setWordWrap(false);

    layout->addWidget(meta, 0, Qt::AlignVCenter);
    layout->addWidget(m_recentActionLabel, 1, Qt::AlignVCenter);
    refreshRecentActionLabel();
    return line;
}

QWidget *MainWindow::buildDashboardPage() {
    auto *page = new QWidget();
    page->setObjectName("panelPage");
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(18, 18, 18, 18);
    pageLayout->setSpacing(14);

    auto *panelHead = new QWidget(page);
    auto *headLayout = new QHBoxLayout(panelHead);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(18);

    auto *headCopy = new QVBoxLayout();
    headCopy->setSpacing(6);
    auto *title = new QLabel("Main control", panelHead);
    title->setObjectName("panelTitle");
    auto *subtitle = new QLabel(
        "More compact, almost tray-like main view: mode first, runtime context directly below it.",
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
    auto *selectorTitle = new QLabel("Connection mode", selectorHead);
    selectorTitle->setObjectName("cardTitleStrong");
    auto *selectorSubtitle = new QLabel("Immediate mode switch stays the main action of the window", selectorHead);
    selectorSubtitle->setObjectName("cardSubtitle");
    selectorSubtitle->setWordWrap(true);
    selectorCopy->addWidget(selectorTitle);
    selectorCopy->addWidget(selectorSubtitle);
    selectorHeadLayout->addLayout(selectorCopy, 1);
    auto *selectorNote = new QLabel("Immediate action", selectorHead);
    selectorNote->setObjectName("metaChip");
    selectorHeadLayout->addWidget(selectorNote, 0, Qt::AlignTop);
    selectorLayout->addWidget(selectorHead);

    m_modeTriggerButton = new QPushButton(selectorCard);
    m_modeTriggerButton->setObjectName("selectorTrigger");
    m_modeTriggerButton->setProperty("open", false);
    m_modeTriggerButton->setMinimumHeight(78);
    m_modeTriggerButton->setCursor(Qt::PointingHandCursor);
    auto *modeTriggerLayout = new QHBoxLayout(m_modeTriggerButton);
    modeTriggerLayout->setContentsMargins(14, 12, 14, 12);
    modeTriggerLayout->setSpacing(12);
    auto *modeTriggerCopy = new QVBoxLayout();
    modeTriggerCopy->setSpacing(4);
    auto *modeTriggerKey = new QLabel("Current mode", m_modeTriggerButton);
    modeTriggerKey->setObjectName("summaryKey");
    m_modeTriggerValueLabel = new QLabel("Unknown", m_modeTriggerButton);
    m_modeTriggerValueLabel->setObjectName("selectorTriggerValue");
    m_modeTriggerSubLabel = new QLabel("Choose a profile to switch immediately.", m_modeTriggerButton);
    m_modeTriggerSubLabel->setObjectName("selectorTriggerSub");
    m_modeTriggerSubLabel->setWordWrap(true);
    modeTriggerCopy->addWidget(modeTriggerKey);
    modeTriggerCopy->addWidget(m_modeTriggerValueLabel);
    modeTriggerCopy->addWidget(m_modeTriggerSubLabel);
    modeTriggerLayout->addLayout(modeTriggerCopy, 1);
    m_modeTriggerCaretLabel = new QLabel("▾", m_modeTriggerButton);
    m_modeTriggerCaretLabel->setObjectName("selectorTriggerCaret");
    modeTriggerLayout->addWidget(m_modeTriggerCaretLabel, 0, Qt::AlignCenter);
    connect(m_modeTriggerButton, &QPushButton::clicked, this, &MainWindow::openModePopup);
    selectorLayout->addWidget(m_modeTriggerButton);
    m_currentProfileLabel = m_modeTriggerValueLabel;
    m_profileDescriptionLabel = m_modeTriggerSubLabel;

    auto *modeSummaryGrid = new QGridLayout();
    modeSummaryGrid->setHorizontalSpacing(10);
    modeSummaryGrid->setVerticalSpacing(10);
    modeSummaryGrid->addWidget(buildSummaryItem(selectorCard, "Selected profile", &m_selectedProfileValue, &m_selectedProfileDetail), 0, 0);
    modeSummaryGrid->addWidget(buildSummaryItem(selectorCard, "Last reload", &m_lastReloadValue, &m_lastReloadDetail), 0, 1);
    selectorLayout->addLayout(modeSummaryGrid);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);
    auto *refreshButton = new QPushButton("Reload config", selectorCard);
    refreshButton->setObjectName("ghostButton");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::reloadSettingsFile);
    auto *reloadButton = new QPushButton("Refresh runtime", selectorCard);
    reloadButton->setObjectName("ghostButton");
    connect(reloadButton, &QPushButton::clicked, this, [this]() {
        m_modeController->refreshStatus();
        m_diagnosticsService->refreshNow();
        showActionMessage("Requested runtime refresh", 3000);
    });
    actionRow->addWidget(refreshButton);
    actionRow->addWidget(reloadButton);
    actionRow->addStretch(1);
    selectorLayout->addLayout(actionRow);
    pageLayout->addWidget(selectorCard);

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
    summaryGrid->setHorizontalSpacing(12);
    summaryGrid->setVerticalSpacing(12);
    summaryGrid->addWidget(buildSummaryItem(stateCard, "Service status", &m_serviceStatusValue, &m_serviceStatusDetail), 0, 0);
    summaryGrid->addWidget(buildSummaryItem(stateCard, "Active mode", &m_stateModeValue, &m_stateModeDetail), 1, 0);
    stateLayout->addLayout(summaryGrid);
    pageLayout->addWidget(stateCard);

    auto *connectionCard = new QWidget(page);
    connectionCard->setObjectName("card");
    auto *connectionLayout = new QVBoxLayout(connectionCard);
    connectionLayout->setContentsMargins(18, 18, 18, 18);
    connectionLayout->setSpacing(14);
    auto *connectionHead = new QWidget(connectionCard);
    auto *connectionHeadLayout = new QHBoxLayout(connectionHead);
    connectionHeadLayout->setContentsMargins(0, 0, 0, 0);
    connectionHeadLayout->setSpacing(12);
    auto *connectionCopy = new QVBoxLayout();
    connectionCopy->setSpacing(4);
    auto *connectionTitle = new QLabel("Connection info", connectionCard);
    connectionTitle->setObjectName("cardTitleStrong");
    auto *connectionSubtitle = new QLabel("Technical details still readable in a narrow window", connectionCard);
    connectionSubtitle->setObjectName("cardSubtitle");
    connectionCopy->addWidget(connectionTitle);
    connectionCopy->addWidget(connectionSubtitle);
    connectionHeadLayout->addLayout(connectionCopy, 1);
    auto *densityLabel = new QLabel("Balanced density", connectionHead);
    densityLabel->setObjectName("metaChip");
    connectionHeadLayout->addWidget(densityLabel, 0, Qt::AlignTop);
    connectionLayout->addWidget(connectionHead);

    auto *metricGrid = new QGridLayout();
    metricGrid->setHorizontalSpacing(10);
    metricGrid->setVerticalSpacing(10);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Clash API", &m_connectionEndpointValue), 0, 0);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Public IP", &m_connectionTunValue), 0, 1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "DNS result", &m_connectionDnsValue), 1, 0);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Delay", &m_connectionDiagnosticsValue), 1, 1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Location", &m_connectionRoutingValue), 2, 0);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Diagnostics config", &m_configRootValue), 2, 1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Controller address", &m_controllerAddressValue), 3, 0);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Rules directory", &m_rulesDirectoryValue), 3, 1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Traffic", &m_profileHintValue), 4, 0, 1, 2);
    connectionLayout->addLayout(metricGrid);
    pageLayout->addWidget(connectionCard);
    pageLayout->addStretch(1);
    return wrapPageInScrollArea(page, "dashboardPageScrollArea");
}

QWidget *MainWindow::buildRuleSetsPage() {
    auto *page = new QWidget();
    page->setObjectName("panelPage");
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(18, 18, 18, 18);
    pageLayout->setSpacing(14);

    auto *panelHead = new QWidget(page);
    auto *headLayout = new QHBoxLayout(panelHead);
    headLayout->setContentsMargins(0, 0, 0, 0);
    headLayout->setSpacing(18);

    auto *headCopy = new QVBoxLayout();
    headCopy->setSpacing(6);
    auto *title = new QLabel("Local rule files", panelHead);
    title->setObjectName("panelTitle");
    auto *subtitle = new QLabel(
        "Compact version: select the file first, then keep one active local JSON editor with validation before save.",
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

    auto *selectorCard = new QWidget(page);
    selectorCard->setObjectName("card");
    auto *selectorLayout = new QVBoxLayout(selectorCard);
    selectorLayout->setContentsMargins(18, 18, 18, 18);
    selectorLayout->setSpacing(12);
    auto *selectorField = new QWidget(selectorCard);
    auto *selectorFieldLayout = new QVBoxLayout(selectorField);
    selectorFieldLayout->setContentsMargins(0, 0, 0, 0);
    selectorFieldLayout->setSpacing(8);
    auto *selectorTitle = new QLabel("Rule file", selectorField);
    selectorTitle->setObjectName("summaryKey");
    selectorFieldLayout->addWidget(selectorTitle);
    m_ruleFileTriggerButton = new QPushButton(selectorCard);
    m_ruleFileTriggerButton->setObjectName("selectorTrigger");
    m_ruleFileTriggerButton->setProperty("open", false);
    m_ruleFileTriggerButton->setMinimumHeight(88);
    m_ruleFileTriggerButton->setCursor(Qt::PointingHandCursor);
    auto *ruleTriggerLayout = new QHBoxLayout(m_ruleFileTriggerButton);
    ruleTriggerLayout->setContentsMargins(14, 12, 14, 12);
    ruleTriggerLayout->setSpacing(12);
    auto *ruleTriggerCopy = new QVBoxLayout();
    ruleTriggerCopy->setSpacing(4);
    m_ruleFileTriggerNameLabel = new QLabel("No file selected", m_ruleFileTriggerButton);
    m_ruleFileTriggerNameLabel->setObjectName("selectorTriggerValue");
    m_ruleFileTriggerDescriptionLabel = new QLabel("Choose the active local JSON file.", m_ruleFileTriggerButton);
    m_ruleFileTriggerDescriptionLabel->setObjectName("selectorTriggerSub");
    m_ruleFileTriggerDescriptionLabel->setWordWrap(true);
    m_ruleFileTriggerPathLabel = new QLabel("", m_ruleFileTriggerButton);
    m_ruleFileTriggerPathLabel->setObjectName("monoNote");
    m_ruleFileTriggerPathLabel->setWordWrap(true);
    ruleTriggerCopy->addWidget(m_ruleFileTriggerNameLabel);
    ruleTriggerCopy->addWidget(m_ruleFileTriggerDescriptionLabel);
    ruleTriggerCopy->addWidget(m_ruleFileTriggerPathLabel);
    ruleTriggerLayout->addLayout(ruleTriggerCopy, 1);
    m_ruleFileTriggerCaretLabel = new QLabel("▾", m_ruleFileTriggerButton);
    m_ruleFileTriggerCaretLabel->setObjectName("selectorTriggerCaret");
    ruleTriggerLayout->addWidget(m_ruleFileTriggerCaretLabel, 0, Qt::AlignCenter);
    connect(m_ruleFileTriggerButton, &QPushButton::clicked, this, &MainWindow::openRuleFilePopup);
    selectorFieldLayout->addWidget(m_ruleFileTriggerButton);
    selectorLayout->addWidget(selectorField);

    auto *fileMeta = new QWidget(selectorCard);
    fileMeta->setObjectName("fileMetaCard");
    auto *fileMetaLayout = new QGridLayout(fileMeta);
    fileMetaLayout->setContentsMargins(12, 12, 12, 12);
    fileMetaLayout->setHorizontalSpacing(10);
    fileMetaLayout->setVerticalSpacing(8);
    auto *descriptionKey = new QLabel("Description", fileMeta);
    descriptionKey->setObjectName("metaLabel");
    m_ruleFileDescriptionLabel = new QLabel(fileMeta);
    m_ruleFileDescriptionLabel->setObjectName("metricValue");
    m_ruleFileDescriptionLabel->setWordWrap(true);
    auto *pathKey = new QLabel("Path", fileMeta);
    pathKey->setObjectName("metaLabel");
    m_ruleFilePathLabel = new QLabel(fileMeta);
    m_ruleFilePathLabel->setObjectName("monoNote");
    m_ruleFilePathLabel->setWordWrap(true);
    fileMetaLayout->addWidget(descriptionKey, 0, 0, Qt::AlignTop);
    fileMetaLayout->addWidget(m_ruleFileDescriptionLabel, 0, 1);
    fileMetaLayout->addWidget(pathKey, 1, 0, Qt::AlignTop);
    fileMetaLayout->addWidget(m_ruleFilePathLabel, 1, 1);
    selectorLayout->addWidget(fileMeta);
    pageLayout->addWidget(selectorCard);

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
    new JsonSyntaxHighlighter(m_editor->document());
    connect(m_editor, &QPlainTextEdit::textChanged, this, &MainWindow::onRuleEditorTextChanged);
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        closeSelectorPopup();
    });
    frameLayout->addWidget(m_ruleLineNumbersLabel);
    frameLayout->addWidget(m_editor, 1);
    editorFrame->setMinimumHeight(320);
    editorLayout->addWidget(editorFrame, 1);
    editorCard->setMinimumHeight(460);
    pageLayout->addWidget(editorCard, 1);
    return wrapPageInScrollArea(page, "rulesPageScrollArea");
}

QWidget *MainWindow::buildSettingsInfoPage() {
    auto *page = new QWidget();
    page->setObjectName("panelPage");
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(18, 18, 18, 18);
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

    auto *appInfoCard = new QWidget(page);
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
    m_infoDiagnosticsValue = buildKeyValueRow(appInfoGrid, 4, "Diagnostics cadence", appInfoCard);
    m_infoProbeCommandsValue = buildKeyValueRow(appInfoGrid, 5, "Probe commands", appInfoCard);
    m_infoGeoDbValue = buildKeyValueRow(appInfoGrid, 6, "Geo DB", appInfoCard);
    m_infoProfilesValue = buildKeyValueRow(appInfoGrid, 7, "Profiles", appInfoCard);
    m_infoThemeValue = buildKeyValueRow(appInfoGrid, 8, "Theme source", appInfoCard);
    appInfoLayout->addLayout(appInfoGrid);
    pageLayout->addWidget(appInfoCard);

    auto *stateCard = new QWidget(page);
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
    m_stateExternalIpValue = buildKeyValueRow(stateGrid, 4, "IP / Location", stateCard);
    m_stateTrafficValue = buildKeyValueRow(stateGrid, 5, "Traffic", stateCard);
    m_stateModeListValue = buildKeyValueRow(stateGrid, 6, "Supported modes", stateCard);
    stateLayout->addLayout(stateGrid);
    pageLayout->addWidget(stateCard);

    auto *editorCard = new QWidget(page);
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
    m_settingsEditor->setMinimumHeight(420);
    new YamlSyntaxHighlighter(m_settingsEditor->document());
    m_settingsEditor->viewport()->installEventFilter(this);
    connect(m_settingsEditor, &QPlainTextEdit::textChanged, this, &MainWindow::onSettingsEditorTextChanged);
    connect(m_settingsEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        closeSelectorPopup();
    });
    editorLayout->addWidget(m_settingsEditor, 1);
    pageLayout->addWidget(editorCard);
    pageLayout->addStretch(1);
    m_settingsPageScrollArea = wrapPageInScrollArea(page, "settingsPageScrollArea");
    return m_settingsPageScrollArea;
}

QScrollArea *MainWindow::wrapPageInScrollArea(QWidget *content, const QString &objectName) {
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName(objectName);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->viewport()->setObjectName("pageViewport");
    scrollArea->setWidget(content);

    connect(scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        closeSelectorPopup();
    });

    return scrollArea;
}

void MainWindow::setCurrentPage(int index) {
    if (!m_pages || index < 0 || index >= m_pages->count()) {
        return;
    }

    closeSelectorPopup();
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
    const QString currentPath = m_selectedRuleFilePath;

    m_ruleFiles = {
        {"force-proxy", m_config.ruleSets.forceProxyPath, "Default force-proxy rule-set"},
        {"force-direct", m_config.ruleSets.forceDirectPath, "Default force-direct rule-set"},
        {"auto-proxy", m_config.ruleSets.autoProxyPath, "Default auto-proxy rule-set"},
        {"auto-direct", m_config.ruleSets.autoDirectPath, "Default auto-direct rule-set"},
    };

    for (const auto &extra : m_config.ruleSets.extraFiles) {
        m_ruleFiles.push_back({extra.name, extra.path, extra.description});
    }

    int targetRow = -1;
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
        m_selectedRuleFilePath = m_ruleFiles.at(targetRow).path;
        onRuleFileSelectionChanged();
    } else {
        updateRuleFileTrigger();
        if (m_editor) {
            QSignalBlocker editorBlocker(m_editor);
            m_editor->clear();
        }
        m_loadedRuleText.clear();
        updateRuleLineNumbers();
        applyEditorErrorHighlight(m_editor, -1, -1);
        setRuleBanner("No files configured", "Add rule-set paths in config.yaml to edit them here.", "warn");
    }
}

void MainWindow::setSelectedProfileName(const QString &profileName) {
    if (!profileName.isEmpty() && !hasProfile(profileName)) {
        return;
    }

    m_selectedProfileName = profileName;
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
    if (m_selectedRuleFilePath.isEmpty()) {
        return nullptr;
    }

    for (const auto &ruleFile : m_ruleFiles) {
        if (ruleFile.path == m_selectedRuleFilePath) {
            return &ruleFile;
        }
    }

    return nullptr;
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

    if (m_modeTriggerValueLabel) {
        const QString displayName = m_selectedProfileName.isEmpty() ? "Unknown" : displayModeName(m_selectedProfileName);
        m_modeTriggerValueLabel->setText(displayName);
    }
    if (m_profileDescriptionLabel) {
        m_profileDescriptionLabel->setText(profileDescription);
    }
    if (m_modeTriggerButton) {
        m_modeTriggerButton->setEnabled(!m_modeController->profiles().isEmpty() && !m_lastStatus.busy);
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

void MainWindow::applyEditorErrorHighlight(QPlainTextEdit *editor, int line, int column) {
    if (!editor) {
        return;
    }
    editor->setExtraSelections(buildEditorErrorSelections(editor, line, column));
}

void MainWindow::updateRuleEditorErrorHighlight() {
    if (!m_editor || !m_ruleSetService) {
        return;
    }

    const auto result = m_ruleSetService->validateJson(m_editor->toPlainText());
    if (!result.ok) {
        applyEditorErrorHighlight(m_editor, result.errorLine, result.errorColumn);
        return;
    }

    applyEditorErrorHighlight(m_editor, -1, -1);
}

void MainWindow::updateSettingsEditorErrorHighlight() {
    if (!m_settingsEditor || !m_configFileService) {
        return;
    }

    const auto result = m_configFileService->validateConfigText(m_config.configPath, m_settingsEditor->toPlainText());
    if (!result.ok) {
        applyEditorErrorHighlight(m_settingsEditor, result.errorLine, result.errorColumn);
        return;
    }

    applyEditorErrorHighlight(m_settingsEditor, -1, -1);
}

void MainWindow::refreshRecentActionLabel() {
    if (!m_recentActionLabel) {
        return;
    }

    m_recentActionLabel->setToolTip(m_recentActionText);
    const int availableWidth = qMax(120, m_recentActionLabel->width() > 0 ? m_recentActionLabel->width() : width() / 2);
    const QString visibleText =
        m_recentActionLabel->fontMetrics().elidedText(m_recentActionText, Qt::ElideRight, availableWidth);
    m_recentActionLabel->setText(visibleText);
}

void MainWindow::closeSelectorPopup() {
    if (m_selectorPopupTrigger) {
        m_selectorPopupTrigger->setProperty("open", false);
        repolish(m_selectorPopupTrigger);
    }

    if (m_selectorPopup) {
        auto *popup = m_selectorPopup;
        m_selectorPopup = nullptr;
        m_selectorPopupTrigger = nullptr;
        popup->close();
        popup->deleteLater();
        return;
    }

    m_selectorPopupTrigger = nullptr;
}

void MainWindow::openModePopup() {
    if (!m_modeTriggerButton || m_modeController->profiles().isEmpty() || m_lastStatus.busy) {
        return;
    }

    if (m_selectorPopup && m_selectorPopupTrigger == m_modeTriggerButton) {
        closeSelectorPopup();
        return;
    }

    closeSelectorPopup();

    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName("selectorPopup");
    popup->setFocusPolicy(Qt::StrongFocus);
    popup->installEventFilter(this);

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->setSizeConstraint(QLayout::SetMinimumSize);

    int activeIndex = 0;
    for (int index = 0; index < m_modeController->profiles().size(); ++index) {
        const auto &profile = m_modeController->profiles().at(index);
        auto *button = new QPushButton(popup);
        button->setObjectName("selectorOption");
        button->setProperty("active", profile.name == m_selectedProfileName);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(64);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->installEventFilter(this);

        auto *buttonLayout = new QVBoxLayout(button);
        buttonLayout->setContentsMargins(12, 10, 12, 10);
        buttonLayout->setSpacing(2);
        buttonLayout->setSizeConstraint(QLayout::SetMinimumSize);

        auto *title = new QLabel(displayModeName(profile.name), button);
        title->setObjectName("selectorOptionTitle");
        auto *subtitle =
            new QLabel(profile.desc.isEmpty() ? QString("Backend mode: %1").arg(profile.mode) : profile.desc, button);
        subtitle->setObjectName("selectorOptionSub");
        auto *meta = new QLabel(QString("Backend mode: %1").arg(profile.mode), button);
        meta->setObjectName("selectorOptionMeta");
        buttonLayout->addWidget(title);
        buttonLayout->addWidget(subtitle);
        buttonLayout->addWidget(meta);

        connect(button, &QPushButton::clicked, this, [this, profileName = profile.name]() {
            closeSelectorPopup();
            setSelectedProfileName(profileName);
            if (!m_lastStatus.busy && profileName != m_lastStatus.currentProfileName) {
                applySelectedModeProfile();
            }
        });

        if (profile.name == m_selectedProfileName) {
            activeIndex = index;
        }

        layout->addWidget(button);
    }

    m_selectorPopup = popup;
    m_selectorPopupTrigger = m_modeTriggerButton;
    m_selectorPopupTrigger->setProperty("open", true);
    repolish(m_selectorPopupTrigger);
    popup->show();
    positionSelectorPopup(m_modeTriggerButton);

    const auto buttons =
        popup->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly);
    if (!buttons.isEmpty()) {
        QTimer::singleShot(0, buttons.at(qBound(0, activeIndex, buttons.size() - 1)), [buttons, activeIndex]() {
            buttons.at(qBound(0, activeIndex, buttons.size() - 1))->setFocus();
        });
    }
}

void MainWindow::openRuleFilePopup() {
    if (!m_ruleFileTriggerButton || m_ruleFiles.isEmpty()) {
        return;
    }

    if (m_selectorPopup && m_selectorPopupTrigger == m_ruleFileTriggerButton) {
        closeSelectorPopup();
        return;
    }

    closeSelectorPopup();

    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName("selectorPopup");
    popup->setFocusPolicy(Qt::StrongFocus);
    popup->installEventFilter(this);

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->setSizeConstraint(QLayout::SetMinimumSize);

    const bool currentDirty = m_editor && m_editor->toPlainText() != m_loadedRuleText;
    const auto validation = (m_editor && currentDirty) ? m_ruleSetService->validateJson(m_editor->toPlainText())
                                                       : rules::ValidationResult{true, QString(), QString()};

    int activeIndex = 0;
    for (int index = 0; index < m_ruleFiles.size(); ++index) {
        const auto &ruleFile = m_ruleFiles.at(index);
        auto *button = new QPushButton(popup);
        button->setObjectName("selectorOption");
        button->setProperty("active", ruleFile.path == m_selectedRuleFilePath);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(74);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->installEventFilter(this);

        auto *buttonLayout = new QVBoxLayout(button);
        buttonLayout->setContentsMargins(12, 10, 12, 10);
        buttonLayout->setSpacing(4);
        buttonLayout->setSizeConstraint(QLayout::SetMinimumSize);

        auto *head = new QWidget(button);
        auto *headLayout = new QHBoxLayout(head);
        headLayout->setContentsMargins(0, 0, 0, 0);
        headLayout->setSpacing(8);
        auto *headCopy = new QVBoxLayout();
        headCopy->setSpacing(4);
        auto *title = new QLabel(ruleFile.name, head);
        title->setObjectName("selectorOptionTitle");
        auto *subtitle = new QLabel(
            ruleFile.description.isEmpty() ? QString("Editable local JSON rule-set") : ruleFile.description,
            head);
        subtitle->setObjectName("selectorOptionSub");
        headCopy->addWidget(title);
        headCopy->addWidget(subtitle);
        headLayout->addLayout(headCopy, 1);

        auto *badge = new QLabel(head);
        badge->setObjectName("statusPill");
        QString badgeText = "Saved";
        QString badgeTone = "neutral";
        if (ruleFile.path == m_selectedRuleFilePath) {
            if (currentDirty) {
                badgeText = validation.ok ? "Unsaved" : "Needs fix";
                badgeTone = validation.ok ? "warn" : "danger";
            } else {
                badgeText = "Ready";
                badgeTone = "ok";
            }
            activeIndex = index;
        }
        badge->setText(badgeText);
        badge->setProperty("tone", badgeTone);
        repolish(badge);
        headLayout->addWidget(badge, 0, Qt::AlignTop);

        auto *path = new QLabel(compactPath(ruleFile.path), button);
        path->setObjectName("selectorOptionMeta");

        buttonLayout->addWidget(head);
        buttonLayout->addWidget(path);

        connect(button, &QPushButton::clicked, this, [this, pathValue = ruleFile.path]() {
            closeSelectorPopup();
            m_selectedRuleFilePath = pathValue;
            onRuleFileSelectionChanged();
        });

        layout->addWidget(button);
    }

    m_selectorPopup = popup;
    m_selectorPopupTrigger = m_ruleFileTriggerButton;
    m_selectorPopupTrigger->setProperty("open", true);
    repolish(m_selectorPopupTrigger);
    popup->show();
    positionSelectorPopup(m_ruleFileTriggerButton);

    const auto buttons =
        popup->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly);
    if (!buttons.isEmpty()) {
        QTimer::singleShot(0, buttons.at(qBound(0, activeIndex, buttons.size() - 1)), [buttons, activeIndex]() {
            buttons.at(qBound(0, activeIndex, buttons.size() - 1))->setFocus();
        });
    }
}

void MainWindow::positionSelectorPopup(QWidget *trigger) {
    if (!m_selectorPopup || !trigger) {
        return;
    }

    QPoint anchor = trigger->mapToGlobal(QPoint(0, trigger->height() + 8));
    QRect available = trigger->screen() ? trigger->screen()->availableGeometry()
                                        : QGuiApplication::primaryScreen()->availableGeometry();

    const int minWidth = qMax(trigger->width(), 320);
    const int maxWidth = qMax(minWidth, qMin(available.width() - 24, 460));
    const int hintedWidth = qMax(minWidth, m_selectorPopup->sizeHint().width());
    const int width = qMin(hintedWidth, maxWidth);

    m_selectorPopup->setMinimumWidth(width);
    m_selectorPopup->resize(width, m_selectorPopup->sizeHint().height());

    int x = anchor.x();
    int y = anchor.y();

    if (x + m_selectorPopup->width() > available.right()) {
        x = available.right() - m_selectorPopup->width();
    }
    if (x < available.left()) {
        x = available.left();
    }
    if (y + m_selectorPopup->height() > available.bottom()) {
        y = trigger->mapToGlobal(QPoint(0, -m_selectorPopup->height() - 8)).y();
    }
    if (y < available.top()) {
        y = available.top();
    }

    m_selectorPopup->move(x, y);
}

void MainWindow::updateRuleFileTrigger() {
    const auto *file = selectedRuleFile();
    const QString emptyDescription = "Choose the active local JSON file.";

    if (m_ruleFileTriggerButton) {
        m_ruleFileTriggerButton->setEnabled(!m_ruleFiles.isEmpty());
    }
    if (m_ruleFileTriggerNameLabel) {
        m_ruleFileTriggerNameLabel->setText(file ? file->name : QString("No file selected"));
    }
    if (m_ruleFileTriggerDescriptionLabel) {
        m_ruleFileTriggerDescriptionLabel->setText(
            file ? (file->description.isEmpty() ? QString("Editable local JSON rule-set") : file->description)
                 : emptyDescription);
    }
    if (m_ruleFileTriggerPathLabel) {
        m_ruleFileTriggerPathLabel->setText(file ? compactPath(file->path) : QString());
    }
}

void MainWindow::showActionMessage(const QString &message, int timeoutMs) {
    if (!message.trimmed().isEmpty()) {
        m_recentActionText = message;
        refreshRecentActionLabel();
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
    const QString publicIp = m_lastDiagnostics.publicIp.isEmpty() ? "-" : m_lastDiagnostics.publicIp;
    const QString locationText = m_lastDiagnostics.location.isEmpty() ? "Location unavailable" : m_lastDiagnostics.location;
    const QString delayText = formatDelayValue(m_lastDiagnostics.delayTotalMs);
    const QString trafficText =
        m_lastDiagnostics.trafficAvailable ? QString("%1\n%2").arg(m_lastDiagnostics.trafficSummary, m_lastDiagnostics.trafficDetail)
                                           : m_lastDiagnostics.trafficDetail;
    const QString diagnosticsText = m_config.diagnostics.enabled
                                        ? QString("Every %1 · timeout %2 ms")
                                              .arg(formatRefreshInterval(m_config.diagnostics.refreshIntervalMs))
                                              .arg(m_config.diagnostics.requestTimeoutMs)
                                        : QString("Disabled");
    const QString dnsText = m_lastDiagnostics.dnsSummary;
    const QString connectionApiText = QString("%1 · %2").arg(apiSummary, endpoint);
    const QString delayDetailText = buildDelayBreakdown(m_lastDiagnostics);
    const QString diagnosticsConfigText = m_lastDiagnostics.configurationSummary.isEmpty()
                                              ? compactProbeCommands(m_config)
                                              : m_lastDiagnostics.configurationSummary;
    const QString lastDetailText = !m_lastDiagnostics.externalDetail.trimmed().isEmpty() &&
                                           m_lastDiagnostics.externalDetail != "Refreshing connection diagnostics"
                                       ? m_lastDiagnostics.externalDetail
                                       : detailText;

    setStatusPill(m_headerReachabilityLabel, apiSummary, tone);
    setStatusPill(m_topRuntimeStatusLabel, busy ? "Syncing" : reachable ? "Running" : "Offline", tone);
    setStatusPill(m_mainPanelStatusLabel, busy ? "Runtime syncing" : reachable ? "Clash healthy" : "Controller degraded", tone);
    setStatusPill(m_rulePageStatusLabel, "Safe local edits", "warn");
    setStatusPill(m_settingsPageStatusLabel, "Local-only", "neutral");

    if (m_topRuntimeSummaryLabel) {
        m_topRuntimeSummaryLabel->setText(
            QString("%1 active · clash API on %2 · %3").arg(displayModeName(activeProfile),
                                                            endpoint,
                                                            m_trayAvailable ? "tray companion available" : "local-only runtime"));
    }

    if (m_serviceStatusValue) {
        m_serviceStatusValue->setText(reachable ? "clash API reachable" : apiSummary);
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
        m_selectedProfileDetail->setText("Current path chosen by controller");
    }
    if (m_lastReloadValue) {
        m_lastReloadValue->setText(refreshText);
    }
    if (m_lastReloadDetail) {
        m_lastReloadDetail->setText(reachable ? "Last combined diagnostics refresh" : detailText);
    }

    if (m_connectionEndpointValue) {
        m_connectionEndpointValue->setText(connectionApiText);
    }
    if (m_connectionDiagnosticsValue) {
        m_connectionDiagnosticsValue->setText(QString("%1 · %2").arg(delayText, delayDetailText));
    }
    if (m_connectionTunValue) {
        m_connectionTunValue->setText(publicIp == "-" ? "Unavailable" : publicIp);
    }
    if (m_connectionDnsValue) {
        m_connectionDnsValue->setText(dnsText);
    }
    if (m_connectionRoutingValue) {
        m_connectionRoutingValue->setText(locationText);
    }
    if (m_controllerAddressValue) {
        m_controllerAddressValue->setText(endpoint);
    }

    if (m_rulesDirectoryValue) {
        m_rulesDirectoryValue->setText(rulesDirectoryForConfig(m_config));
    }
    if (m_configRootValue) {
        m_configRootValue->setText(QString("%1\n%2").arg(diagnosticsConfigText, m_lastDiagnostics.configurationDetail));
    }
    if (m_profileHintValue) {
        m_profileHintValue->setText(trafficText);
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
    if (m_infoProbeCommandsValue) {
        m_infoProbeCommandsValue->setText(diagnosticsConfigText);
    }
    if (m_infoGeoDbValue) {
        m_infoGeoDbValue->setText(compactPath(m_config.diagnostics.connection.location.databasePath));
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
        m_stateLastDetailValue->setText(lastDetailText);
    }
    if (m_stateExternalIpValue) {
        m_stateExternalIpValue->setText(publicIp == "-" ? "Unavailable" : QString("%1 · %2").arg(publicIp, locationText));
    }
    if (m_stateTrafficValue) {
        m_stateTrafficValue->setText(trafficText);
    }
    if (m_stateModeListValue) {
        m_stateModeListValue->setText(joinOrUnknown(m_lastStatus.supportedModes));
    }

    if (m_footerLatencyValue) {
        m_footerLatencyValue->setText(delayText);
    }
    if (m_footerIpValue) {
        m_footerIpValue->setText(publicIp);
    }
    if (m_footerDnsValue) {
        m_footerDnsValue->setText(dnsText);
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
    updateRuleFileTrigger();
    if (!file) {
        return;
    }

    if (m_ruleFileDescriptionLabel) {
        m_ruleFileDescriptionLabel->setText(file->description.isEmpty() ? "Editable local JSON rule-set" : file->description);
    }
    if (m_ruleFilePathLabel) {
        m_ruleFilePathLabel->setText(compactPath(file->path));
    }

    const auto result = m_ruleSetService->loadFile(file->path);
    if (!result.ok) {
        if (m_editor) {
            QSignalBlocker blocker(m_editor);
            m_editor->clear();
        }
        m_loadedRuleText.clear();
        updateRuleLineNumbers();
        applyEditorErrorHighlight(m_editor, -1, -1);
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
    updateRuleEditorErrorHighlight();
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

    updateRuleEditorErrorHighlight();

    const bool dirty = m_editor->toPlainText() != m_loadedRuleText;
    if (dirty) {
        setRuleBanner("Unsaved changes", "Validate before save to confirm formatting and JSON syntax.", "warn");
    } else {
        setRuleBanner("Saved copy", "Editor content matches the last loaded or saved file.", "neutral");
    }
    updateRuleFileTrigger();
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
    applyEditorErrorHighlight(m_editor, -1, -1);
    const bool dirty = result.formattedText != m_loadedRuleText;
    setRuleBanner("Valid JSON", dirty ? "JSON is valid. Save to write the formatted version." : "JSON is valid and matches disk.", "ok");
    showActionMessage("JSON validated", 3000);
    updateRuleFileTrigger();
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
    applyEditorErrorHighlight(m_editor, -1, -1);
    setRuleBanner("Saved", QString("Wrote %1").arg(compactPath(file->path)), "ok");
    showActionMessage("Rule-set saved", 3000);
    updateRuleFileTrigger();
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

    updateSettingsEditorErrorHighlight();

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
    applyEditorErrorHighlight(m_settingsEditor, -1, -1);
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
    applyEditorErrorHighlight(m_settingsEditor, -1, -1);
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
        applyEditorErrorHighlight(m_settingsEditor, -1, -1);
        setSettingsBanner(result.error, "danger");
        showActionMessage(result.error, 5000);
        return;
    }

    {
        QSignalBlocker blocker(m_settingsEditor);
        m_settingsEditor->setPlainText(result.text);
    }
    m_loadedSettingsText = result.text;
    updateSettingsEditorErrorHighlight();

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
