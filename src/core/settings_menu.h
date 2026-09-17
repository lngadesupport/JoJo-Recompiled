#pragma once
#include "core/settings.h"
#include <cstddef>
#include <string>

namespace jojo {

enum class SettingsPage {
    graphics,
    audio,
    controls
};

class SettingsMenuSession {
public:
    explicit SettingsMenuSession(AppSettings baseline);

    [[nodiscard]] const AppSettings& draft() const noexcept { return draft_; }
    [[nodiscard]] SettingsPage page() const noexcept { return page_; }
    [[nodiscard]] std::size_t selected_player() const noexcept { return selected_player_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

    void set_page(SettingsPage page) noexcept { page_ = page; }
    [[nodiscard]] Result<void> select_player(std::size_t player);
    [[nodiscard]] Result<void> set_graphics(const GraphicsSettings& graphics);
    [[nodiscard]] Result<void> set_audio(const AudioSettings& audio);
    [[nodiscard]] Result<void> select_device(
        std::size_t player,
        std::string device_id,
        const InputDeviceRegistry& registry);
    [[nodiscard]] Result<void> bind_action(
        std::size_t player,
        GameAction action,
        InputBinding binding);

    [[nodiscard]] AppSettings commit();
    void discard();

private:
    [[nodiscard]] static Result<void> validate_player(std::size_t player);

    AppSettings baseline_;
    AppSettings draft_;
    SettingsPage page_{SettingsPage::graphics};
    std::size_t selected_player_{0};
    bool dirty_{false};
};

}
