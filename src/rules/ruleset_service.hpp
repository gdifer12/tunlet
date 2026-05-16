#pragma once

#include "config/app_config.hpp"

#include <QJsonDocument>
#include <QString>

namespace tunlet::rules {

struct FileLoadResult {
    bool ok = false;
    QString text;
    QString error;
};

struct ValidationResult {
    bool ok = false;
    QString formattedText;
    QString error;
};

struct SaveResult {
    bool ok = false;
    QString error;
};

class RuleSetService {
public:
    explicit RuleSetService(config::EditingConfig editingConfig);

    FileLoadResult loadFile(const QString &path) const;
    ValidationResult validateJson(const QString &text) const;
    SaveResult saveFile(const QString &path, const QString &text) const;

private:
    ValidationResult parseJson(const QString &text) const;
    SaveResult createBackupIfNeeded(const QString &path) const;

    config::EditingConfig m_editingConfig;
};

}  // namespace tunlet::rules
