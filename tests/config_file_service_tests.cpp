#include "config/config_file_service.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

TEST_CASE("ConfigFileService reports YAML parse coordinates", "[config]") {
    tunlet::config::ConfigFileService service({});

    const auto result = service.validateConfigText(
        "/tmp/config.yaml",
        "clashApi:\n"
        "  host: 127.0.0.1\n"
        "  port: [\n"
        "ruleSets:\n"
        "  forceProxyPath: one.json\n"
        "  forceDirectPath: two.json\n"
        "  autoProxyPath: three.json\n"
        "  autoDirectPath: four.json\n");

    REQUIRE_FALSE(result.ok);
    REQUIRE_FALSE(result.error.isEmpty());
    REQUIRE(result.errorLine >= 1);
    REQUIRE(result.errorColumn >= 1);
}

TEST_CASE("ConfigFileService applies updated editing settings on next save", "[config]") {
    tunlet::config::EditingConfig editing;
    editing.createBackup = true;
    editing.backupSuffix = ".bak";

    tunlet::config::ConfigFileService service(editing);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = dir.path() + "/config.yaml";
    QFile original(path);
    REQUIRE(original.open(QIODevice::WriteOnly | QIODevice::Text));
    original.write(
        "clashApi:\n"
        "  host: 127.0.0.1\n"
        "  port: 9090\n"
        "ruleSets:\n"
        "  forceProxyPath: one.json\n"
        "  forceDirectPath: two.json\n"
        "  autoProxyPath: three.json\n"
        "  autoDirectPath: four.json\n");
    original.close();

    editing.backupSuffix = ".live";
    service.updateEditingConfig(editing);

    const auto saveResult = service.saveFile(
        path,
        "clashApi:\n"
        "  host: 127.0.0.1\n"
        "  port: 9091\n"
        "ruleSets:\n"
        "  forceProxyPath: one.json\n"
        "  forceDirectPath: two.json\n"
        "  autoProxyPath: three.json\n"
        "  autoDirectPath: four.json\n");
    REQUIRE(saveResult.ok);
    REQUIRE(QFile::exists(path + ".live"));
    REQUIRE_FALSE(QFile::exists(path + ".bak"));
}
