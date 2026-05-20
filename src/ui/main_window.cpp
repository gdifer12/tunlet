#include "ui/main_window.hpp"
#include "ui/editor_highlighting.hpp"
#include "ui/mode_selection_state.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QColor>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostAddress>
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
#include <QSizePolicy>
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
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

namespace tunlet::ui {

namespace {

constexpr int kDashboardPageIndex = 0;
constexpr int kRulesPageIndex = 1;
constexpr int kSettingsPageIndex = 2;

QString endpointLabelForConfig(const config::AppConfig &config) {
    return QString("%1:%2").arg(config.clashApi.host).arg(config.clashApi.port);
}

bool hasUsableIpAddress(const QString &value) {
    if (value.trimmed().isEmpty()) {
        return false;
    }

    QHostAddress address;
    return address.setAddress(value.trimmed());
}

QString flagEmojiForCountryCode(QString countryCode) {
    countryCode = countryCode.trimmed().toUpper();
    if (countryCode.size() != 2) {
        return {};
    }

    QString flag;
    flag.reserve(4);
    for (const QChar ch : countryCode) {
        if (ch < QChar('A') || ch > QChar('Z')) {
            return {};
        }
        const char32_t regionalIndicator = 0x1F1E6 + (ch.unicode() - 'A');
        flag.append(QString::fromUcs4(&regionalIndicator, 1));
    }
    return flag;
}

QString formatLocationBadgeText(const diagnostics::DiagnosticsSnapshot &snapshot) {
    const QString countryCode = snapshot.locationCountryCode.trimmed().toUpper();
    if (countryCode.isEmpty()) {
        return "N/A";
    }

    const QString flag = flagEmojiForCountryCode(countryCode);
    return flag.isEmpty() ? countryCode : QString("%1 %2").arg(countryCode, flag);
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

QString formatDiagnosticTimestamp(const QDateTime &time) {
    return time.isValid() ? time.toLocalTime().toString("yyyy-MM-dd HH:mm") : QString("Unavailable");
}

QString formatLocationSourceText(const diagnostics::DiagnosticsSnapshot &snapshot) {
    if (snapshot.locationDisabled) {
        return "Disabled";
    }

    QString text = snapshot.locationSource.trimmed();
    if (text.isEmpty()) {
        text = "Unavailable";
    }
    if (snapshot.locationStale && !text.contains("stale", Qt::CaseInsensitive)) {
        text += ", stale";
    }
    return text;
}

QString formatLoggingStatusText(const logging::LoggingStatus &status) {
    if (!status.enabled) {
        return "Disabled";
    }
    if (status.textSinkActive || status.jsonlSinkActive) {
        return status.lastError.trimmed().isEmpty() ? QString("Active") : QString("Degraded");
    }
    return "Unavailable";
}

QString formatLoggingSinkText(const QString &path, bool active, bool enabled) {
    if (path.trimmed().isEmpty()) {
        return "Off";
    }
    if (!enabled) {
        return QString("Configured (%1)").arg(compactPath(path));
    }
    return active ? compactPath(path) : QString("Unavailable (%1)").arg(compactPath(path));
}

QString compactProbeCommands(const config::AppConfig &config) {
    return QString("IP %1 · Delay %2 · DNS %3")
        .arg(commandName(config.diagnostics.connection.ipv4),
             commandName(config.diagnostics.connection.timing),
             commandName(config.diagnostics.connection.dns));
}

bool isInformationalLabelCandidate(QLabel *label) {
    if (!label) {
        return false;
    }

    static const QStringList excludedObjectNames = {
        "statusPill",
        "metaChip",
        "selectorTriggerCaret",
        "lineNumbers",
        "healthInlineBadge",
    };
    if (excludedObjectNames.contains(label->objectName())) {
        return false;
    }

    for (QWidget *ancestor = label->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (qobject_cast<QAbstractButton *>(ancestor)) {
            return false;
        }
    }

    return true;
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

QWidget *buildDelayMetricItem(QWidget *parent,
                              QLabel **totalValueLabel,
                              QLabel **dnsValueLabel,
                              QLabel **connectValueLabel,
                              QLabel **tlsValueLabel) {
    auto *item = new QWidget(parent);
    item->setObjectName("metricItem");

    auto *layout = new QVBoxLayout(item);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *label = new QLabel("Delay", item);
    label->setObjectName("metricLabel");
    layout->addWidget(label);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);

    auto addCell = [item, grid](int row, int column, const QString &key, QLabel **target, const QString &valueObjectName) {
        auto *cell = new QWidget(item);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(2);

        auto *meta = new QLabel(key, cell);
        meta->setObjectName("metaLabel");
        *target = new QLabel(cell);
        (*target)->setObjectName(valueObjectName);
        (*target)->setWordWrap(false);
        (*target)->setTextFormat(Qt::PlainText);

        cellLayout->addWidget(meta);
        cellLayout->addWidget(*target);
        grid->addWidget(cell, row, column);
    };

    addCell(0, 0, "Total", totalValueLabel, "metricValueLead");
    addCell(0, 1, "DNS", dnsValueLabel, "metricValueCompact");
    addCell(1, 0, "Connect", connectValueLabel, "metricValueCompact");
    addCell(1, 1, "TLS", tlsValueLabel, "metricValueCompact");
    layout->addLayout(grid);
    return item;
}

QWidget *buildLocationMetricItem(QWidget *parent,
                                 QLabel **summaryValueLabel,
                                 QLabel **asnOrgValueLabel,
                                 QLabel **sourceValueLabel,
                                 QLabel **nextRefreshValueLabel) {
    auto *item = new QWidget(parent);
    item->setObjectName("metricItem");

    auto *layout = new QVBoxLayout(item);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    auto *label = new QLabel("Location", item);
    label->setObjectName("metricLabel");
    layout->addWidget(label);

    *summaryValueLabel = new QLabel(item);
    (*summaryValueLabel)->setObjectName("metricValueLead");
    (*summaryValueLabel)->setWordWrap(true);
    (*summaryValueLabel)->setTextFormat(Qt::PlainText);
    layout->addWidget(*summaryValueLabel);

    auto addDetailRow = [item, layout](const QString &key, QLabel **target) {
        auto *row = new QWidget(item);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(2);

        auto *meta = new QLabel(key, row);
        meta->setObjectName("metaLabel");
        *target = new QLabel(row);
        (*target)->setObjectName("metricValueCompact");
        (*target)->setWordWrap(true);
        (*target)->setTextFormat(Qt::PlainText);

        rowLayout->addWidget(meta);
        rowLayout->addWidget(*target);
        layout->addWidget(row);
    };

    addDetailRow("ASN / Org", asnOrgValueLabel);
    addDetailRow("Source", sourceValueLabel);
    addDetailRow("Next refresh", nextRefreshValueLabel);
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
                       app::RuntimeConfigApplier *runtimeConfigApplier,
                       logging::LoggingService *loggingService,
                       bool trayAvailable,
                       QWidget *parent)
    : QMainWindow(parent),
      m_config(config),
      m_modeController(modeController),
      m_configFileService(configFileService),
      m_diagnosticsService(diagnosticsService),
      m_loggingService(loggingService),
      m_ruleSetService(ruleSetService),
      m_runtimeConfigApplier(runtimeConfigApplier),
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
        setModeBanner("Mode switch failed", message, "danger");
        showActionMessage(message, 5000);
        updateDashboardCards();
    });
    connect(m_diagnosticsService, &diagnostics::DiagnosticsService::diagnosticsUpdated, this, &MainWindow::onDiagnosticsUpdated);
    if (m_loggingService) {
        connect(m_loggingService, &logging::LoggingService::statusChanged, this, [this](const logging::LoggingStatus &status) {
            onLoggingStatusChanged(status, true);
        });
        onLoggingStatusChanged(m_loggingService->status(), true);
    }

