#pragma once
#include "core/result.h"
#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

inline constexpr std::size_t input_player_count = 2;

enum class GameAction {
    up,
    down,
    left,
    right,
    attack_light,
    attack_medium,
    attack_heavy,
    stand,
    start,
    coin,
    pause
};

enum class BindingKind {
    keyboard_key,
    gamepad_button,
    gamepad_axis
};

enum class DeviceKind {
    keyboard,
    xinput,
    hid
};

struct InputBinding {
    std::string device_id;
    BindingKind kind{BindingKind::keyboard_key};
    std::string code;
    friend bool operator==(const InputBinding&, const InputBinding&) = default;
};

struct PlayerInputSettings {
    std::string selected_device{"keyboard:default"};
    std::map<GameAction, InputBinding> bindings;
    friend bool operator==(const PlayerInputSettings&, const PlayerInputSettings&) = default;
};

struct InputSettings {
    std::array<PlayerInputSettings, input_player_count> players{};

    // Source-compatible aliases for the original single-player development API.
    // Both aliases always refer to player 1 and keep older callers working while
    // the runtime uses the explicit players[] model.
    std::string& selected_device;
    std::map<GameAction, InputBinding>& bindings;

    InputSettings();
    InputSettings(const InputSettings& other);
    InputSettings(InputSettings&& other) noexcept;
    InputSettings& operator=(const InputSettings& other);
    InputSettings& operator=(InputSettings&& other) noexcept;
};

struct InputDeviceInfo {
    std::string id;
    std::string name;
    DeviceKind kind{DeviceKind::keyboard};
    friend bool operator==(const InputDeviceInfo&, const InputDeviceInfo&) = default;
};

struct InputDeviceState {
    std::string device_id;
    DeviceKind kind{DeviceKind::keyboard};
    std::set<std::string> pressed;
    std::map<std::string, float> axes;
};

struct InputFrame {
    std::vector<InputDeviceState> devices;
};

struct ResolvedPlayerInput {
    std::map<GameAction, bool> actions;
    [[nodiscard]] bool pressed(GameAction action) const noexcept;
};

using ResolvedInputFrame = std::array<ResolvedPlayerInput, input_player_count>;

enum class DeviceChangeKind {
    connected,
    disconnected
};

struct InputDeviceChange {
    DeviceChangeKind kind{DeviceChangeKind::connected};
    InputDeviceInfo device;
};

class InputDeviceRegistry {
public:
    [[nodiscard]] const std::vector<InputDeviceInfo>& devices() const noexcept { return devices_; }
    [[nodiscard]] bool contains(std::string_view device_id) const noexcept;
    [[nodiscard]] std::vector<InputDeviceChange> refresh(std::vector<InputDeviceInfo> devices);

private:
    std::vector<InputDeviceInfo> devices_;
};

[[nodiscard]] const std::vector<GameAction>& all_game_actions();
[[nodiscard]] std::string to_string(GameAction action);
[[nodiscard]] Result<GameAction> game_action_from_string(std::string_view value);
[[nodiscard]] std::string to_string(BindingKind kind);
[[nodiscard]] Result<BindingKind> binding_kind_from_string(std::string_view value);
[[nodiscard]] std::string to_string(DeviceKind kind);
[[nodiscard]] std::string serialize_binding(const InputBinding& binding);
[[nodiscard]] Result<InputBinding> parse_binding(std::string_view value);
[[nodiscard]] std::map<GameAction, InputBinding> default_bindings(std::size_t player_index = 0);
[[nodiscard]] ResolvedInputFrame resolve_player_actions(
    const InputSettings& settings,
    const InputFrame& frame,
    float axis_threshold = 0.5f);
[[nodiscard]] Result<InputBinding> capture_binding(
    std::string_view device_id,
    const InputFrame& previous,
    const InputFrame& current,
    float axis_threshold = 0.5f);

}
