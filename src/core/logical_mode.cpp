#include "core/logical_mode.hpp"

namespace tunlet::core {

QString logicalModeToString(LogicalMode mode) {
    switch (mode) {
    case LogicalMode::Direct:
        return "direct";
    case LogicalMode::Proxy:
        return "proxy";
    case LogicalMode::Auto:
        return "auto";
    case LogicalMode::Unknown:
    default:
        return "unknown";
    }
}

}  // namespace tunlet::core
