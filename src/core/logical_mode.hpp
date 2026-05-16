#pragma once

#include <QString>

namespace tunlet::core {

enum class LogicalMode {
    Direct,
    Proxy,
    Auto,
    Unknown,
};

QString logicalModeToString(LogicalMode mode);

}  // namespace tunlet::core
