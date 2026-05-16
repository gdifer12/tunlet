#include "config/config_file_service.hpp"

#include "config/config_loader.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <yaml-cpp/yaml.h>

namespace tunlet::config {

ConfigFileService::ConfigFileService(EditingConfig editingConfig)
    : m_editingConfig(std::move(editingConfig)) {}

void ConfigFileService::updateEditingConfig(const EditingConfig &editingConfig) {
    m_editingConfig = editingConfig;
}

ConfigFileLoadResult ConfigFileService::loadFile(const QString &path) const {
    QFile file(path);
    if (!file.exists()) {
        return {.ok = false, .error = QString("file not found: %1").arg(path)};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {.ok = false, .error = QString("failed to open file: %1").arg(file.errorString())};
    }

    return {.ok = true, .text = QString::fromUtf8(file.readAll())};
}

ConfigFileValidationResult ConfigFileService::validateConfigText(const QString &path, const QString &text) const {
    try {
        ConfigLoader::loadFromData(text, path);
        return {.ok = true};
    } catch (const YAML::Exception &ex) {
        return {
            .ok = false,
            .error = QString::fromStdString(ex.msg),
            .errorLine = ex.mark.line >= 0 ? ex.mark.line + 1 : -1,
            .errorColumn = ex.mark.column >= 0 ? ex.mark.column + 1 : -1,
        };
    } catch (const std::exception &ex) {
        return {.ok = false, .error = ex.what()};
    }
}

ConfigFileSaveResult ConfigFileService::createBackupIfNeeded(const QString &path) const {
    if (!m_editingConfig.createBackup) {
        return {.ok = true};
    }

    QFileInfo info(path);
    if (!info.exists()) {
        return {.ok = true};
    }

    const QString backupPath = path + m_editingConfig.backupSuffix;
    QFile::remove(backupPath);
    if (!QFile::copy(path, backupPath)) {
        return {.ok = false, .error = QString("failed to create backup file: %1").arg(backupPath)};
    }

    return {.ok = true};
}

ConfigFileSaveResult ConfigFileService::saveFile(const QString &path, const QString &text) const {
    const auto validation = validateConfigText(path, text);
    if (!validation.ok) {
        return {.ok = false, .error = validation.error};
    }

    const auto backupResult = createBackupIfNeeded(path);
    if (!backupResult.ok) {
        return backupResult;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {.ok = false, .error = QString("failed to open file for writing: %1").arg(file.errorString())};
    }

    if (file.write(text.toUtf8()) < 0) {
        return {.ok = false, .error = QString("failed to write file: %1").arg(file.errorString())};
    }

    if (!file.commit()) {
        return {.ok = false, .error = QString("failed to commit file: %1").arg(file.errorString())};
    }

    return {.ok = true};
}

}  // namespace tunlet::config
