#pragma once

#include "clash/mode_controller.hpp"
#include "config/app_config.hpp"

#include <QString>
#include <QVector>

namespace tunlet::ui {

inline bool hasConfiguredProfile(const QVector<clash::ModeOption> &profiles, const QString &optionId) {
    for (const auto &profile : profiles) {
        if (profile.id == optionId) {
            return true;
        }
    }
    return false;
}

inline QString syncModeProfileSelection(const QVector<clash::ModeOption> &profiles,
                                        const clash::ModeStatus &status,
                                        const QString &currentSelection) {
    const bool currentSelectionValid = hasConfiguredProfile(profiles, currentSelection);
    const bool activeProfileValid = hasConfiguredProfile(profiles, status.currentProfileId);
    const bool preservePendingSelection =
        status.busy && currentSelectionValid &&
        (status.currentProfileId.isEmpty() || currentSelection != status.currentProfileId);

    if (preservePendingSelection) {
        return currentSelection;
    }
    if (activeProfileValid) {
        return status.currentProfileId;
    }
    return {};
}

}  // namespace tunlet::ui
