#include "rules/ruleset_service.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

TEST_CASE("RuleSetService validates and saves formatted JSON", "[rules]") {
    tunlet::config::EditingConfig editing;
    editing.createBackup = true;

    tunlet::rules::RuleSetService service(editing);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = dir.path() + "/rules.json";
    QFile original(path);
    REQUIRE(original.open(QIODevice::WriteOnly | QIODevice::Text));
    original.write("{\"old\":true}");
    original.close();

    const auto saveResult = service.saveFile(path, "{\"a\":1}");
    REQUIRE(saveResult.ok);

    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString savedText = QString::fromUtf8(file.readAll());
    REQUIRE(savedText.contains("\n"));

    QFile backup(path + ".bak");
    REQUIRE(backup.exists());
}

TEST_CASE("RuleSetService rejects invalid JSON", "[rules]") {
    tunlet::rules::RuleSetService service({});
    const auto result = service.saveFile("/tmp/unused.json", "{invalid");
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.contains("invalid JSON"));
}

TEST_CASE("RuleSetService reports JSON error coordinates", "[rules]") {
    tunlet::rules::RuleSetService service({});
    const auto result = service.validateJson("{\n  \"a\":\n}");
    REQUIRE_FALSE(result.ok);
    REQUIRE(result.error.contains("invalid JSON"));
    REQUIRE(result.errorLine == 3);
    REQUIRE(result.errorColumn >= 1);
    REQUIRE(result.errorOffset >= 0);
}

TEST_CASE("RuleSetService applies updated editing settings on next save", "[rules]") {
    tunlet::config::EditingConfig editing;
    editing.createBackup = true;
    editing.backupSuffix = ".bak";

    tunlet::rules::RuleSetService service(editing);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString path = dir.path() + "/rules.json";
    QFile original(path);
    REQUIRE(original.open(QIODevice::WriteOnly | QIODevice::Text));
    original.write("{\"old\":true}");
    original.close();

    editing.backupSuffix = ".live";
    service.updateEditingConfig(editing);

    const auto saveResult = service.saveFile(path, "{\"a\":1}");
    REQUIRE(saveResult.ok);
    REQUIRE(QFile::exists(path + ".live"));
    REQUIRE_FALSE(QFile::exists(path + ".bak"));
}
