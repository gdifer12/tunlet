#include "theme/theme_loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

QApplication *testApplication() {
    static int argc = 1;
    static char appName[] = "tunlet_theme_loader_tests";
    static char *argv[] = {appName, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication application(argc, argv);
    return &application;
}

QString readUtf8File(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

void writeUtf8File(const QString &path, const QString &content) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream << content;
}

}  // namespace

TEST_CASE("ThemeLoader renders the built-in theme", "[theme]") {
    tunlet::config::ThemeConfig config;
    QString stylesheet;
    QString error;

    const bool ok = tunlet::theme::ThemeLoader::buildStylesheet(config, &stylesheet, &error);

    REQUIRE(ok);
    REQUIRE(error.isEmpty());
    REQUIRE(stylesheet.contains("#151a21"));
    REQUIRE(stylesheet.contains("\"Inter\", \"Noto Sans\", \"Segoe UI\", sans-serif"));
    REQUIRE(stylesheet.contains("QWidget#windowTitleBar"));
    REQUIRE_FALSE(stylesheet.contains("{{"));
}

TEST_CASE("ThemeLoader deep-merges token overrides", "[theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString themePath = dir.path() + "/custom.theme.json";
    writeUtf8File(themePath,
                  "{\n"
                  "  \"color\": {\n"
                  "    \"bg\": {\n"
                  "      \"titleBar\": \"#010203\"\n"
                  "    }\n"
                  "  },\n"
                  "  \"font\": {\n"
                  "    \"family\": {\n"
                  "      \"ui\": \"\\\"IBM Plex Sans\\\", sans-serif\"\n"
                  "    }\n"
                  "  }\n"
                  "}\n");

    tunlet::config::ThemeConfig config;
    config.themePath = themePath;

    QString stylesheet;
    QString error;
    const bool ok = tunlet::theme::ThemeLoader::buildStylesheet(config, &stylesheet, &error);

    REQUIRE(ok);
    REQUIRE(error.isEmpty());
    REQUIRE(stylesheet.contains("#010203"));
    REQUIRE(stylesheet.contains("\"IBM Plex Sans\", sans-serif"));
    REQUIRE(stylesheet.contains("#1b2028"));
}

TEST_CASE("ThemeLoader uses external template overrides", "[theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString templatePath = dir.path() + "/custom.qss.in";
    writeUtf8File(templatePath,
                  "QWidget { color: {{color.text.primary}}; }\n"
                  "QLabel#special { font-family: {{font.family.mono}}; }\n");

    tunlet::config::ThemeConfig config;
    config.templatePath = templatePath;

    QString stylesheet;
    QString error;
    const bool ok = tunlet::theme::ThemeLoader::buildStylesheet(config, &stylesheet, &error);

    REQUIRE(ok);
    REQUIRE(error.isEmpty());
    REQUIRE(stylesheet.contains("QLabel#special"));
    REQUIRE(stylesheet.contains("\"JetBrains Mono\", \"Fira Code\", monospace"));
}

TEST_CASE("ThemeLoader appends raw QSS overlays", "[theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString qssPath = dir.path() + "/overlay.qss";
    writeUtf8File(qssPath, "QWidget#appRoot { border: 3px solid magenta; }\n");

    tunlet::config::ThemeConfig config;
    config.qssPath = qssPath;

    QString stylesheet;
    QString error;
    const bool ok = tunlet::theme::ThemeLoader::buildStylesheet(config, &stylesheet, &error);

    REQUIRE(ok);
    REQUIRE(error.isEmpty());
    REQUIRE(stylesheet.contains("QWidget#appRoot { border: 3px solid magenta; }"));
    REQUIRE(stylesheet.contains("#151a21"));
}

TEST_CASE("ThemeLoader keeps generated theme when raw QSS overlay is missing", "[theme]") {
    tunlet::config::ThemeConfig config;
    config.qssPath = "/tmp/does-not-exist.qss";

    QString stylesheet;
    QString error;
    const bool ok = tunlet::theme::ThemeLoader::buildStylesheet(config, &stylesheet, &error);

    REQUIRE_FALSE(ok);
    REQUIRE(error.contains("QSS override file not found"));
    REQUIRE(stylesheet.contains("#151a21"));
}

TEST_CASE("ThemeLoader rejects malformed theme override JSON", "[theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString themePath = dir.path() + "/broken.theme.json";
    writeUtf8File(themePath, "{ not valid json }\n");

    tunlet::config::ThemeConfig config;
    config.themePath = themePath;

    QString stylesheet;
    QString error;
    const bool ok = tunlet::theme::ThemeLoader::buildStylesheet(config, &stylesheet, &error);

    REQUIRE_FALSE(ok);
    REQUIRE(stylesheet.isEmpty());
    REQUIRE(error.contains("failed to parse theme JSON"));
}

TEST_CASE("ThemeLoader rejects missing template tokens", "[theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString templatePath = dir.path() + "/broken.qss.in";
    writeUtf8File(templatePath, "QWidget { color: {{color.text.unknown}}; }\n");

    tunlet::config::ThemeConfig config;
    config.templatePath = templatePath;

    QString stylesheet;
    QString error;
    const bool ok = tunlet::theme::ThemeLoader::buildStylesheet(config, &stylesheet, &error);

    REQUIRE_FALSE(ok);
    REQUIRE(stylesheet.isEmpty());
    REQUIRE(error.contains("missing theme token"));
}

TEST_CASE("ThemeLoader preserves the previous stylesheet after fatal apply failure", "[theme]") {
    QApplication *app = testApplication();
    app->setStyleSheet("QWidget { color: chartreuse; }");

    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    const QString templatePath = dir.path() + "/broken.qss.in";
    writeUtf8File(templatePath, "QWidget { color: {{color.text.unknown}}; }\n");

    tunlet::config::ThemeConfig config;
    config.templatePath = templatePath;

    QString error;
    const bool ok = tunlet::theme::ThemeLoader::applyTheme(*app, config, &error);

    REQUIRE_FALSE(ok);
    REQUIRE(error.contains("missing theme token"));
    REQUIRE(app->styleSheet() == "QWidget { color: chartreuse; }");
}
