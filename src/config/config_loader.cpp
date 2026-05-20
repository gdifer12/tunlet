#include "config/config_loader.hpp"

#include "app/application_paths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <yaml-cpp/yaml.h>

#include <array>
#include <stdexcept>

namespace tunlet::config {
namespace {

QString requireString(const YAML::Node &node, const char *key, const QString &context) {
    const YAML::Node value = node[key];
    if (!value || !value.IsScalar()) {
        throw std::runtime_error(QString("%1: missing or invalid '%2'").arg(context, key).toStdString());
    }

    return QString::fromStdString(value.as<std::string>());
}

QString readString(const YAML::Node &node, const char *key, const QString &defaultValue, const QString &context) {
    const YAML::Node value = node[key];
    if (!value) {
        return defaultValue;
    }
    if (!value.IsScalar()) {
        throw std::runtime_error(QString("%1: '%2' must be a string").arg(context, key).toStdString());
    }
    return QString::fromStdString(value.as<std::string>());
}

bool readBool(const YAML::Node &node, const char *key, bool defaultValue) {
    const YAML::Node value = node[key];
    return value ? value.as<bool>() : defaultValue;
}

int readInt(const YAML::Node &node, const char *key, int defaultValue) {
    const YAML::Node value = node[key];
    return value ? value.as<int>() : defaultValue;
}

QStringList readStringList(const YAML::Node &node, const char *key, const QString &context) {
    const YAML::Node value = node[key];
    if (!value) {
        return {};
    }
    if (!value.IsSequence()) {
        throw std::runtime_error(QString("%1: '%2' must be a sequence").arg(context, key).toStdString());
    }

    QStringList items;
    int index = 0;
    for (const auto &item : value) {
        if (!item.IsScalar()) {
            throw std::runtime_error(
                QString("%1: '%2[%3]' must be a scalar").arg(context, key).arg(index).toStdString());
        }
        items.push_back(QString::fromStdString(item.as<std::string>()));
        ++index;
    }
    return items;
}

quint16 requirePort(const YAML::Node &node, const char *key, const QString &context) {
    const YAML::Node value = node[key];
    if (!value || !value.IsScalar()) {
        throw std::runtime_error(QString("%1: missing or invalid '%2'").arg(context, key).toStdString());
    }

    const int port = value.as<int>();
    if (port <= 0 || port > 65535) {
        throw std::runtime_error(QString("%1: port must be in range 1..65535").arg(context).toStdString());
    }

    return static_cast<quint16>(port);
}

ClashModeProfile parseModeProfile(const YAML::Node &node, const QString &context) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error(QString("%1: expected a map").arg(context).toStdString());
    }

    ClashModeProfile profile;
    profile.name = requireString(node, "name", context);
    profile.mode = node["mode"] ? QString::fromStdString(node["mode"].as<std::string>()) : profile.name;
    profile.desc = node["desc"] ? QString::fromStdString(node["desc"].as<std::string>()) : QString{};
    if (profile.name.trimmed().isEmpty() || profile.mode.trimmed().isEmpty()) {
        throw std::runtime_error(QString("%1: name and mode must not be empty").arg(context).toStdString());
    }
    return profile;
}

RuleSetFileConfig parseRuleSetFile(const YAML::Node &node,
                                   const QString &context,
                                   const QString &configRoute,
                                   const QString &fallbackBasePath) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error(QString("%1: expected a map").arg(context).toStdString());
    }

    RuleSetFileConfig file;
    file.name = requireString(node, "name", context);
    file.path = tunlet::app::resolveConfiguredPath(requireString(node, "path", context), configRoute, fallbackBasePath);
    file.description = node["description"] ? QString::fromStdString(node["description"].as<std::string>()) : QString{};
    return file;
}

void validateNonEmptyPath(const QString &value, const QString &fieldName) {
    if (value.trimmed().isEmpty()) {
        throw std::runtime_error(QString("ruleSets.%1 must not be empty").arg(fieldName).toStdString());
    }
}

QVector<ClashModeProfile> defaultProfiles() {
    return {
        {"direct", "direct", "Built-in logical direct mode"},
        {"proxy", "global", "Built-in logical proxy mode mapped to Clash global"},
        {"auto", "rule", "Built-in logical auto mode mapped to Clash rule"},
    };
}

