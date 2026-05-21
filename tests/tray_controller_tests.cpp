#include "ui/tray_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTest>

namespace {

QApplication *testApplication() {
    static int argc = 1;
    static char appName[] = "tunlet_tray_tests";
    static char *argv[] = {appName, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication application(argc, argv);
    return &application;
}

QSystemTrayIcon *trayIconFor(tunlet::ui::TrayController &controller) {
    return controller.findChild<QSystemTrayIcon *>();
}

QMenu *trayMenuFor(tunlet::ui::TrayController &controller) {
    QSystemTrayIcon *trayIcon = trayIconFor(controller);
    return trayIcon ? trayIcon->contextMenu() : nullptr;
}

QStringList visibleActionTexts(QMenu *menu) {
    QStringList texts;
    if (!menu) {
        return texts;
    }

    for (QAction *action : menu->actions()) {
        if (action->isSeparator()) {
            continue;
        }
        texts.push_back(action->text());
    }
    return texts;
}

QAction *findAction(QMenu *menu, const QString &text) {
    if (!menu) {
        return nullptr;
    }

    for (QAction *action : menu->actions()) {
        if (!action->isSeparator() && action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

tunlet::clash::ModeStatus makeStatus() {
    tunlet::clash::ModeStatus status;
    status.currentProfileName = "proxy";
    status.currentModeValue = "global";
    status.reachable = true;
    status.busy = false;
    status.switchInFlight = false;
    return status;
}

QVector<tunlet::config::ClashModeProfile> makeProfiles() {
    return {
        {.name = "direct", .mode = "direct", .desc = "Direct"},
        {.name = "proxy", .mode = "global", .desc = "Proxy"},
    };
}

tunlet::diagnostics::DiagnosticsSnapshot makeDiagnostics() {
    tunlet::diagnostics::DiagnosticsSnapshot snapshot;
    snapshot.publicIp = "203.0.113.20";
    snapshot.locationCountryCode = "nl";
    snapshot.delayTotalMs = 123;
    return snapshot;
}

}  // namespace

TEST_CASE("TrayController shows mode and diagnostics in tooltip and native menu", "[tray]") {
    testApplication();

    tunlet::ui::TrayController controller;
    tunlet::config::TrayConfig trayConfig;
    controller.setup(makeStatus(), makeProfiles(), makeDiagnostics(), trayConfig);

    QSystemTrayIcon *trayIcon = trayIconFor(controller);
    REQUIRE(trayIcon != nullptr);
    REQUIRE(trayIcon->toolTip().contains("Mode: Proxy"));
    REQUIRE(trayIcon->toolTip().contains("Status: API reachable"));
    REQUIRE(trayIcon->toolTip().contains("IP: 203.0.113.20 (NL)"));
    REQUIRE(trayIcon->toolTip().contains("Delay: 123 ms"));

    QMenu *menu = trayMenuFor(controller);
    REQUIRE(menu != nullptr);

    const QStringList texts = visibleActionTexts(menu);
    REQUIRE(texts.contains("Mode: Proxy"));
    REQUIRE(texts.contains("API: Reachable"));
    REQUIRE(texts.contains("Direct"));
    REQUIRE(texts.contains("Proxy"));
    REQUIRE(texts.contains("IP: 203.0.113.20 | NL"));
    REQUIRE(texts.contains("Delay: 123 ms"));
    REQUIRE(texts.contains("Open tunlet"));
    REQUIRE(texts.contains("Refresh"));
    REQUIRE(texts.contains("Quit"));
    REQUIRE_FALSE(texts.contains("Status"));
    REQUIRE_FALSE(texts.contains("Connection mode"));
    REQUIRE_FALSE(texts.contains("Diagnostics"));
    REQUIRE_FALSE(texts.contains("Actions"));

    QAction *proxyAction = findAction(menu, "Proxy");
    QAction *directAction = findAction(menu, "Direct");
    REQUIRE(proxyAction != nullptr);
    REQUIRE(directAction != nullptr);
    REQUIRE(proxyAction->isCheckable());
    REQUIRE(directAction->isCheckable());
    REQUIRE(proxyAction->isChecked());
    REQUIRE_FALSE(directAction->isChecked());
}

TEST_CASE("TrayController triggers one immediate refresh when the tray menu opens", "[tray]") {
    testApplication();

    tunlet::ui::TrayController controller;
    tunlet::config::TrayConfig trayConfig;
    controller.setup(makeStatus(), makeProfiles(), makeDiagnostics(), trayConfig);

    QSignalSpy refreshSpy(&controller, &tunlet::ui::TrayController::refreshRequested);
    REQUIRE(refreshSpy.isValid());

    REQUIRE(QMetaObject::invokeMethod(&controller, "showContextMenu", Qt::DirectConnection));
    QMenu *menu = trayMenuFor(controller);
    REQUIRE(menu != nullptr);
    QTRY_VERIFY(menu->isVisible());
    REQUIRE(refreshSpy.count() == 1);
}

TEST_CASE("TrayController open and quit actions emit their signals", "[tray]") {
    testApplication();

    {
        tunlet::ui::TrayController controller;
        tunlet::config::TrayConfig trayConfig;
        controller.setup(makeStatus(), makeProfiles(), makeDiagnostics(), trayConfig);

        QSignalSpy openSpy(&controller, &tunlet::ui::TrayController::openMainWindowRequested);
        REQUIRE(openSpy.isValid());

        QMenu *menu = trayMenuFor(controller);
        REQUIRE(menu != nullptr);
        QAction *openAction = findAction(menu, "Open tunlet");
        REQUIRE(openAction != nullptr);
        openAction->trigger();
        REQUIRE(openSpy.count() == 1);
    }

    {
        tunlet::ui::TrayController controller;
        tunlet::config::TrayConfig trayConfig;
        controller.setup(makeStatus(), makeProfiles(), makeDiagnostics(), trayConfig);

        QSignalSpy quitSpy(&controller, &tunlet::ui::TrayController::quitRequested);
        REQUIRE(quitSpy.isValid());

        QMenu *menu = trayMenuFor(controller);
        REQUIRE(menu != nullptr);
        QAction *quitAction = findAction(menu, "Quit");
        REQUIRE(quitAction != nullptr);
        quitAction->trigger();
        REQUIRE(quitSpy.count() == 1);
    }
}

TEST_CASE("TrayController keeps stable values during in-flight refresh and does not create custom popup", "[tray]") {
    testApplication();

    tunlet::ui::TrayController controller;
    tunlet::config::TrayConfig trayConfig;
    controller.setup(makeStatus(), makeProfiles(), makeDiagnostics(), trayConfig);

    REQUIRE(QMetaObject::invokeMethod(&controller, "showContextMenu", Qt::DirectConnection));
    QMenu *menu = trayMenuFor(controller);
    REQUIRE(menu != nullptr);
    QTRY_VERIFY(menu->isVisible());

    tunlet::clash::ModeStatus busyStatus = makeStatus();
    busyStatus.busy = true;
    tunlet::diagnostics::DiagnosticsSnapshot busyDiagnostics = makeDiagnostics();
    busyDiagnostics.runtimeRefreshInFlight = true;
    busyDiagnostics.publicIp.clear();
    busyDiagnostics.delayTotalMs = -1;

    controller.updateStatus(busyStatus);
    controller.updateDiagnostics(busyDiagnostics);

    const QStringList texts = visibleActionTexts(menu);
    REQUIRE(menu->isVisible());
    REQUIRE_FALSE(texts.contains("Refreshing..."));
    REQUIRE(texts.contains("Mode: Proxy"));
    REQUIRE(texts.contains("IP: 203.0.113.20 | NL"));
    REQUIRE(texts.contains("Delay: 123 ms"));

    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevelWidgets) {
        REQUIRE(widget->objectName() != "trayPopup");
    }

    REQUIRE(QMetaObject::invokeMethod(&controller, "hideContextMenu", Qt::DirectConnection));
    QTRY_VERIFY(!menu->isVisible());
}

TEST_CASE("TrayController refresh action emits a manual refresh request", "[tray]") {
    testApplication();

    tunlet::ui::TrayController controller;
    tunlet::config::TrayConfig trayConfig;
    controller.setup(makeStatus(), makeProfiles(), makeDiagnostics(), trayConfig);

    QSignalSpy refreshSpy(&controller, &tunlet::ui::TrayController::refreshRequested);
    REQUIRE(refreshSpy.isValid());

    QMenu *menu = trayMenuFor(controller);
    REQUIRE(menu != nullptr);

    QAction *refreshAction = findAction(menu, "Refresh");
    REQUIRE(refreshAction != nullptr);
    refreshAction->trigger();
    REQUIRE(refreshSpy.count() == 1);
}
