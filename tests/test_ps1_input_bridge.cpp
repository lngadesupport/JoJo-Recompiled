#include "core/input.h"
#include "core/ps1_input_bridge.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #x "\n"; ++failures; } } while (0)

int main() {
    jojo::ResolvedPlayerInput player{};
    for (const auto action : jojo::all_game_actions()) {
        player.actions[action] = false;
    }

    CHECK(jojo::ps1_digital_pad_buttons(player) == 0xFFFFu);

    player.actions[jojo::GameAction::up] = true;
    player.actions[jojo::GameAction::right] = true;
    player.actions[jojo::GameAction::attack_light] = true;
    player.actions[jojo::GameAction::attack_medium] = true;
    player.actions[jojo::GameAction::attack_heavy] = true;
    player.actions[jojo::GameAction::stand] = true;
    player.actions[jojo::GameAction::start] = true;
    player.actions[jojo::GameAction::coin] = true;

    const auto buttons = jojo::ps1_digital_pad_buttons(player);
    CHECK((buttons & (1u << 4u)) == 0u);   // Up
    CHECK((buttons & (1u << 5u)) == 0u);   // Right
    CHECK((buttons & (1u << 14u)) == 0u);  // Cross = Light
    CHECK((buttons & (1u << 15u)) == 0u);  // Square = Medium
    CHECK((buttons & (1u << 13u)) == 0u);  // Circle = Heavy
    CHECK((buttons & (1u << 12u)) == 0u);  // Triangle = Stand
    CHECK((buttons & (1u << 3u)) == 0u);   // Start
    CHECK((buttons & (1u << 0u)) == 0u);   // Select / taunt
    CHECK((buttons & (1u << 6u)) != 0u);   // Down not pressed
    CHECK((buttons & (1u << 7u)) != 0u);   // Left not pressed

    player.actions[jojo::GameAction::start] = false;
    player.actions[jojo::GameAction::pause] = true;
    CHECK((jojo::ps1_digital_pad_buttons(player) & (1u << 3u)) == 0u);

    jojo::ResolvedInputFrame frame{};
    frame[0] = player;
    frame[1].actions[jojo::GameAction::down] = true;
    const auto pads = jojo::ps1_digital_pad_frame(frame);
    CHECK(pads[0] == jojo::ps1_digital_pad_buttons(player));
    CHECK((pads[1] & (1u << 6u)) == 0u);

    return failures ? 1 : 0;
}
