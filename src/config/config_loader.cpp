#include "config/config_loader.hpp"

#include "app/application_paths.hpp"

#include <QDir>
#include <QFileInfo>

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
    config.diagnostics.connection.location.enabled = true;
    config.diagnostics.connection.location.databasePath = defaultGeoDbPath(config.configRoute, formatConfigRoute(sourcePath));

    const YAML::Node clashApi = root["clashApi"];
    if (!clashApi || !clashApi.IsMap()) {
        throw std::runtime_error("missing clashApi section");
    }

    config.clashApi.host = clashApi["host"] ? QString::fromStdString(clashApi["host"].as<std::string>()) : QString("127.0.0.1");
    config.clashApi.port = requirePort(clashApi, "port", "clashApi");
    config.clashApi.profiles = defaultProfiles();

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
                config.diagnostics.connection.location.enabled = readBool(location, "enabled", true);
                if (location["databasePath"]) {
                    config.diagnostics.connection.location.databasePath =
                        tunlet::app::resolveConfiguredPath(
                            QString::fromStdString(location["databasePath"].as<std::string>()),
                            config.configRoute,
                            fallbackBasePath);
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
