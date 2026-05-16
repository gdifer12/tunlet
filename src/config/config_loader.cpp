#include "config/config_loader.hpp"

#include "app/application_paths.hpp"

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

RuleSetFileConfig parseRuleSetFile(const YAML::Node &node, const QString &context) {
    if (!node || !node.IsMap()) {
        throw std::runtime_error(QString("%1: expected a map").arg(context).toStdString());
    }

    RuleSetFileConfig file;
    file.name = requireString(node, "name", context);
    file.path = tunlet::app::expandUserPath(requireString(node, "path", context));
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

AppConfig parseConfigRoot(const YAML::Node &root, const QString &sourcePath) {
    if (!root.IsMap()) {
        throw std::runtime_error("config root must be a map");
    }

    AppConfig config;
    config.configPath = sourcePath;

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

    config.ruleSets.forceProxyPath = tunlet::app::expandUserPath(requireString(ruleSets, "forceProxyPath", "ruleSets"));
    config.ruleSets.forceDirectPath = tunlet::app::expandUserPath(requireString(ruleSets, "forceDirectPath", "ruleSets"));
    config.ruleSets.autoProxyPath = tunlet::app::expandUserPath(requireString(ruleSets, "autoProxyPath", "ruleSets"));
    config.ruleSets.autoDirectPath = tunlet::app::expandUserPath(requireString(ruleSets, "autoDirectPath", "ruleSets"));
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
            config.ruleSets.extraFiles.push_back(parseRuleSetFile(item, QString("ruleSets.extraFiles[%1]").arg(index)));
            ++index;
        }
    }

    if (const YAML::Node diagnostics = root["diagnostics"]) {
        config.diagnostics.enabled = readBool(diagnostics, "enabled", true);
        config.diagnostics.refreshIntervalMs = readInt(diagnostics, "refreshIntervalMs", 10000);
        config.diagnostics.requestTimeoutMs = readInt(diagnostics, "requestTimeoutMs", 5000);
        if (config.diagnostics.refreshIntervalMs <= 0) {
            throw std::runtime_error("diagnostics.refreshIntervalMs must be > 0");
        }
        if (config.diagnostics.requestTimeoutMs <= 0) {
            throw std::runtime_error("diagnostics.requestTimeoutMs must be > 0");
        }

        if (const YAML::Node externalIp = diagnostics["externalIp"]) {
            config.diagnostics.externalIp.enabled = readBool(externalIp, "enabled", false);
            config.diagnostics.externalIp.ipv4Url = externalIp["ipv4Url"] ? QString::fromStdString(externalIp["ipv4Url"].as<std::string>()) : QString{};
            config.diagnostics.externalIp.ipv6Url = externalIp["ipv6Url"] ? QString::fromStdString(externalIp["ipv6Url"].as<std::string>()) : QString{};
            config.diagnostics.externalIp.proxyUrl = externalIp["proxyUrl"] ? QString::fromStdString(externalIp["proxyUrl"].as<std::string>()) : QString{};
            config.diagnostics.externalIp.locationUrlTemplate =
                externalIp["locationUrlTemplate"]
                    ? QString::fromStdString(externalIp["locationUrlTemplate"].as<std::string>())
                    : config.diagnostics.externalIp.locationUrlTemplate;
        }
    }

    if (const YAML::Node theme = root["theme"]) {
        config.theme.qssPath = theme["qssPath"] ? tunlet::app::expandUserPath(QString::fromStdString(theme["qssPath"].as<std::string>())) : QString{};
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
