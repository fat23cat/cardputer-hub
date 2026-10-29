#pragma once

namespace cardputer_hub::apps {

// Navigation Actions handled by ApplicationShell and dispatched by any
// screen or Mini App. The G0 status toggle stays private to the Shell.
inline constexpr char uiSettingsActionId[] = "ui.settings";
inline constexpr char uiBluetoothActionId[] = "ui.bluetooth";
inline constexpr char uiWifiActionId[] = "ui.wifi";
inline constexpr char uiLauncherActionId[] = "ui.launcher";
inline constexpr char uiBackActionId[] = "ui.back";
inline constexpr char appOpenActionId[] = "app.open";
inline constexpr char appCloseActionId[] = "app.close";
inline constexpr const char* shellNavigationActionIds[] = {
    uiSettingsActionId, uiBluetoothActionId, uiWifiActionId,  uiLauncherActionId,
    uiBackActionId,     appOpenActionId,     appCloseActionId};

} // namespace cardputer_hub::apps
