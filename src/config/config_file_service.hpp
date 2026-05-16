#pragma once

#include "config/app_config.hpp"

#include <QString>

namespace tunlet::config {

struct ConfigFileLoadResult {
    bool ok = false;
    QString text;
    QString error;
};

struct ConfigFileValidationResult {
    bool ok = false;
    QString error;
    int errorLine = -1;
    int errorColumn = -1;
};

struct ConfigFileSaveResult {
    bool ok = false;
    QString error;
};

class ConfigFileService {
public:
    explicit ConfigFileService(EditingConfig editingConfig);

    ConfigFileLoadResult loadFile(const QString &path) const;
    ConfigFileValidationResult validateConfigText(const QString &path, const QString &text) const;
    ConfigFileSaveResult saveFile(const QString &path, const QString &text) const;

private:
    ConfigFileSaveResult createBackupIfNeeded(const QString &path) const;

    EditingConfig m_editingConfig;
};

}  // namespace tunlet::config
