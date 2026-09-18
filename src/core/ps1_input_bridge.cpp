#include "core/ps1_input_bridge.h"

namespace jojo {
namespace {

void set_pressed(std::uint16_t& buttons, unsigned bit, bool pressed) noexcept {
    if (pressed) {
        buttons = static_cast<std::uint16_t>(
            buttons & static_cast<std::uint16_t>(~(1u << bit)));
    }
}

} // namespace

std::uint16_t ps1_digital_pad_buttons(
    const ResolvedPlayerInput& input) noexcept {
    std::uint16_t buttons = 0xFFFFu;

    set_pressed(buttons, 0u, input.pressed(GameAction::coin));
    set_pressed(
        buttons,
        3u,
        input.pressed(GameAction::start) ||
            input.pressed(GameAction::pause));
    set_pressed(buttons, 4u, input.pressed(GameAction::up));
    set_pressed(buttons, 5u, input.pressed(GameAction::right));
    set_pressed(buttons, 6u, input.pressed(GameAction::down));
    set_pressed(buttons, 7u, input.pressed(GameAction::left));

    // JoJo PS1 default layout:
    // Triangle=Stand, Circle=Heavy, Cross=Light, Square=Medium.
    set_pressed(buttons, 12u, input.pressed(GameAction::stand));
    set_pressed(buttons, 13u, input.pressed(GameAction::attack_heavy));
    set_pressed(buttons, 14u, input.pressed(GameAction::attack_light));
    set_pressed(buttons, 15u, input.pressed(GameAction::attack_medium));

    return buttons;
}

std::array<std::uint16_t, input_player_count>
ps1_digital_pad_frame(const ResolvedInputFrame& input) noexcept {
    std::array<std::uint16_t, input_player_count> result{};
    for (std::size_t player = 0u; player < result.size(); ++player) {
        result[player] = ps1_digital_pad_buttons(input[player]);
    }
    return result;
}

} // namespace jojo
