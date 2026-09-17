#include "core/input.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace jojo {
namespace {

const InputDeviceState* find_device(const InputFrame& frame, std::string_view id) noexcept {
    const auto it = std::find_if(frame.devices.begin(), frame.devices.end(),
                                 [&](const InputDeviceState& device) { return device.device_id == id; });
    return it == frame.devices.end() ? nullptr : &*it;
}

bool binding_active(const InputBinding& binding, const InputDeviceState& device, float axis_threshold) noexcept {
    switch (binding.kind) {
        case BindingKind::keyboard_key:
            if (device.kind != DeviceKind::keyboard) return false;
            return device.pressed.contains(binding.code);
        case BindingKind::gamepad_button:
            if (device.kind == DeviceKind::keyboard) return false;
            return device.pressed.contains(binding.code);
        case BindingKind::gamepad_axis: {
            if (device.kind == DeviceKind::keyboard || binding.code.size() < 2) return false;
            const char direction = binding.code.back();
            if (direction != '+' && direction != '-') return false;
            const auto axis = device.axes.find(binding.code.substr(0, binding.code.size() - 1));
            if (axis == device.axes.end()) return false;
            return direction == '+' ? axis->second >= axis_threshold : axis->second <= -axis_threshold;
        }
    }
    return false;
}

}

const std::vector<GameAction>& all_game_actions() {
    static const std::vector<GameAction> actions{
        GameAction::up, GameAction::down, GameAction::left, GameAction::right,
        GameAction::attack_light, GameAction::attack_medium, GameAction::attack_heavy,
        GameAction::stand, GameAction::start, GameAction::coin, GameAction::pause
    };
    return actions;
}

std::string to_string(GameAction action) {
    switch (action) {
        case GameAction::up: return "up";
        case GameAction::down: return "down";
        case GameAction::left: return "left";
        case GameAction::right: return "right";
        case GameAction::attack_light: return "attack_light";
        case GameAction::attack_medium: return "attack_medium";
        case GameAction::attack_heavy: return "attack_heavy";
        case GameAction::stand: return "stand";
        case GameAction::start: return "start";
        case GameAction::coin: return "coin";
        case GameAction::pause: return "pause";
    }
    return {};
}

Result<GameAction> game_action_from_string(std::string_view value) {
    for (const auto action : all_game_actions()) {
        if (to_string(action) == value) return Result<GameAction>::success(action);
    }
    return Result<GameAction>::failure(ErrorCode::invalid_settings, "unknown game action: " + std::string(value));
}

std::string to_string(BindingKind kind) {
    switch (kind) {
        case BindingKind::keyboard_key: return "key";
        case BindingKind::gamepad_button: return "button";
        case BindingKind::gamepad_axis: return "axis";
    }
    return {};
}

Result<BindingKind> binding_kind_from_string(std::string_view value) {
    if (value == "key") return Result<BindingKind>::success(BindingKind::keyboard_key);
    if (value == "button") return Result<BindingKind>::success(BindingKind::gamepad_button);
    if (value == "axis") return Result<BindingKind>::success(BindingKind::gamepad_axis);
    return Result<BindingKind>::failure(ErrorCode::invalid_settings, "unknown binding kind: " + std::string(value));
}

std::string to_string(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::keyboard: return "keyboard";
        case DeviceKind::xinput: return "xinput";
        case DeviceKind::hid: return "hid";
    }
    return {};
}

std::string serialize_binding(const InputBinding& binding) {
    return binding.device_id + "|" + to_string(binding.kind) + "|" + binding.code;
}