config::DiagnosticsCommandConfig defaultIpv4Command() {
    return {
        "curl",
        {"-4", "-sS", "--noproxy", "*", "https://api.ipify.org"},
    };
}

config::DiagnosticsCommandConfig defaultTimingCommand() {
    return {
        "curl",
        {
            "-4",
            "-sS",
            "--noproxy",
            "*",
            "-o",
            "/dev/null",
            "-w",
            "dns=%{time_namelookup}s connect=%{time_connect}s tls=%{time_appconnect}s total=%{time_total}s\n",
            "https://www.gstatic.com/generate_204",
        },
    };
}

config::DiagnosticsCommandConfig defaultDnsCommand() {
    return {
        "dig",
        {"-4", "+short", "TXT", "o-o.myaddr.l.google.com"},
    };
}

QString defaultGeoDbPath(const QString &configRoute, const QString &fallbackBasePath) {
    return tunlet::app::resolveConfiguredPath("GeoLite2-City.mmdb", configRoute, fallbackBasePath);
}

QString defaultGeoAsnDbPath(const QString &configRoute, const QString &fallbackBasePath) {
    return tunlet::app::resolveConfiguredPath("GeoLite2-ASN.mmdb", configRoute, fallbackBasePath);
}

QString defaultGeoCachePath(const QString &configRoute, const QString &fallbackBasePath) {
    return tunlet::app::resolveConfiguredPath("geoip-cache.json", configRoute, fallbackBasePath);
}

QString formatConfigRoute(const QString &sourcePath) {
    QString configPath = sourcePath.trimmed();
    if (configPath.isEmpty()) {
        configPath = tunlet::app::defaultConfigPath();
    }

    const QFileInfo configInfo(tunlet::app::expandUserPath(configPath));
    const QString configDir = configInfo.dir().absolutePath();
    if (configDir.isEmpty()) {
        return QDir::homePath() + "/.config/tunlet";
    }
    return configDir;
}

QString parseConfigRoute(const YAML::Node &root, const QString &sourcePath) {
    const QString fallbackBasePath = formatConfigRoute(sourcePath);
    if (!root["configRoute"]) {
        return fallbackBasePath;
    }

    const YAML::Node value = root["configRoute"];
    if (!value.IsScalar()) {
        throw std::runtime_error("configRoute must be a string");
    }

    const QString configuredRoute = QString::fromStdString(value.as<std::string>());
    const QString resolvedRoute = tunlet::app::resolveConfiguredPath(configuredRoute, QString(), fallbackBasePath);
    if (resolvedRoute.trimmed().isEmpty()) {
        throw std::runtime_error("configRoute must not be empty");
    }
    return resolvedRoute;
}

config::DiagnosticsCommandConfig parseDiagnosticsCommand(const YAML::Node &node,
                                                        const QString &context,
                                                        const config::DiagnosticsCommandConfig &defaults) {
    if (!node) {
        return defaults;
    }
    if (!node.IsMap()) {
        throw std::runtime_error(QString("%1: expected a map").arg(context).toStdString());
    }

    config::DiagnosticsCommandConfig command = defaults;
    if (node["executable"]) {
        command.executable = QString::fromStdString(node["executable"].as<std::string>());
    }
    if (node["args"]) {
        command.args = readStringList(node, "args", context);
    }

    if (command.executable.trimmed().isEmpty()) {
        throw std::runtime_error(QString("%1.executable must not be empty").arg(context).toStdString());
    }

    return command;
}

config::DiagnosticsLocationMode parseLocationMode(const QString &modeText) {
    const QString normalized = modeText.trimmed().toLower();
    if (normalized.isEmpty() || normalized == "local_db") {
        return config::DiagnosticsLocationMode::LocalDb;
    }
    if (normalized == "disabled") {
        return config::DiagnosticsLocationMode::Disabled;
    }
    if (normalized == "dynamic_cache") {
        return config::DiagnosticsLocationMode::DynamicCache;
    }

    throw std::runtime_error(QString("unsupported diagnostics.connection.location.mode: %1").arg(modeText).toStdString());
}