    onStatusUpdated(m_modeController->status());
    onDiagnosticsUpdated(m_diagnosticsService->snapshot());
}

void MainWindow::showAndRaise() {
    show();
    if (isMinimized()) {
        setWindowState(windowState() & ~Qt::WindowMinimized);
    }
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_trayAvailable && m_config.tray.keepRunningWithoutWindow) {
        closeSelectorPopup();
        hide();
        if (m_loggingService) {
            m_loggingService->logInfo("tray", "Main window hidden to tray");
        }
        showActionMessage("Main window hidden to tray", 3000);
        event->ignore();
        return;
    }

    QMainWindow::closeEvent(event);
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
    refreshAppConfigPathLabel();
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

    statusBar()->hide();
    setCurrentPage(kDashboardPageIndex);
    updateWindowSizeLabel();
    updateInformationalLabelSelection();
    rebuildShortcuts();
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
    layout->setContentsMargins(10, 3, 10, 3);
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
        itemLayout->setContentsMargins(10, 5, 10, 5);
        itemLayout->setSpacing(1);
        if (labels.at(index) == "IP") {
            m_footerIpContent = new QWidget(item);
            m_footerIpContent->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            auto *contentLayout = new QVBoxLayout(m_footerIpContent);
            contentLayout->setContentsMargins(0, 0, 0, 0);
            contentLayout->setSpacing(1);

            auto *head = new QWidget(m_footerIpContent);
            auto *headLayout = new QHBoxLayout(head);
            headLayout->setContentsMargins(0, 0, 0, 0);
            headLayout->setSpacing(6);
            m_footerIpMetaLabel = new QLabel(labels.at(index), head);
            m_footerIpMetaLabel->setObjectName("metaLabel");
            m_footerIpLocationBadgeLabel = new QLabel("N/A", head);
            m_footerIpLocationBadgeLabel->setObjectName("healthInlineBadge");
            m_footerIpLocationBadgeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            headLayout->addWidget(m_footerIpMetaLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
            headLayout->addStretch(1);
            headLayout->addWidget(m_footerIpLocationBadgeLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            contentLayout->addWidget(head);

            *targets[index] = new QLabel(m_footerIpContent);
            (*targets[index])->setObjectName("healthValue");
            (*targets[index])->setWordWrap(false);
            contentLayout->addWidget(*targets[index], 0, Qt::AlignLeft);

            itemLayout->addWidget(m_footerIpContent, 0, Qt::AlignLeft);
        } else {
            auto *meta = new QLabel(labels.at(index), item);
            meta->setObjectName("metaLabel");
            itemLayout->addWidget(meta);
            *targets[index] = new QLabel(item);
            (*targets[index])->setObjectName("healthValue");
            (*targets[index])->setWordWrap(true);
            itemLayout->addWidget(*targets[index]);
        }
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
        if (m_loggingService) {
            m_loggingService->logInfo("ui.runtime", "Requested diagnostics recheck");
        }
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
    layout->setContentsMargins(14, 2, 14, 3);
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
    headCopy->setSpacing(0);
    auto *title = new QLabel("Main control", panelHead);
    title->setObjectName("panelTitle");
    headCopy->addWidget(title);
    headLayout->addLayout(headCopy, 1);

    m_mainPanelStatusLabel = new QLabel(panelHead);
    m_mainPanelStatusLabel->setObjectName("statusPill");
    headLayout->addWidget(m_mainPanelStatusLabel, 0, Qt::AlignTop);
    pageLayout->addWidget(panelHead);

    auto *selectorCard = new QWidget(page);
    selectorCard->setObjectName("card");
    auto *selectorLayout = new QVBoxLayout(selectorCard);
    selectorLayout->setContentsMargins(18, 14, 18, 14);
    selectorLayout->setSpacing(8);

    auto *selectorHead = new QWidget(selectorCard);
    auto *selectorHeadLayout = new QHBoxLayout(selectorHead);
    selectorHeadLayout->setContentsMargins(0, 0, 0, 0);
    selectorHeadLayout->setSpacing(12);
    auto *selectorCopy = new QVBoxLayout();
    selectorCopy->setSpacing(2);
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
    m_modeTriggerButton->setMinimumHeight(72);
    m_modeTriggerButton->setCursor(Qt::PointingHandCursor);
    auto *modeTriggerLayout = new QHBoxLayout(m_modeTriggerButton);
    modeTriggerLayout->setContentsMargins(14, 8, 14, 8);
    modeTriggerLayout->setSpacing(12);
    auto *modeTriggerCopy = new QVBoxLayout();
    modeTriggerCopy->setSpacing(2);
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
    modeSummaryGrid->setVerticalSpacing(6);
    modeSummaryGrid->addWidget(buildSummaryItem(selectorCard, "Selected profile", &m_selectedProfileValue, &m_selectedProfileDetail), 0, 0);
    modeSummaryGrid->addWidget(buildSummaryItem(selectorCard, "Last reload", &m_lastReloadValue, &m_lastReloadDetail), 0, 1);
    selectorLayout->addLayout(modeSummaryGrid);

    auto *modeBanner = new QWidget(selectorCard);
    modeBanner->setObjectName("editorBanner");
    modeBanner->setProperty("tone", "neutral");
    auto *modeBannerLayout = new QHBoxLayout(modeBanner);
    modeBannerLayout->setContentsMargins(14, 12, 14, 12);
    modeBannerLayout->setSpacing(12);
    auto *modeBannerCopy = new QVBoxLayout();
    modeBannerCopy->setSpacing(4);
    m_modeBannerTitleLabel = new QLabel("Mode synchronized", modeBanner);
    m_modeBannerTitleLabel->setObjectName("bannerTitle");
    m_modeBannerMessageLabel = new QLabel("Visible mode selection matches the backend state.", modeBanner);
    m_modeBannerMessageLabel->setObjectName("bannerMessage");
    m_modeBannerMessageLabel->setWordWrap(true);
    modeBannerCopy->addWidget(m_modeBannerTitleLabel);
    modeBannerCopy->addWidget(m_modeBannerMessageLabel);
    modeBannerLayout->addLayout(modeBannerCopy, 1);
    m_modeBannerStateLabel = new QLabel(modeBanner);
    m_modeBannerStateLabel->setObjectName("statusPill");
    modeBannerLayout->addWidget(m_modeBannerStateLabel, 0, Qt::AlignTop);
    selectorLayout->addWidget(modeBanner);

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);
    auto *refreshButton = new QPushButton("Reload config", selectorCard);
    refreshButton->setObjectName("ghostButton");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::reloadSettingsFile);
    auto *reloadButton = new QPushButton("Refresh runtime", selectorCard);
    reloadButton->setObjectName("ghostButton");
    connect(reloadButton, &QPushButton::clicked, this, &MainWindow::refreshRuntime);
    m_refreshLocationDataButton = new QPushButton("Refresh location data", selectorCard);
    m_refreshLocationDataButton->setObjectName("ghostButton");
    m_refreshLocationDataButton->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    connect(m_refreshLocationDataButton, &QPushButton::clicked, this, [this]() {
        m_diagnosticsService->refreshLocationDataNow();
        if (m_loggingService) {
            m_loggingService->logInfo("ui.runtime", "Requested location data refresh");
        }
        showActionMessage("Requested location data refresh", 3000);
    });
    actionRow->addWidget(refreshButton);
    actionRow->addWidget(reloadButton);
    actionRow->addWidget(m_refreshLocationDataButton);
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
    stateCopy->setSpacing(0);
    auto *stateTitle = new QLabel("Current state", stateHead);
    stateTitle->setObjectName("cardTitleStrong");
    stateCopy->addWidget(stateTitle);
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
    connectionCopy->setSpacing(0);
    auto *connectionTitle = new QLabel("Connection info", connectionCard);
    connectionTitle->setObjectName("cardTitleStrong");
    connectionCopy->addWidget(connectionTitle);
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
    metricGrid->addWidget(
        buildLocationMetricItem(connectionCard,
                                &m_connectionRoutingValue,
                                &m_controllerAddressValue,
                                &m_rulesDirectoryValue,
                                &m_configRootValue),
        1,
        0,
        2,
        1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "DNS result", &m_connectionDnsValue), 1, 1);
    metricGrid->addWidget(
        buildDelayMetricItem(connectionCard,
                             &m_connectionDiagnosticsValue,
                             &m_connectionDelayDnsValue,
                             &m_connectionDelayConnectValue,
                             &m_connectionDelayTlsValue),
        2,
        1);
    metricGrid->addWidget(buildMetricItem(connectionCard, "Traffic", &m_profileHintValue), 3, 0, 1, 2);
    metricGrid->setRowStretch(1, 1);
    metricGrid->setRowStretch(2, 1);
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
    headCopy->setSpacing(0);
    auto *title = new QLabel("Local rule files", panelHead);
    title->setObjectName("panelTitle");
    headCopy->addWidget(title);
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
    headCopy->setSpacing(0);
    auto *title = new QLabel("Settings / Info", panelHead);
    title->setObjectName("panelTitle");
    headCopy->addWidget(title);
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
    appInfoLayout->addWidget(appInfoTitle);
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
    m_infoGeoDbValue = buildKeyValueRow(appInfoGrid, 6, "GeoIP backend", appInfoCard);
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
    stateLayout->addWidget(stateTitle);
    auto *stateGrid = new QGridLayout();
    stateGrid->setHorizontalSpacing(14);
    stateGrid->setVerticalSpacing(10);
    stateGrid->setColumnStretch(1, 1);
    m_stateApiStatusValue = buildKeyValueRow(stateGrid, 0, "API status", stateCard);
    m_stateCurrentModeValue = buildKeyValueRow(stateGrid, 1, "Current mode", stateCard);
    m_stateLastRefreshValue = buildKeyValueRow(stateGrid, 2, "Last refresh", stateCard);
    m_stateLastDetailValue = buildKeyValueRow(stateGrid, 3, "Last detail", stateCard);
    m_stateExternalIpValue = buildKeyValueRow(stateGrid, 4, "Public IP", stateCard);
    m_stateLocationValue = buildKeyValueRow(stateGrid, 5, "Location", stateCard);
    m_stateAsnOrgValue = buildKeyValueRow(stateGrid, 6, "ASN / Org", stateCard);
    m_stateLocationSourceValue = buildKeyValueRow(stateGrid, 7, "Source", stateCard);
    m_stateLocationUpdatedValue = buildKeyValueRow(stateGrid, 8, "Updated", stateCard);
    m_stateLocationRefreshValue = buildKeyValueRow(stateGrid, 9, "Next refresh", stateCard);
    m_stateTrafficValue = buildKeyValueRow(stateGrid, 10, "Traffic", stateCard);
    m_stateModeListValue = buildKeyValueRow(stateGrid, 11, "Supported modes", stateCard);
    stateLayout->addLayout(stateGrid);
    pageLayout->addWidget(stateCard);

    auto *loggingCard = new QWidget(page);
    loggingCard->setObjectName("card");
    auto *loggingLayout = new QVBoxLayout(loggingCard);
    loggingLayout->setContentsMargins(18, 18, 18, 18);
    loggingLayout->setSpacing(14);
    auto *loggingTitle = new QLabel("Logging", loggingCard);
    loggingTitle->setObjectName("cardTitleStrong");
    loggingLayout->addWidget(loggingTitle);
    auto *loggingGrid = new QGridLayout();
    loggingGrid->setHorizontalSpacing(14);
    loggingGrid->setVerticalSpacing(10);
    loggingGrid->setColumnStretch(1, 1);
    m_loggingStatusValue = buildKeyValueRow(loggingGrid, 0, "Logger state", loggingCard);
    m_loggingLevelValue = buildKeyValueRow(loggingGrid, 1, "Minimum level", loggingCard);
    m_loggingTextPathValue = buildKeyValueRow(loggingGrid, 2, "Text log", loggingCard);
    m_loggingJsonlPathValue = buildKeyValueRow(loggingGrid, 3, "JSONL log", loggingCard);
    m_loggingLastErrorValue = buildKeyValueRow(loggingGrid, 4, "Last logger error", loggingCard);
    loggingLayout->addLayout(loggingGrid);
    pageLayout->addWidget(loggingCard);

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
    m_appConfigPathLabel = new QLabel(editorHead);
    m_appConfigPathLabel->setObjectName("cardSubtitle");
    m_appConfigPathLabel->setWordWrap(false);
    m_appConfigPathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_appConfigPathLabel->setMinimumWidth(0);
    editorCopy->addWidget(editorTitle);
    editorCopy->addWidget(m_appConfigPathLabel);
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

    updateInformationalLabelSelection();
    updateFooterIpContentWidth();
    rebuildShortcuts();
    populateModeProfiles();
    populateRuleFiles();
    refreshAppConfigPathLabel();
    updateDashboardCards();
}