Result<InputBinding> parse_binding(std::string_view value) {
    const auto first = value.find('|');
    if (first == std::string_view::npos) {
        return Result<InputBinding>::failure(ErrorCode::invalid_settings, "binding is missing kind separator");
    }
    const auto second = value.find('|', first + 1);
    if (second == std::string_view::npos) {
        return Result<InputBinding>::failure(ErrorCode::invalid_settings, "binding is missing code separator");
    }
    const auto device = value.substr(0, first);
    const auto kind_text = value.substr(first + 1, second - first - 1);
    const auto code = value.substr(second + 1);
    if (device.empty() || code.empty()) {
        return Result<InputBinding>::failure(ErrorCode::invalid_settings, "binding device/code cannot be empty");
    }
    auto kind = binding_kind_from_string(kind_text);
    if (!kind) return Result<InputBinding>::failure(kind.error, kind.detail);
    return Result<InputBinding>::success(InputBinding{std::string(device), kind.value, std::string(code)});
}

std::map<GameAction, InputBinding> default_bindings(std::size_t player_index) {
    const std::string keyboard = "keyboard:default";
    if (player_index == 0) {
        return {
            {GameAction::up, {keyboard, BindingKind::keyboard_key, "ArrowUp"}},
            {GameAction::down, {keyboard, BindingKind::keyboard_key, "ArrowDown"}},
            {GameAction::left, {keyboard, BindingKind::keyboard_key, "ArrowLeft"}},
            {GameAction::right, {keyboard, BindingKind::keyboard_key, "ArrowRight"}},
            {GameAction::attack_light, {keyboard, BindingKind::keyboard_key, "Z"}},
            {GameAction::attack_medium, {keyboard, BindingKind::keyboard_key, "X"}},
            {GameAction::attack_heavy, {keyboard, BindingKind::keyboard_key, "C"}},
            {GameAction::stand, {keyboard, BindingKind::keyboard_key, "A"}},
            {GameAction::start, {keyboard, BindingKind::keyboard_key, "Enter"}},
            {GameAction::coin, {keyboard, BindingKind::keyboard_key, "RightShift"}},
            {GameAction::pause, {keyboard, BindingKind::keyboard_key, "Escape"}},
        };
    }
    return {
        {GameAction::up, {keyboard, BindingKind::keyboard_key, "W"}},
        {GameAction::down, {keyboard, BindingKind::keyboard_key, "S"}},
        {GameAction::left, {keyboard, BindingKind::keyboard_key, "A"}},
        {GameAction::right, {keyboard, BindingKind::keyboard_key, "D"}},
        {GameAction::attack_light, {keyboard, BindingKind::keyboard_key, "F"}},
        {GameAction::attack_medium, {keyboard, BindingKind::keyboard_key, "G"}},
        {GameAction::attack_heavy, {keyboard, BindingKind::keyboard_key, "H"}},
        {GameAction::stand, {keyboard, BindingKind::keyboard_key, "R"}},
        {GameAction::start, {keyboard, BindingKind::keyboard_key, "Enter"}},
        {GameAction::coin, {keyboard, BindingKind::keyboard_key, "Tab"}},
        {GameAction::pause, {keyboard, BindingKind::keyboard_key, "Escape"}},
    };
}

InputSettings::InputSettings()
    : selected_device(players[0].selected_device), bindings(players[0].bindings) {
    for (std::size_t player = 0; player < players.size(); ++player) {
        players[player].selected_device = "keyboard:default";
        players[player].bindings = default_bindings(player);
    }
}

InputSettings::InputSettings(const InputSettings& other)
    : players(other.players), selected_device(players[0].selected_device), bindings(players[0].bindings) {}

InputSettings::InputSettings(InputSettings&& other) noexcept
    : players(std::move(other.players)), selected_device(players[0].selected_device), bindings(players[0].bindings) {}

InputSettings& InputSettings::operator=(const InputSettings& other) {
    if (this != &other) players = other.players;
    return *this;
}

InputSettings& InputSettings::operator=(InputSettings&& other) noexcept {
    if (this != &other) players = std::move(other.players);
    return *this;
}

bool ResolvedPlayerInput::pressed(GameAction action) const noexcept {
    const auto it = actions.find(action);
    return it != actions.end() && it->second;
}

