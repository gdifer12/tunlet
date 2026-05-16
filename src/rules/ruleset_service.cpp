#include "rules/ruleset_service.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QSaveFile>

namespace tunlet::rules {

RuleSetService::RuleSetService(config::EditingConfig editingConfig)
    : m_editingConfig(std::move(editingConfig)) {}

FileLoadResult RuleSetService::loadFile(const QString &path) const {
    QFile file(path);
    if (!file.exists()) {
        return {.ok = false, .error = QString("file not found: %1").arg(path)};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {.ok = false, .error = QString("failed to open file: %1").arg(file.errorString())};
    }

    const QByteArray data = file.readAll();
    return {.ok = true, .text = QString::fromUtf8(data)};
}

ValidationResult RuleSetService::parseJson(const QString &text) const {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        return {.ok = false, .error = QString("invalid JSON: %1").arg(parseError.errorString())};
    }

    return {.ok = true, .formattedText = QString::fromUtf8(document.toJson(QJsonDocument::Indented))};
}

ValidationResult RuleSetService::validateJson(const QString &text) const {
    return parseJson(text);
}

SaveResult RuleSetService::createBackupIfNeeded(const QString &path) const {
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

SaveResult RuleSetService::saveFile(const QString &path, const QString &text) const {
    const auto validation = parseJson(text);
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

    if (file.write(validation.formattedText.toUtf8()) < 0) {
        return {.ok = false, .error = QString("failed to write file: %1").arg(file.errorString())};
    }

    if (!file.commit()) {
        return {.ok = false, .error = QString("failed to commit file: %1").arg(file.errorString())};
    }

    return {.ok = true};
}

}  // namespace tunlet::rules