void MainWindow::populateModeProfiles() {
    setSelectedProfileName(syncModeProfileSelection(m_modeController->profiles(), m_lastStatus, m_selectedProfileName));
}

void MainWindow::populateRuleFiles() {
    const QString currentPath = m_selectedRuleFilePath;
    m_ruleFileStatuses.clear();

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
    QString profileDescription = m_modeController->profiles().isEmpty()
                                     ? QString("No mode profiles configured. Add entries in config.yaml.")
                                     : QString("Choose a profile to switch immediately.");
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
        const QString displayName = m_selectedProfileName.isEmpty()
                                        ? (m_modeController->profiles().isEmpty() ? QString("No profiles") : QString("Unknown"))
                                        : displayModeName(m_selectedProfileName);
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

void MainWindow::refreshAppConfigPathLabel() {
    if (!m_appConfigPathLabel) {
        return;
    }

    const QString fullPath = m_config.configPath.trimmed();
    if (fullPath.isEmpty()) {
        m_appConfigPathLabel->setText("Config path unavailable");
        m_appConfigPathLabel->setToolTip(QString());
        return;
    }

    const QString compact = compactPath(fullPath);
    m_appConfigPathLabel->setToolTip(fullPath);
    int availableWidth = m_appConfigPathLabel->width();
    if (availableWidth <= 0 && m_appConfigPathLabel->parentWidget()) {
        availableWidth = m_appConfigPathLabel->parentWidget()->contentsRect().width();
    }
    if (availableWidth <= 0 && m_appConfigPathLabel->parentWidget() && m_appConfigPathLabel->parentWidget()->parentWidget()) {
        availableWidth = m_appConfigPathLabel->parentWidget()->parentWidget()->contentsRect().width();
    }
    availableWidth = qMax(220, availableWidth);
    const QString visibleText = m_appConfigPathLabel->fontMetrics().elidedText(compact, Qt::ElideMiddle, availableWidth);
    m_appConfigPathLabel->setText(visibleText);
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
    refreshRuleFileStatusCache();

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
        if (ruleFile.path == m_selectedRuleFilePath) {
            activeIndex = index;
        }
        const RuleFileUiStatus status = statusForRuleFilePath(ruleFile.path);
        badge->setText(status.text);
        badge->setProperty("tone", status.tone);
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

MainWindow::RuleFileUiStatus MainWindow::ruleFileStatusForText(const QString &text, const QString &loadedText) const {
    const auto validation = m_ruleSetService->validateJson(text);
    if (!validation.ok) {
        return {"Needs fix", "danger"};
    }
    if (text != loadedText) {
        return {"Unsaved", "warn"};
    }
    return {"Saved", "neutral"};
}

MainWindow::RuleFileUiStatus MainWindow::evaluateRuleFileStatusOnDisk(const NamedRuleFile &file) const {
    const auto result = m_ruleSetService->loadFile(file.path);
    if (!result.ok) {
        return {"Needs fix", "danger"};
    }
    return ruleFileStatusForText(result.text, result.text);
}

void MainWindow::refreshRuleFileStatusCache() {
    m_ruleFileStatuses.clear();
    for (const auto &ruleFile : m_ruleFiles) {
        m_ruleFileStatuses.insert(ruleFile.path, evaluateRuleFileStatusOnDisk(ruleFile));
    }
    updateSelectedRuleFileStatus();
}

void MainWindow::updateSelectedRuleFileStatus() {
    const auto *file = selectedRuleFile();
    if (!file || !m_editor) {
        return;
    }
    m_ruleFileStatuses.insert(file->path, ruleFileStatusForText(m_editor->toPlainText(), m_loadedRuleText));
}

MainWindow::RuleFileUiStatus MainWindow::statusForRuleFilePath(const QString &path) const {
    const auto it = m_ruleFileStatuses.constFind(path);
    if (it != m_ruleFileStatuses.cend()) {
        return it.value();
    }
    return {"Saved", "neutral"};
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
    setStatusPill(m_ruleBannerStateLabel, tone == "danger" ? "Needs fix" : tone == "warn" ? "Unsaved" : tone == "ok" ? "Valid" : "Saved", tone);
    if (m_ruleBannerTitleLabel && m_ruleBannerTitleLabel->parentWidget()) {
        m_ruleBannerTitleLabel->parentWidget()->setProperty("tone", tone);
        repolish(m_ruleBannerTitleLabel->parentWidget());
    }
}

void MainWindow::setModeBanner(const QString &title, const QString &message, const QString &tone) {
    if (m_modeBannerTitleLabel) {
        m_modeBannerTitleLabel->setText(title);
    }
    if (m_modeBannerMessageLabel) {
        m_modeBannerMessageLabel->setText(message);
    }
    setStatusPill(m_modeBannerStateLabel,
                  tone == "danger" ? "Error" : tone == "warn" ? "Attention" : tone == "ok" ? "Live" : "Synced",
                  tone);
    if (m_modeBannerTitleLabel && m_modeBannerTitleLabel->parentWidget()) {
        m_modeBannerTitleLabel->parentWidget()->setProperty("tone", tone);
        repolish(m_modeBannerTitleLabel->parentWidget());
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

void MainWindow::updateInformationalLabelSelection() {
    const bool enableSelection = m_config.ui.textSelection.enableInformationalLabels;
    const auto labels = findChildren<QLabel *>();
    for (QLabel *label : labels) {
        const bool selectable = enableSelection && isInformationalLabelCandidate(label);
        label->setTextInteractionFlags(selectable ? Qt::TextSelectableByMouse : Qt::NoTextInteraction);
        label->setCursor(selectable ? Qt::IBeamCursor : Qt::ArrowCursor);
    }
}

void MainWindow::updateFooterIpContentWidth() {
    if (!m_footerIpContent || !m_footerIpValue || !m_footerIpMetaLabel || !m_footerIpLocationBadgeLabel) {
        return;
    }

    m_footerIpLocationBadgeLabel->adjustSize();
    const int ipTextWidth = m_footerIpValue->sizeHint().width();
    const QString badgeText = m_footerIpLocationBadgeLabel->text().trimmed();
    const int headerSpacing = 6;
    const int headerWidth = m_footerIpMetaLabel->sizeHint().width() +
                            (badgeText.isEmpty() ? 0 : headerSpacing + m_footerIpLocationBadgeLabel->sizeHint().width());
    m_footerIpContent->setFixedWidth(qMax(ipTextWidth, headerWidth));
}

void MainWindow::rebuildShortcuts() {
    for (const auto &shortcut : m_shortcuts) {
        if (shortcut) {
            shortcut->deleteLater();
        }
    }
    m_shortcuts.clear();

    const auto &shortcuts = m_config.ui.keyboard.shortcuts;
    auto registerShortcut = [this](const QString &sequence, auto handler, bool allowWhenEditorFocused = true) {
        if (sequence.trimmed().isEmpty()) {
            return;
        }

        auto *shortcut = new QShortcut(QKeySequence(sequence), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, handler, allowWhenEditorFocused]() {
            if (!allowWhenEditorFocused && isTextEditorFocused()) {
                return;
            }
            handler();
        });
        m_shortcuts.push_back(shortcut);
    };

    registerShortcut(shortcuts.closeWindowPrimary, [this]() {
        closeWindowFromShortcut(true);
    });
    registerShortcut(shortcuts.closeWindowSecondary, [this]() {
        closeWindowFromShortcut(false);
    }, false);
    registerShortcut(shortcuts.nextPage, [this]() {
        if (m_navButtons.isEmpty() || !m_pages) {
            return;
        }
        const int next = (m_pages->currentIndex() + 1) % m_navButtons.size();
        setCurrentPage(next);
    });
    registerShortcut(shortcuts.previousPage, [this]() {
        if (m_navButtons.isEmpty() || !m_pages) {
            return;
        }
        const int prev = (m_pages->currentIndex() - 1 + m_navButtons.size()) % m_navButtons.size();
        setCurrentPage(prev);
    });
    registerShortcut(shortcuts.pageMain, [this]() {
        setCurrentPage(kDashboardPageIndex);
    }, false);
    registerShortcut(shortcuts.pageRules, [this]() {
        setCurrentPage(kRulesPageIndex);
    }, false);
    registerShortcut(shortcuts.pageSettings, [this]() {
        setCurrentPage(kSettingsPageIndex);
    }, false);
    registerShortcut(shortcuts.openModeSelector, [this]() {
        setCurrentPage(kDashboardPageIndex);
        openModePopup();
    }, false);
    registerShortcut(shortcuts.openRuleFileSelector, [this]() {
        setCurrentPage(kRulesPageIndex);
        openRuleFilePopup();
    }, false);
    registerShortcut(shortcuts.refreshRuntime, [this]() {
        refreshRuntime();
    });
    registerShortcut(shortcuts.refreshLocationData, [this]() {
        if (!m_refreshLocationDataButton || !m_refreshLocationDataButton->isEnabled()) {
            return;
        }
        m_diagnosticsService->refreshLocationDataNow();
        if (m_loggingService) {
            m_loggingService->logInfo("ui.runtime", "Requested location data refresh");
        }
        showActionMessage("Requested location data refresh", 3000);
    });
    registerShortcut(shortcuts.validateEditor, [this]() {
        triggerEditorValidate();
    });
    registerShortcut(shortcuts.saveEditor, [this]() {
        triggerEditorSave();
    });
    registerShortcut(shortcuts.reloadEditor, [this]() {
        triggerEditorReload();
    });
}

void MainWindow::refreshRuntime() {
    if (!m_modeController || !m_diagnosticsService) {
        return;
    }

    m_modeController->refreshStatus();
    m_diagnosticsService->refreshNow();
    if (m_loggingService) {
        m_loggingService->logInfo("ui.runtime", "Requested runtime refresh");
    }
    showActionMessage("Requested runtime refresh", 3000);
}

void MainWindow::onLoggingStatusChanged(const logging::LoggingStatus &status, bool announceError) {
    const QString previousError = m_lastLoggingStatus.lastError;
    m_lastLoggingStatus = status;
    updateDashboardCards();
    if (announceError && !status.lastError.trimmed().isEmpty() && status.lastError != previousError) {
        showActionMessage(status.lastError, 5000);
    }
}

void MainWindow::triggerEditorSave() {
    if (!m_pages) {
        return;
    }

    switch (m_pages->currentIndex()) {
    case kRulesPageIndex:
        saveCurrentRuleFile();
        break;
    case kSettingsPageIndex:
        saveSettingsFile();
        break;
    default:
        break;
    }
}

void MainWindow::triggerEditorReload() {
    if (!m_pages) {
        return;
    }

    switch (m_pages->currentIndex()) {
    case kRulesPageIndex:
        reloadCurrentRuleFile();
        break;
    case kSettingsPageIndex:
        reloadSettingsFile();
        break;
    default:
        break;
    }
}

void MainWindow::triggerEditorValidate() {
    if (!m_pages) {
        return;
    }

    switch (m_pages->currentIndex()) {
    case kRulesPageIndex:
        validateCurrentEditorText();
        break;
    case kSettingsPageIndex:
        validateSettingsText();
        break;
    default:
        break;
    }
}

void MainWindow::closeWindowFromShortcut(bool allowWhenEditorFocused) {
    if (!allowWhenEditorFocused && isTextEditorFocused()) {
        return;
    }
    if (m_selectorPopup) {
        closeSelectorPopup();
        return;
    }
    close();
}

bool MainWindow::isTextEditorFocused() const {
    QWidget *focused = QApplication::focusWidget();
    return focused && (focused == m_editor || focused == m_settingsEditor ||
                       (m_editor && m_editor->isAncestorOf(focused)) ||
                       (m_settingsEditor && m_settingsEditor->isAncestorOf(focused)));
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
    const bool publicIpUsable = hasUsableIpAddress(m_lastDiagnostics.publicIp);
    const QString locationText =
        m_lastDiagnostics.location.isEmpty() ? (m_lastDiagnostics.locationDisabled ? "Location disabled" : "Location unavailable")
                                             : m_lastDiagnostics.location;
    const QString locationBadgeText = formatLocationBadgeText(m_lastDiagnostics);
    const QString locationAsnOrgText =
        m_lastDiagnostics.locationAsnOrg.trimmed().isEmpty() ? "Unavailable" : m_lastDiagnostics.locationAsnOrg;
    const QString locationSourceText = formatLocationSourceText(m_lastDiagnostics);
    const QString locationUpdatedText =
        m_lastDiagnostics.locationDisabled ? "Disabled" : formatDiagnosticTimestamp(m_lastDiagnostics.locationUpdatedAt);
    const QString locationNextRefreshText =
        m_lastDiagnostics.locationDisabled ? "Disabled" : formatDiagnosticTimestamp(m_lastDiagnostics.locationNextRefreshAt);
    const QString delayText = formatDelayValue(m_lastDiagnostics.delayTotalMs);
    const QString trafficText =
        m_lastDiagnostics.trafficAvailable ? QString("%1\n%2").arg(m_lastDiagnostics.trafficSummary, m_lastDiagnostics.trafficDetail)
                                           : m_lastDiagnostics.trafficDetail;
    const QString modeSyncText = m_config.clashApi.modeSyncIntervalMs > 0
                                     ? QString("Mode sync %1").arg(formatRefreshInterval(m_config.clashApi.modeSyncIntervalMs))
                                     : QString("Mode sync disabled");
    const QString diagnosticsText = m_config.diagnostics.enabled
                                        ? QString("Every %1 · timeout %2 ms · %3")
                                              .arg(formatRefreshInterval(m_config.diagnostics.refreshIntervalMs))
                                              .arg(m_config.diagnostics.requestTimeoutMs)
                                              .arg(modeSyncText)
                                        : QString("Disabled · %1").arg(modeSyncText);
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
    bool canRefreshLocationData = false;
    QString refreshLocationTooltip = "Location data refresh unavailable";
    switch (m_config.diagnostics.connection.location.mode) {
    case config::DiagnosticsLocationMode::Disabled:
        refreshLocationTooltip = "Location lookup is disabled";
        break;
    case config::DiagnosticsLocationMode::DynamicCache:
        if (!m_config.diagnostics.connection.location.dynamicCache.allowManualRefresh) {
            refreshLocationTooltip = "Manual location updates are disabled";
        } else if (!publicIpUsable) {
            refreshLocationTooltip = "Public IP unavailable; run Refresh runtime first";
        } else {
            canRefreshLocationData = true;
            refreshLocationTooltip = "Refresh cached location data from the GeoIP provider";
        }
        break;
    case config::DiagnosticsLocationMode::LocalDb:
    default:
        if (m_config.diagnostics.connection.location.localDb.downloadUrl.trimmed().isEmpty()) {
            refreshLocationTooltip = "Local DB update is not configured";
        } else {
            canRefreshLocationData = true;
            refreshLocationTooltip = "Download and replace the local GeoIP database";
        }
        break;
    }

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
    if (m_refreshLocationDataButton) {
        m_refreshLocationDataButton->setEnabled(canRefreshLocationData);
        m_refreshLocationDataButton->setToolTip(refreshLocationTooltip);
    }
    if (m_footerIpLocationBadgeLabel) {
        m_footerIpLocationBadgeLabel->setText(locationBadgeText);
        m_footerIpLocationBadgeLabel->setToolTip(
            locationBadgeText == "N/A" ? QString("Location information unavailable") : locationText);
    }

    if (m_connectionEndpointValue) {
        m_connectionEndpointValue->setText(connectionApiText);
    }
    if (m_connectionDiagnosticsValue) {
        m_connectionDiagnosticsValue->setText(delayText);
        m_connectionDiagnosticsValue->setToolTip(delayDetailText);
    }
    if (m_connectionDelayDnsValue) {
        m_connectionDelayDnsValue->setText(
            m_lastDiagnostics.delayDnsMs >= 0 ? QString("%1 ms").arg(m_lastDiagnostics.delayDnsMs) : "—");
    }
    if (m_connectionDelayConnectValue) {
        m_connectionDelayConnectValue->setText(
            m_lastDiagnostics.delayConnectMs >= 0 ? QString("%1 ms").arg(m_lastDiagnostics.delayConnectMs) : "—");
    }
    if (m_connectionDelayTlsValue) {
        m_connectionDelayTlsValue->setText(
            m_lastDiagnostics.delayTlsMs >= 0 ? QString("%1 ms").arg(m_lastDiagnostics.delayTlsMs) : "—");
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
        m_controllerAddressValue->setText(locationAsnOrgText);
    }

    if (m_rulesDirectoryValue) {
        m_rulesDirectoryValue->setText(locationSourceText);
    }
    if (m_configRootValue) {
        m_configRootValue->setText(locationNextRefreshText);
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
        m_infoGeoDbValue->setText(m_lastDiagnostics.configurationDetail);
    }
    if (m_infoProfilesValue) {
        m_infoProfilesValue->setText(QString("%1 available").arg(m_modeController->profiles().size()));
    }
    if (m_infoThemeValue) {
        m_infoThemeValue->setText(m_config.theme.qssPath.isEmpty() ? "Built-in QSS theme" : compactPath(m_config.theme.qssPath));
    }
    if (m_loggingStatusValue) {
        m_loggingStatusValue->setText(formatLoggingStatusText(m_lastLoggingStatus));
    }
    if (m_loggingLevelValue) {
        m_loggingLevelValue->setText(displayModeName(logging::loggingLevelToString(m_lastLoggingStatus.level)));
    }
    if (m_loggingTextPathValue) {
        m_loggingTextPathValue->setText(
            formatLoggingSinkText(m_lastLoggingStatus.textPath, m_lastLoggingStatus.textSinkActive, m_lastLoggingStatus.enabled));
    }
    if (m_loggingJsonlPathValue) {
        m_loggingJsonlPathValue->setText(
            formatLoggingSinkText(m_lastLoggingStatus.jsonlPath, m_lastLoggingStatus.jsonlSinkActive, m_lastLoggingStatus.enabled));
    }
    if (m_loggingLastErrorValue) {
        m_loggingLastErrorValue->setText(m_lastLoggingStatus.lastError.trimmed().isEmpty() ? QString("None")
                                                                                           : m_lastLoggingStatus.lastError);
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
        m_stateExternalIpValue->setText(publicIp == "-" ? "Unavailable" : publicIp);
    }
    if (m_stateLocationValue) {
        m_stateLocationValue->setText(locationText);
    }
    if (m_stateAsnOrgValue) {
        m_stateAsnOrgValue->setText(locationAsnOrgText);
    }
    if (m_stateLocationSourceValue) {
        m_stateLocationSourceValue->setText(locationSourceText);
    }
    if (m_stateLocationUpdatedValue) {
        m_stateLocationUpdatedValue->setText(locationUpdatedText);
    }
    if (m_stateLocationRefreshValue) {
        m_stateLocationRefreshValue->setText(locationNextRefreshText);
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

    updateFooterIpContentWidth();
    updateModeSelectionUi();
}

void MainWindow::onStatusUpdated(const clash::ModeStatus &status) {
    m_lastStatus = status;
    populateModeProfiles();
    if (!status.busy) {
        if (status.reachable && !status.currentModeValue.isEmpty()) {
            if (status.currentProfileName == "unknown" || status.currentProfileName.isEmpty()) {
                setModeBanner("Mode not mapped",
                              QString("Backend reports '%1', but no configured profile matches it.")
                                  .arg(status.currentModeValue),
                              "warn");
            } else {
                setModeBanner("Mode synchronized",
                              QString("Profile %1 matches backend mode '%2'.")
                                  .arg(displayModeName(status.currentProfileName), status.currentModeValue),
                              "neutral");
            }
        } else if (!status.detail.trimmed().isEmpty() && status.detail != "Not refreshed yet") {
            setModeBanner("Mode sync issue", status.detail, "warn");
        }
    }
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

    const auto result = m_ruleSetService->loadFile(file->path);
    if (!result.ok) {
        if (m_editor) {
            QSignalBlocker blocker(m_editor);
            m_editor->clear();
        }
        m_loadedRuleText.clear();
        m_ruleFileStatuses.insert(file->path, {"Needs fix", "danger"});
        updateRuleLineNumbers();
        applyEditorErrorHighlight(m_editor, -1, -1);
        setRuleBanner("Load failed", result.error, "danger");
        if (m_loggingService) {
            m_loggingService->logWarning("rules.editor",
                                         "Failed to load rule-set file",
                                         result.error,
                                         {{"path", file->path}});
        }
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
    const RuleFileUiStatus status = ruleFileStatusForText(result.text, m_loadedRuleText);
    m_ruleFileStatuses.insert(file->path, status);
    if (status.tone == "danger") {
        const auto validation = m_ruleSetService->validateJson(result.text);
        setRuleBanner("Needs fix", validation.error.isEmpty() ? compactPath(file->path) : validation.error, "danger");
    } else {
        setRuleBanner("Loaded file", file->description.isEmpty() ? compactPath(file->path) : file->description, "neutral");
    }
    showActionMessage(QString("Loaded %1").arg(file->name), 3000);
}

void MainWindow::onRuleEditorTextChanged() {
    updateRuleLineNumbers();
    if (!m_editor) {
        return;
    }

    updateRuleEditorErrorHighlight();

    const RuleFileUiStatus status = ruleFileStatusForText(m_editor->toPlainText(), m_loadedRuleText);
    if (status.tone == "danger") {
        setRuleBanner("Needs fix", "JSON is invalid. Fix syntax before save.", "danger");
    } else if (status.tone == "warn") {
        setRuleBanner("Unsaved changes", "Validate before save to confirm formatting and JSON syntax.", "warn");
    } else {
        setRuleBanner("Saved copy", "Editor content matches the last loaded or saved file.", "neutral");
    }
    updateSelectedRuleFileStatus();
    updateRuleFileTrigger();
}

void MainWindow::validateCurrentEditorText() {
    if (!m_editor) {
        return;
    }

    const auto *file = selectedRuleFile();
    const auto result = m_ruleSetService->validateJson(m_editor->toPlainText());
    if (!result.ok) {
        updateSelectedRuleFileStatus();
        setRuleBanner("Invalid JSON", result.error, "danger");
        if (m_loggingService) {
            m_loggingService->logWarning("rules.editor",
                                         "Rule-set validation failed",
                                         result.error,
                                         file ? logging::LogContext{{"path", file->path}} : logging::LogContext{});
        }
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
    updateSelectedRuleFileStatus();
    updateRuleFileTrigger();
}

void MainWindow::saveCurrentRuleFile() {
    const auto *file = selectedRuleFile();
    if (!file || !m_editor) {
        return;
    }

    const auto validation = m_ruleSetService->validateJson(m_editor->toPlainText());
    if (!validation.ok) {
        updateSelectedRuleFileStatus();
        setRuleBanner("Invalid JSON", validation.error, "danger");
        if (m_loggingService) {
            m_loggingService->logWarning("rules.editor",
                                         "Rule-set save rejected by validation",
                                         validation.error,
                                         {{"path", file->path}});
        }
        showActionMessage(validation.error, 5000);
        QMessageBox::warning(this, "Save failed", validation.error);
        return;
    }

    const auto result = m_ruleSetService->saveFile(file->path, validation.formattedText);
    if (!result.ok) {
        setRuleBanner("Save failed", result.error, "danger");
        if (m_loggingService) {
            m_loggingService->logError("rules.editor",
                                       "Failed to save rule-set file",
                                       result.error,
                                       {{"path", file->path}});
        }
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
    if (m_loggingService) {
        m_loggingService->logInfo("rules.editor", "Saved rule-set file", {}, {{"path", file->path}});
    }
    showActionMessage("Rule-set saved", 3000);
    updateSelectedRuleFileStatus();
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
        if (m_loggingService) {
            m_loggingService->logWarning("config.editor",
                                         "Config validation failed",
                                         result.error,
                                         {{"path", m_config.configPath}});
        }
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
        if (m_loggingService) {
            m_loggingService->logWarning("config.editor",
                                         "Config save rejected by validation",
                                         validation.error,
                                         {{"path", m_config.configPath}});
        }
        showActionMessage(validation.error, 5000);
        QMessageBox::warning(this, "Config save failed", validation.error);
        return;
    }

    const auto result = m_configFileService->saveFile(m_config.configPath, text);
    if (!result.ok) {
        setSettingsBanner(result.error, "danger");
        if (m_loggingService) {
            m_loggingService->logError("config.editor",
                                       "Failed to write config file",
                                       result.error,
                                       {{"path", m_config.configPath}});
        }
        showActionMessage(result.error, 5000);
        QMessageBox::warning(this, "Config save failed", result.error);
        return;
    }

    config::AppConfig parsedConfig;
    try {
        parsedConfig = config::ConfigLoader::loadFromData(text, m_config.configPath);
    } catch (const std::exception &ex) {
        const QString error = QString("Config saved but failed to apply: %1").arg(ex.what());
        setSettingsBanner(error, "warn");
        if (m_loggingService) {
            m_loggingService->logError("config.editor",
                                       "Saved config file but failed to parse applied config",
                                       ex.what(),
                                       {{"path", m_config.configPath}});
        }
        showActionMessage(error, 5000);
        return;
    }

    app::RuntimeConfigApplyResult applyResult;
    if (m_runtimeConfigApplier) {
        applyResult = m_runtimeConfigApplier->apply(parsedConfig);
    }
    applyConfig(parsedConfig);
    m_loadedSettingsText = text;
    if (!applyResult.warning.trimmed().isEmpty()) {
        const QString warning = QString("Config saved and applied with warning: %1").arg(applyResult.warning);
        setSettingsBanner(warning, "warn");
        if (m_loggingService) {
            m_loggingService->logWarning("config.editor",
                                         "Saved config file and applied runtime changes with warning",
                                         applyResult.warning,
                                         {{"path", m_config.configPath}});
        }
        showActionMessage(warning, 5000);
    } else {
        setSettingsBanner("Config saved and applied.", "ok");
        if (m_loggingService) {
            m_loggingService->logInfo("config.editor",
                                      "Saved config file and applied runtime changes",
                                      {},
                                      {{"path", m_config.configPath}});
        }
        showActionMessage("Config saved", 3000);
    }
    applyEditorErrorHighlight(m_settingsEditor, -1, -1);
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
        if (m_loggingService) {
            m_loggingService->logError("config.editor",
                                       "Failed to reload config file from disk",
                                       result.error,
                                       {{"path", m_config.configPath}});
        }
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
        const auto parsedConfig = config::ConfigLoader::loadFromData(result.text, m_config.configPath);
        app::RuntimeConfigApplyResult applyResult;
        if (m_runtimeConfigApplier) {
            applyResult = m_runtimeConfigApplier->apply(parsedConfig);
        }
        applyConfig(parsedConfig);
        if (!applyResult.warning.trimmed().isEmpty()) {
            const QString warning = QString("Loaded config with warning: %1").arg(applyResult.warning);
            setSettingsBanner(warning, "warn");
            if (m_loggingService) {
                m_loggingService->logWarning("config.editor",
                                             "Reloaded config file with runtime warning",
                                             applyResult.warning,
                                             {{"path", m_config.configPath}});
            }
            showActionMessage(warning, 5000);
        } else {
            setSettingsBanner("Loaded and applied current config.yaml.", "ok");
            if (m_loggingService) {
                m_loggingService->logInfo("config.editor",
                                          "Reloaded config file and applied runtime changes",
                                          {},
                                          {{"path", m_config.configPath}});
            }
            showActionMessage("Config reloaded", 3000);
        }
    } catch (const std::exception &ex) {
        const QString error = QString("Loaded config text but failed to apply: %1").arg(ex.what());
        setSettingsBanner(error, "warn");
        if (m_loggingService) {
            m_loggingService->logError("config.editor",
                                       "Loaded config text but failed to apply runtime changes",
                                       ex.what(),
                                       {{"path", m_config.configPath}});
        }
        showActionMessage(error, 5000);
    }
}

}  // namespace tunlet::ui