ResolvedInputFrame resolve_player_actions(const InputSettings& settings,
                                          const InputFrame& frame,
                                          float axis_threshold) {
    ResolvedInputFrame result{};
    const float threshold = std::clamp(std::abs(axis_threshold), 0.01f, 1.0f);
    for (std::size_t player = 0; player < settings.players.size(); ++player) {
        for (const auto action : all_game_actions()) result[player].actions[action] = false;
        for (const auto& [action, binding] : settings.players[player].bindings) {
            const auto* device = find_device(frame, binding.device_id);
            if (device) result[player].actions[action] = binding_active(binding, *device, threshold);
        }
    }
    return result;
}

Result<InputBinding> capture_binding(std::string_view device_id,
                                     const InputFrame& previous,
                                     const InputFrame& current,
                                     float axis_threshold) {
    const auto* now = find_device(current, device_id);
    if (!now) {
        return Result<InputBinding>::failure(ErrorCode::invalid_argument,
                                             "capture device is not present: " + std::string(device_id));
    }
    const auto* before = find_device(previous, device_id);
    for (const auto& code : now->pressed) {
        if (!before || !before->pressed.contains(code)) {
            const auto kind = now->kind == DeviceKind::keyboard
                ? BindingKind::keyboard_key : BindingKind::gamepad_button;
            return Result<InputBinding>::success({std::string(device_id), kind, code});
        }
    }

    if (now->kind != DeviceKind::keyboard) {
        const float threshold = std::clamp(std::abs(axis_threshold), 0.01f, 1.0f);
        for (const auto& [axis, value] : now->axes) {
            float old_value = 0.0f;
            if (before) {
                const auto old = before->axes.find(axis);
                if (old != before->axes.end()) old_value = old->second;
            }
            if (value >= threshold && old_value < threshold) {
                return Result<InputBinding>::success(
                    {std::string(device_id), BindingKind::gamepad_axis, axis + "+"});
            }
            if (value <= -threshold && old_value > -threshold) {
                return Result<InputBinding>::success(
                    {std::string(device_id), BindingKind::gamepad_axis, axis + "-"});
            }
        }
    }
    return Result<InputBinding>::failure(ErrorCode::invalid_argument, "no newly activated control to capture");
}

bool InputDeviceRegistry::contains(std::string_view device_id) const noexcept {
    return std::any_of(devices_.begin(), devices_.end(),
                       [&](const InputDeviceInfo& device) { return device.id == device_id; });
}

std::vector<InputDeviceChange> InputDeviceRegistry::refresh(std::vector<InputDeviceInfo> devices) {
    devices.erase(std::remove_if(devices.begin(), devices.end(),
                                 [](const InputDeviceInfo& device) { return device.id.empty(); }),
                  devices.end());
    std::stable_sort(devices.begin(), devices.end(),
                     [](const InputDeviceInfo& a, const InputDeviceInfo& b) { return a.id < b.id; });
    devices.erase(std::unique(devices.begin(), devices.end(),
                              [](const InputDeviceInfo& a, const InputDeviceInfo& b) { return a.id == b.id; }),
                  devices.end());

    std::vector<InputDeviceChange> changes;
    for (const auto& old_device : devices_) {
        const bool still_present = std::any_of(devices.begin(), devices.end(),
            [&](const InputDeviceInfo& device) { return device.id == old_device.id; });
        if (!still_present) changes.push_back({DeviceChangeKind::disconnected, old_device});
    }
    for (const auto& new_device : devices) {
        const bool already_present = std::any_of(devices_.begin(), devices_.end(),
            [&](const InputDeviceInfo& device) { return device.id == new_device.id; });
        if (!already_present) changes.push_back({DeviceChangeKind::connected, new_device});
    }
    std::sort(changes.begin(), changes.end(), [](const InputDeviceChange& a, const InputDeviceChange& b) {
        if (a.device.id != b.device.id) return a.device.id < b.device.id;
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    devices_ = std::move(devices);
    return changes;
}

}