config::LoggingLevel parseLoggingLevel(const QString &levelText) {
    const QString normalized = levelText.trimmed().toLower();
    if (normalized.isEmpty() || normalized == "info") {
        return config::LoggingLevel::Info;
    }
    if (normalized == "warning") {
        return config::LoggingLevel::Warning;
    }
    if (normalized == "error") {
        return config::LoggingLevel::Error;
    }

    throw std::runtime_error(QString("unsupported logging.level: %1").arg(levelText).toStdString());
}

AppConfig parseConfigRoot(const YAML::Node &root, const QString &sourcePath) {
    if (!root.IsMap()) {
        throw std::runtime_error("config root must be a map");
    }

    AppConfig config;
    config.configPath = sourcePath;
    config.configRoute = parseConfigRoute(root, sourcePath);
    config.diagnostics.connection.ipv4 = defaultIpv4Command();
    config.diagnostics.connection.timing = defaultTimingCommand();
    config.diagnostics.connection.dns = defaultDnsCommand();
    config.diagnostics.connection.location.mode = config::DiagnosticsLocationMode::LocalDb;
    config.diagnostics.connection.location.localDb.databasePath = defaultGeoDbPath(config.configRoute, formatConfigRoute(sourcePath));
    config.diagnostics.connection.location.localDb.asnDatabasePath = defaultGeoAsnDbPath(config.configRoute, formatConfigRoute(sourcePath));
    config.diagnostics.connection.location.dynamicCache.cachePath =
        defaultGeoCachePath(config.configRoute, formatConfigRoute(sourcePath));

    const YAML::Node clashApi = root["clashApi"];
    if (!clashApi || !clashApi.IsMap()) {
        throw std::runtime_error("missing clashApi section");
    }

    config.clashApi.host = clashApi["host"] ? QString::fromStdString(clashApi["host"].as<std::string>()) : QString("127.0.0.1");
    config.clashApi.port = requirePort(clashApi, "port", "clashApi");
    config.clashApi.modeSyncIntervalMs = readInt(clashApi, "modeSyncIntervalMs", config.clashApi.modeSyncIntervalMs);
    if (config.clashApi.modeSyncIntervalMs < 0) {
        throw std::runtime_error("clashApi.modeSyncIntervalMs must be >= 0");
    }
    config.clashApi.disableDefaultProfiles = readBool(clashApi, "disableDefaultProfiles", false);
    if (!config.clashApi.disableDefaultProfiles) {
        config.clashApi.profiles = defaultProfiles();
    }

    if (const YAML::Node profiles = clashApi["profiles"]) {
        if (!profiles.IsSequence()) {
            throw std::runtime_error("clashApi.profiles must be a sequence");
        }

        int index = 0;
        for (const auto &item : profiles) {
            config.clashApi.profiles.push_back(parseModeProfile(item, QString("clashApi.profiles[%1]").arg(index)));
            ++index;
        }
    }

    const YAML::Node ruleSets = root["ruleSets"];
    if (!ruleSets || !ruleSets.IsMap()) {
        throw std::runtime_error("missing ruleSets section");
    }

    const QString fallbackBasePath = formatConfigRoute(sourcePath);
    config.ruleSets.forceProxyPath =
        tunlet::app::resolveConfiguredPath(requireString(ruleSets, "forceProxyPath", "ruleSets"), config.configRoute, fallbackBasePath);
    config.ruleSets.forceDirectPath =
        tunlet::app::resolveConfiguredPath(requireString(ruleSets, "forceDirectPath", "ruleSets"), config.configRoute, fallbackBasePath);
    config.ruleSets.autoProxyPath =
        tunlet::app::resolveConfiguredPath(requireString(ruleSets, "autoProxyPath", "ruleSets"), config.configRoute, fallbackBasePath);
    config.ruleSets.autoDirectPath =
        tunlet::app::resolveConfiguredPath(requireString(ruleSets, "autoDirectPath", "ruleSets"), config.configRoute, fallbackBasePath);
    validateNonEmptyPath(config.ruleSets.forceProxyPath, "forceProxyPath");
    validateNonEmptyPath(config.ruleSets.forceDirectPath, "forceDirectPath");
    validateNonEmptyPath(config.ruleSets.autoProxyPath, "autoProxyPath");
    validateNonEmptyPath(config.ruleSets.autoDirectPath, "autoDirectPath");

    if (const YAML::Node extraFiles = ruleSets["extraFiles"]) {
        if (!extraFiles.IsSequence()) {
            throw std::runtime_error("ruleSets.extraFiles must be a sequence");
        }

        int index = 0;
        for (const auto &item : extraFiles) {
            config.ruleSets.extraFiles.push_back(
                parseRuleSetFile(item, QString("ruleSets.extraFiles[%1]").arg(index), config.configRoute, fallbackBasePath));
            ++index;
        }
    }

    if (const YAML::Node diagnostics = root["diagnostics"]) {
        config.diagnostics.enabled = readBool(diagnostics, "enabled", true);
        config.diagnostics.refreshIntervalMs = readInt(diagnostics, "refreshIntervalMs", 180000);
        config.diagnostics.requestTimeoutMs = readInt(diagnostics, "requestTimeoutMs", 5000);
        if (config.diagnostics.refreshIntervalMs <= 0) {
            throw std::runtime_error("diagnostics.refreshIntervalMs must be > 0");
        }
        if (config.diagnostics.requestTimeoutMs <= 0) {
            throw std::runtime_error("diagnostics.requestTimeoutMs must be > 0");
        }

        if (diagnostics["externalIp"]) {
            throw std::runtime_error("diagnostics.externalIp is no longer supported; use diagnostics.connection");
        }

        if (const YAML::Node connection = diagnostics["connection"]) {
            if (!connection.IsMap()) {
                throw std::runtime_error("diagnostics.connection must be a map");
            }

            config.diagnostics.connection.ipv4 = parseDiagnosticsCommand(
                connection["ipv4"],
                "diagnostics.connection.ipv4",
                config.diagnostics.connection.ipv4);
            config.diagnostics.connection.timing = parseDiagnosticsCommand(
                connection["timing"],
                "diagnostics.connection.timing",
                config.diagnostics.connection.timing);
            config.diagnostics.connection.dns = parseDiagnosticsCommand(
                connection["dns"],
                "diagnostics.connection.dns",
                config.diagnostics.connection.dns);

            if (const YAML::Node location = connection["location"]) {
                if (!location.IsMap()) {
                    throw std::runtime_error("diagnostics.connection.location must be a map");
                }
                if (location["mode"]) {
                    config.diagnostics.connection.location.mode =
                        parseLocationMode(QString::fromStdString(location["mode"].as<std::string>()));
                } else if (location["enabled"]) {
                    config.diagnostics.connection.location.mode =
                        readBool(location, "enabled", true) ? config::DiagnosticsLocationMode::LocalDb
                                                             : config::DiagnosticsLocationMode::Disabled;
                }

                if (location["databasePath"]) {
                    config.diagnostics.connection.location.localDb.databasePath =
                        tunlet::app::resolveConfiguredPath(
                            QString::fromStdString(location["databasePath"].as<std::string>()),
                            config.configRoute,
                            fallbackBasePath);
                }
                if (location["downloadUrl"]) {
                    config.diagnostics.connection.location.localDb.downloadUrl =
                        QString::fromStdString(location["downloadUrl"].as<std::string>()).trimmed();
                }

                if (const YAML::Node localDb = location["localDb"]) {
                    if (!localDb.IsMap()) {
                        throw std::runtime_error("diagnostics.connection.location.localDb must be a map");
                    }
                    if (localDb["databasePath"]) {
                        config.diagnostics.connection.location.localDb.databasePath =
                            tunlet::app::resolveConfiguredPath(
                                QString::fromStdString(localDb["databasePath"].as<std::string>()),
                                config.configRoute,
                                fallbackBasePath);
                    }
                    if (localDb["asnDatabasePath"]) {
                        config.diagnostics.connection.location.localDb.asnDatabasePath =
                            tunlet::app::resolveConfiguredPath(
                                QString::fromStdString(localDb["asnDatabasePath"].as<std::string>()),
                                config.configRoute,
                                fallbackBasePath);
                    }
                    if (localDb["downloadUrl"]) {
                        config.diagnostics.connection.location.localDb.downloadUrl =
                            QString::fromStdString(localDb["downloadUrl"].as<std::string>()).trimmed();
                    }
                }

                if (const YAML::Node dynamicCache = location["dynamicCache"]) {
                    if (!dynamicCache.IsMap()) {
                        throw std::runtime_error("diagnostics.connection.location.dynamicCache must be a map");
                    }
                    if (dynamicCache["provider"]) {
                        config.diagnostics.connection.location.dynamicCache.provider =
                            QString::fromStdString(dynamicCache["provider"].as<std::string>()).trimmed();
                    }
                    if (dynamicCache["url"]) {
                        config.diagnostics.connection.location.dynamicCache.url =
                            QString::fromStdString(dynamicCache["url"].as<std::string>()).trimmed();
                    }
                    if (dynamicCache["cachePath"]) {
                        config.diagnostics.connection.location.dynamicCache.cachePath =
                            tunlet::app::resolveConfiguredPath(
                                QString::fromStdString(dynamicCache["cachePath"].as<std::string>()),
                                config.configRoute,
                                fallbackBasePath);
                    }
                    config.diagnostics.connection.location.dynamicCache.baseRefreshDays =
                        readInt(dynamicCache, "baseRefreshDays", config.diagnostics.connection.location.dynamicCache.baseRefreshDays);
                    config.diagnostics.connection.location.dynamicCache.randomShiftDays =
                        readInt(dynamicCache, "randomShiftDays", config.diagnostics.connection.location.dynamicCache.randomShiftDays);
                    config.diagnostics.connection.location.dynamicCache.timeoutMs =
                        readInt(dynamicCache, "timeoutMs", config.diagnostics.connection.location.dynamicCache.timeoutMs);
                    config.diagnostics.connection.location.dynamicCache.refreshOnStartup =
                        readBool(dynamicCache, "refreshOnStartup", config.diagnostics.connection.location.dynamicCache.refreshOnStartup);
                    config.diagnostics.connection.location.dynamicCache.allowManualRefresh =
                        readBool(dynamicCache, "allowManualRefresh", config.diagnostics.connection.location.dynamicCache.allowManualRefresh);
                }

                if (config.diagnostics.connection.location.localDb.databasePath.trimmed().isEmpty()) {
                    throw std::runtime_error("diagnostics.connection.location.localDb.databasePath must not be empty");
                }
                if (config.diagnostics.connection.location.dynamicCache.provider.trimmed().isEmpty()) {
                    throw std::runtime_error("diagnostics.connection.location.dynamicCache.provider must not be empty");
                }
                if (config.diagnostics.connection.location.dynamicCache.url.trimmed().isEmpty()) {
                    throw std::runtime_error("diagnostics.connection.location.dynamicCache.url must not be empty");
                }
                if (config.diagnostics.connection.location.dynamicCache.cachePath.trimmed().isEmpty()) {
                    throw std::runtime_error("diagnostics.connection.location.dynamicCache.cachePath must not be empty");
                }
                if (config.diagnostics.connection.location.dynamicCache.baseRefreshDays <= 0) {
                    throw std::runtime_error("diagnostics.connection.location.dynamicCache.baseRefreshDays must be > 0");
                }
                if (config.diagnostics.connection.location.dynamicCache.randomShiftDays < 0) {
                    throw std::runtime_error("diagnostics.connection.location.dynamicCache.randomShiftDays must be >= 0");
                }
                if (config.diagnostics.connection.location.dynamicCache.timeoutMs <= 0) {
                    throw std::runtime_error("diagnostics.connection.location.dynamicCache.timeoutMs must be > 0");
                }
                if (config.diagnostics.connection.location.mode == config::DiagnosticsLocationMode::DynamicCache) {
                    const QUrl geoUrl(config.diagnostics.connection.location.dynamicCache.url);
                    if (!geoUrl.isValid() || geoUrl.scheme().compare("https", Qt::CaseInsensitive) != 0) {
                        throw std::runtime_error("diagnostics.connection.location.dynamicCache.url must be a valid https URL");
                    }
                }
            }
        }
    }

    if (const YAML::Node theme = root["theme"]) {
        config.theme.qssPath =
            theme["qssPath"]
                ? tunlet::app::resolveConfiguredPath(QString::fromStdString(theme["qssPath"].as<std::string>()),
                                                     config.configRoute,
                                                     fallbackBasePath)
                : QString{};
    }

    if (const YAML::Node editing = root["editing"]) {
        config.editing.createBackup = readBool(editing, "createBackup", true);
        config.editing.backupSuffix = editing["backupSuffix"] ? QString::fromStdString(editing["backupSuffix"].as<std::string>()) : QString(".bak");
        if (config.editing.backupSuffix.isEmpty()) {
            config.editing.backupSuffix = ".bak";
        }
    }

    if (const YAML::Node tray = root["tray"]) {
        if (!tray.IsMap()) {
            throw std::runtime_error("tray must be a map");
        }
        config.tray.keepRunningWithoutWindow = readBool(tray, "keepRunningWithoutWindow", true);
        config.tray.startHidden = readBool(tray, "startHidden", false);
    }

    if (const YAML::Node logging = root["logging"]) {
        if (!logging.IsMap()) {
            throw std::runtime_error("logging must be a map");
        }

        config.logging.enabled = readBool(logging, "enabled", config.logging.enabled);
        if (logging["level"]) {
            if (!logging["level"].IsScalar()) {
                throw std::runtime_error("logging.level must be a string");
            }
            config.logging.level = parseLoggingLevel(QString::fromStdString(logging["level"].as<std::string>()));
        }
        if (logging["textPath"]) {
            if (!logging["textPath"].IsScalar()) {
                throw std::runtime_error("logging.textPath must be a string");
            }
            config.logging.textPath = tunlet::app::resolveConfiguredPath(
                QString::fromStdString(logging["textPath"].as<std::string>()),
                config.configRoute,
                fallbackBasePath);
        }
        if (logging["jsonlPath"]) {
            if (!logging["jsonlPath"].IsScalar()) {
                throw std::runtime_error("logging.jsonlPath must be a string");
            }
            config.logging.jsonlPath = tunlet::app::resolveConfiguredPath(
                QString::fromStdString(logging["jsonlPath"].as<std::string>()),
                config.configRoute,
                fallbackBasePath);
        }
        if (const YAML::Node rotation = logging["rotation"]) {
            if (!rotation.IsMap()) {
                throw std::runtime_error("logging.rotation must be a map");
            }
            config.logging.rotation.enabled = readBool(rotation, "enabled", config.logging.rotation.enabled);
            config.logging.rotation.maxFileBytes =
                static_cast<qint64>(readInt(rotation, "maxFileBytes", static_cast<int>(config.logging.rotation.maxFileBytes)));
            config.logging.rotation.keepFiles = readInt(rotation, "keepFiles", config.logging.rotation.keepFiles);
        }

        if (config.logging.enabled &&
            config.logging.textPath.trimmed().isEmpty() &&
            config.logging.jsonlPath.trimmed().isEmpty()) {
            throw std::runtime_error("logging.enabled requires logging.textPath or logging.jsonlPath");
        }
        if (!config.logging.textPath.trimmed().isEmpty() &&
            !config.logging.jsonlPath.trimmed().isEmpty() &&
            QFileInfo(config.logging.textPath).absoluteFilePath() == QFileInfo(config.logging.jsonlPath).absoluteFilePath()) {
            throw std::runtime_error("logging.textPath and logging.jsonlPath must be different files");
        }
        if (config.logging.rotation.enabled) {
            if (config.logging.rotation.maxFileBytes <= 0) {
                throw std::runtime_error("logging.rotation.maxFileBytes must be > 0 when rotation is enabled");
            }
            if (config.logging.rotation.keepFiles < 1) {
                throw std::runtime_error("logging.rotation.keepFiles must be >= 1 when rotation is enabled");
            }
        }
    }

    if (const YAML::Node ui = root["ui"]) {
        if (!ui.IsMap()) {
            throw std::runtime_error("ui must be a map");
        }

        if (const YAML::Node textSelection = ui["textSelection"]) {
            if (!textSelection.IsMap()) {
                throw std::runtime_error("ui.textSelection must be a map");
            }
            config.ui.textSelection.enableInformationalLabels =
                readBool(textSelection, "enableInformationalLabels", config.ui.textSelection.enableInformationalLabels);
        }

        if (const YAML::Node keyboard = ui["keyboard"]) {
            if (!keyboard.IsMap()) {
                throw std::runtime_error("ui.keyboard must be a map");
            }

            if (const YAML::Node shortcuts = keyboard["shortcuts"]) {
                if (!shortcuts.IsMap()) {
                    throw std::runtime_error("ui.keyboard.shortcuts must be a map");
                }

                auto &shortcutConfig = config.ui.keyboard.shortcuts;
                shortcutConfig.closeWindowPrimary = readString(
                    shortcuts,
                    "closeWindowPrimary",
                    shortcutConfig.closeWindowPrimary,
                    "ui.keyboard.shortcuts");
                shortcutConfig.closeWindowSecondary = readString(
                    shortcuts,
                    "closeWindowSecondary",
                    shortcutConfig.closeWindowSecondary,
                    "ui.keyboard.shortcuts");
                shortcutConfig.nextPage =
                    readString(shortcuts, "nextPage", shortcutConfig.nextPage, "ui.keyboard.shortcuts");
                shortcutConfig.previousPage =
                    readString(shortcuts, "previousPage", shortcutConfig.previousPage, "ui.keyboard.shortcuts");
                shortcutConfig.pageMain =
                    readString(shortcuts, "pageMain", shortcutConfig.pageMain, "ui.keyboard.shortcuts");
                shortcutConfig.pageRules =
                    readString(shortcuts, "pageRules", shortcutConfig.pageRules, "ui.keyboard.shortcuts");
                shortcutConfig.pageSettings =
                    readString(shortcuts, "pageSettings", shortcutConfig.pageSettings, "ui.keyboard.shortcuts");
                shortcutConfig.openModeSelector = readString(
                    shortcuts,
                    "openModeSelector",
                    shortcutConfig.openModeSelector,
                    "ui.keyboard.shortcuts");
                shortcutConfig.openRuleFileSelector = readString(
                    shortcuts,
                    "openRuleFileSelector",
                    shortcutConfig.openRuleFileSelector,
                    "ui.keyboard.shortcuts");
                shortcutConfig.refreshRuntime = readString(
                    shortcuts,
                    "refreshRuntime",
                    shortcutConfig.refreshRuntime,
                    "ui.keyboard.shortcuts");
                shortcutConfig.refreshLocationData = readString(
                    shortcuts,
                    "refreshLocationData",
                    shortcutConfig.refreshLocationData,
                    "ui.keyboard.shortcuts");
                shortcutConfig.validateEditor = readString(
                    shortcuts,
                    "validateEditor",
                    shortcutConfig.validateEditor,
                    "ui.keyboard.shortcuts");
                shortcutConfig.saveEditor =
                    readString(shortcuts, "saveEditor", shortcutConfig.saveEditor, "ui.keyboard.shortcuts");
                shortcutConfig.reloadEditor =
                    readString(shortcuts, "reloadEditor", shortcutConfig.reloadEditor, "ui.keyboard.shortcuts");
            }
        }
    }

    return config;
}

}  // namespace

AppConfig ConfigLoader::loadFromPath(const QString &path) {
    const QString expandedPath = tunlet::app::expandUserPath(path);
    const QFileInfo configInfo(expandedPath);
    if (!configInfo.exists() || !configInfo.isFile()) {
        throw std::runtime_error(QString("config file not found: %1").arg(expandedPath).toStdString());
    }

    const YAML::Node root = YAML::LoadFile(expandedPath.toStdString());
    return parseConfigRoot(root, expandedPath);
}

AppConfig ConfigLoader::loadFromData(const QString &data, const QString &sourcePath) {
    const YAML::Node root = YAML::Load(data.toStdString());
    return parseConfigRoot(root, sourcePath);
}

}  // namespace tunlet::config
