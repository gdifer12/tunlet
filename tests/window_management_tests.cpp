#include "app/instance_controller.hpp"
#include "config/config_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

QApplication *testApplication() {
    static int argc = 1;
    static char appName[] = "tunlet_window_management_tests";
    static char *argv[] = {appName, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication application(argc, argv);
    return &application;
}

QString minimalConfigWithWindowMode(const QString &modeLine = {}) {
    QString config;
    config += "clashApi:\n";
    config += "  host: 127.0.0.1\n";
    config += "  port: 9090\n";
    config += "ruleSets:\n";
    config += "  forceProxyPath: /tmp/force-proxy.json\n";
    config += "  forceDirectPath: /tmp/force-direct.json\n";
    config += "  autoProxyPath: /tmp/auto-proxy.json\n";
    config += "  autoDirectPath: /tmp/auto-direct.json\n";
    if (!modeLine.isEmpty()) {
        config += "ui:\n";
        config += "  windowActivation:\n";
        config += QString("    mode: %1\n").arg(modeLine);
    }
    return config;
}

}  // namespace

TEST_CASE("ConfigLoader parses ui.windowActivation.mode and defaults to auto", "[window-management]") {
    const auto defaultConfig =
        tunlet::config::ConfigLoader::loadFromData(minimalConfigWithWindowMode(), "/tmp/tunlet/config.yaml");
    REQUIRE(defaultConfig.ui.windowActivation.mode == tunlet::config::UiWindowActivationMode::Auto);

    const auto portableConfig =
        tunlet::config::ConfigLoader::loadFromData(minimalConfigWithWindowMode("portable"), "/tmp/tunlet/config.yaml");
    REQUIRE(portableConfig.ui.windowActivation.mode == tunlet::config::UiWindowActivationMode::Portable);

    const auto hyprlandConfig =
        tunlet::config::ConfigLoader::loadFromData(minimalConfigWithWindowMode("hyprland"), "/tmp/tunlet/config.yaml");
    REQUIRE(hyprlandConfig.ui.windowActivation.mode == tunlet::config::UiWindowActivationMode::Hyprland);

    REQUIRE_THROWS(tunlet::config::ConfigLoader::loadFromData(minimalConfigWithWindowMode("unknown"),
                                                              "/tmp/tunlet/config.yaml"));
}

TEST_CASE("InstanceController forwards open-window requests to the primary instance for the same config", "[window-management]") {
    testApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString configPath = dir.path() + "/config.yaml";

    tunlet::app::InstanceController primaryController;
    QString error;
    REQUIRE(primaryController.acquireOrForward(configPath, &error) == tunlet::app::InstanceController::AcquireResult::Primary);

    QSignalSpy openWindowSpy(&primaryController, &tunlet::app::InstanceController::openWindowRequested);
    REQUIRE(openWindowSpy.isValid());

    tunlet::app::InstanceController secondaryController;
    REQUIRE(secondaryController.acquireOrForward(configPath, &error) ==
            tunlet::app::InstanceController::AcquireResult::ForwardedToPrimary);

    QTRY_COMPARE(openWindowSpy.count(), 1);
}

TEST_CASE("InstanceController allows separate primaries for different config paths", "[window-management]") {
    testApplication();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString firstConfigPath = dir.path() + "/first.yaml";
    const QString secondConfigPath = dir.path() + "/second.yaml";

    tunlet::app::InstanceController firstController;
    tunlet::app::InstanceController secondController;
    QString error;
    REQUIRE(firstController.acquireOrForward(firstConfigPath, &error) == tunlet::app::InstanceController::AcquireResult::Primary);
    REQUIRE(secondController.acquireOrForward(secondConfigPath, &error) == tunlet::app::InstanceController::AcquireResult::Primary);
}
