#pragma once

#include "config/app_config.hpp"

namespace tunlet::config {

class ConfigLoader {
public:
    static AppConfig loadFromPath(const QString &path);
    static AppConfig loadFromData(const QString &data, const QString &sourcePath = QString{});
};

}  // namespace tunlet::config
