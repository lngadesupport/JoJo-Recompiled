#include "core/ps1_rollback_runtime.h"

#include <cstdint>
#include <iostream>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #expr "\n"; ++failures; } } while (0)

static void test_ps1_button_encoding_keeps_prediction_neutral() {
    const auto neutral =
        jojo::ps1_rollback_input_from_active_low(0xFFFFu);
    CHECK(neutral.buttons == 0u);
    CHECK(jojo::ps1_active_low_from_rollback_input(neutral) == 0xFFFFu);

    const auto one_pressed =
        jojo::ps1_rollback_input_from_active_low(0x7FFFu);
    CHECK(one_pressed.buttons == 0x8000u);
    CHECK(jojo::ps1_active_low_from_rollback_input(one_pressed) == 0x7FFFu);

    const auto mixed =
        jojo::ps1_rollback_input_from_active_low(0xA55Au);
    CHECK(jojo::ps1_active_low_from_rollback_input(mixed) == 0xA55Au);
}

int main() {
    test_ps1_button_encoding_keeps_prediction_neutral();
    if (failures) {
        std::cerr << failures << " PS1 rollback runtime assertion(s) failed\n";
        return 1;
    }
    std::cout << "PS1 rollback runtime assertions passed\n";
    return 0;
}
