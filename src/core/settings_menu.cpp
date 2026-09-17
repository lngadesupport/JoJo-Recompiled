#include "core/settings_menu.h"
#include <utility>

namespace jojo {

SettingsMenuSession::SettingsMenuSession(AppSettings baseline)
    : baseline_(std::move(baseline)), draft_(baseline_) {}

Result<void> SettingsMenuSession::validate_player(std::size_t player) {
    if (player >= input_player_count) {
        return Result<void>::failure(ErrorCode::invalid_argument, "player index is outside supported range");
    }
    return Result<void>::success();
}

Result<void> SettingsMenuSession::select_player(std::size_t player) {
    auto valid = validate_player(player);
    if (!valid) return valid;
    selected_player_ = player;
    return Result<void>::success();
}

Result<void> SettingsMenuSession::set_graphics(const GraphicsSettings& graphics) {
    if (!validate_graphics(graphics)) {
        return Result<void>::failure(ErrorCode::invalid_settings, "graphics settings are outside supported ranges");
    }
    draft_.graphics = graphics;
    dirty_ = true;
    return Result<void>::success();
}

Result<void> SettingsMenuSession::set_audio(const AudioSettings& audio) {
    if (!validate_audio(audio)) {
        return Result<void>::failure(ErrorCode::invalid_settings, "audio settings are outside supported ranges");
    }
    draft_.audio = audio;
    dirty_ = true;
    return Result<void>::success();
}

Result<void> SettingsMenuSession::select_device(std::size_t player,
                                                std::string device_id,
                                                const InputDeviceRegistry& registry) {
    auto valid = validate_player(player);
    if (!valid) return valid;
    if (!registry.contains(device_id)) {
        return Result<void>::failure(ErrorCode::invalid_argument, "selected input device is not connected: " + device_id);
    }
    draft_.input.players[player].selected_device = std::move(device_id);
    dirty_ = true;
    return Result<void>::success();
}

Result<void> SettingsMenuSession::bind_action(std::size_t player,
                                              GameAction action,
                                              InputBinding binding) {
    auto valid = validate_player(player);
    if (!valid) return valid;
    if (binding.device_id.empty() || binding.code.empty()) {
        return Result<void>::failure(ErrorCode::invalid_argument, "input binding device/code cannot be empty");
    }
    draft_.input.players[player].bindings[action] = std::move(binding);
    dirty_ = true;
    return Result<void>::success();
}

AppSettings SettingsMenuSession::commit() {
    baseline_ = draft_;
    dirty_ = false;
    return baseline_;
}

void SettingsMenuSession::discard() {
    draft_ = baseline_;
    dirty_ = false;
}

}
