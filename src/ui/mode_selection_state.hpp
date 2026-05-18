#pragma once

#include "clash/mode_controller.hpp"
#include "config/app_config.hpp"

#include <QString>
#include <QVector>

namespace tunlet::ui {

inline bool hasConfiguredProfile(const QVector<config::ClashModeProfile> &profiles, const QString &profileName) {
    for (const auto &profile : profiles) {
        if (profile.name == profileName) {
            return true;
        }
    }
    return false;
}

inline QString syncModeProfileSelection(const QVector<config::ClashModeProfile> &profiles,
                                        const clash::ModeStatus &status,
                                        const QString &currentSelection) {
    const bool currentSelectionValid = hasConfiguredProfile(profiles, currentSelection);
    const bool activeProfileValid = hasConfiguredProfile(profiles, status.currentProfileName);
    const bool preservePendingSelection =
        status.busy && currentSelectionValid &&
        (status.currentProfileName.isEmpty() || currentSelection != status.currentProfileName);

    if (preservePendingSelection) {
        return currentSelection;
    }
    if (activeProfileValid) {
        return status.currentProfileName;
    }
    return {};
}

}  // namespace tunlet::ui
