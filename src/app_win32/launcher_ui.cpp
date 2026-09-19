#ifdef _WIN32
#define NOMINMAX
#include "app_win32/launcher_ui.h"
#include "core/version.h"

#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace jojo::win32 {
namespace {

constexpr float kUiWidth = 1024.0f;
constexpr float kUiHeight = 720.0f;
const Gdiplus::Color kGold(255, 242, 195, 72);
const Gdiplus::Color kText(255, 246, 249, 252);
const Gdiplus::Color kMuted(255, 166, 184, 198);
const Gdiplus::Color kDisabled(255, 92, 109, 122);
const Gdiplus::Color kPanel(224, 5, 16, 25);
const Gdiplus::Color kPanelHighContrast(248, 0, 0, 0);
const Gdiplus::Color kJojoBlue(255, 42, 176, 228);
const Gdiplus::Color kJojoBlueDark(235, 9, 83, 126);
const Gdiplus::Color kJojoIce(255, 201, 235, 247);

bool contains(const Gdiplus::RectF& rect, float x, float y) noexcept {
    return x >= rect.X && y >= rect.Y &&
           x <= rect.X + rect.Width && y <= rect.Y + rect.Height;
}

std::wstring widen_ascii(std::string_view text) {
    return std::wstring(text.begin(), text.end());
}

std::wstring display_mode_name(DisplayMode mode) {
    switch (mode) {
        case DisplayMode::windowed: return L"WINDOWED";
        case DisplayMode::fullscreen: return L"FULLSCREEN";
        case DisplayMode::borderless: return L"BORDERLESS";
    }
    return L"UNKNOWN";
}

std::wstring aspect_name(AspectRatio aspect) {
    return widen_ascii(to_string(aspect));
}

std::wstring msaa_name(Msaa value) {
    const auto amount = static_cast<int>(value);
    return amount == 0 ? L"OFF" : std::to_wstring(amount) + L"x";
}

std::wstring filter_name(TextureFilter value) {
    const auto amount = static_cast<int>(value);
    return amount == 0 ? L"OFF / POINT" : std::to_wstring(amount) + L"x";
}

std::wstring on_off(bool value) {
    return value ? L"ON" : L"OFF";
}

std::wstring action_name(GameAction action) {
    switch (action) {
        case GameAction::up: return L"UP";
        case GameAction::down: return L"DOWN";
        case GameAction::left: return L"LEFT";
        case GameAction::right: return L"RIGHT";
        case GameAction::attack_light: return L"LIGHT ATTACK";
        case GameAction::attack_medium: return L"MEDIUM ATTACK";
        case GameAction::attack_heavy: return L"HEAVY ATTACK";
        case GameAction::stand: return L"STAND";
        case GameAction::start: return L"START";
        case GameAction::coin: return L"COIN";
        case GameAction::pause: return L"PAUSE";
    }
    return L"ACTION";
}

std::wstring device_name(
    std::string_view device_id,
    const InputDeviceRegistry& registry) {
    for (const auto& device : registry.devices()) {
        if (device.id == device_id) return widen_ascii(device.name);
    }
    return widen_ascii(device_id);
}

void draw_string(
    Gdiplus::Graphics& graphics,
    const std::wstring& text,
    const Gdiplus::RectF& rect,
    float size,
    const Gdiplus::Color& color,
    int style = Gdiplus::FontStyleRegular,
    Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear) {
    Gdiplus::Font font(L"Segoe UI", size, style, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    graphics.DrawString(
        text.c_str(),
        static_cast<INT>(text.size()),
        &font,
        rect,
        &format,
        &brush);
}

void draw_jojo_shell(Gdiplus::Graphics& graphics) {
    Gdiplus::LinearGradientBrush background(
        Gdiplus::PointF(0.0f, 0.0f),
        Gdiplus::PointF(kUiWidth, kUiHeight),
        Gdiplus::Color(255, 4, 15, 24),
        Gdiplus::Color(255, 28, 52, 63));
    graphics.FillRectangle(&background, 0.0f, 0.0f, kUiWidth, kUiHeight);

    const Gdiplus::PointF left_band[] = {
        {0.0f, 0.0f}, {430.0f, 0.0f}, {220.0f, kUiHeight}, {0.0f, kUiHeight}};
    Gdiplus::SolidBrush band_a(Gdiplus::Color(118, 25, 67, 89));
    graphics.FillPolygon(&band_a, left_band, 4);

    const Gdiplus::PointF center_band[] = {
        {320.0f, 0.0f}, {690.0f, 0.0f}, {500.0f, kUiHeight}, {140.0f, kUiHeight}};
    Gdiplus::SolidBrush band_b(Gdiplus::Color(74, 171, 206, 220));
    graphics.FillPolygon(&band_b, center_band, 4);

    const Gdiplus::PointF white_band[] = {
        {575.0f, 0.0f}, {655.0f, 0.0f}, {440.0f, kUiHeight}, {360.0f, kUiHeight}};
    Gdiplus::SolidBrush band_c(Gdiplus::Color(34, 245, 249, 250));
    graphics.FillPolygon(&band_c, white_band, 4);

    Gdiplus::Pen line(Gdiplus::Color(80, 170, 225, 244), 1.0f);
    for (int i = -220; i < 1200; i += 64) {
        graphics.DrawLine(
            &line,
            static_cast<float>(i),
            0.0f,
            static_cast<float>(i - 220),
            kUiHeight);
    }

    Gdiplus::SolidBrush top_bar(Gdiplus::Color(196, 3, 12, 19));
    graphics.FillRectangle(&top_bar, 0.0f, 0.0f, kUiWidth, 70.0f);
    Gdiplus::SolidBrush cyan_bar(kJojoBlue);
    graphics.FillRectangle(&cyan_bar, 0.0f, 68.0f, kUiWidth, 3.0f);

    draw_string(
        graphics,
        L"JOJO'S BIZARRE ADVENTURE",
        Gdiplus::RectF(54.0f, 14.0f, 470.0f, 38.0f),
        28.0f,
        kText,
        Gdiplus::FontStyleBold);
    draw_string(
        graphics,
        L"RECOMPILED",
        Gdiplus::RectF(528.0f, 16.0f, 210.0f, 34.0f),
        20.0f,
        kJojoBlue,
        Gdiplus::FontStyleBold);
}

std::size_t wrap_index(std::size_t current, int direction, std::size_t count) {
    if (count == 0) return 0;
    if (direction > 0) return (current + 1) % count;
    return current == 0 ? count - 1 : current - 1;
}

template <typename T, std::size_t N>
T cycle_value(T current, int direction, const std::array<T, N>& values) {
    const auto it = std::find(values.begin(), values.end(), current);
    const auto index = it == values.end()
        ? std::size_t{0}
        : static_cast<std::size_t>(std::distance(values.begin(), it));
    return values[wrap_index(index, direction, values.size())];
}

LauncherUiAction map_online_action(OnlineUiAction action) noexcept {
    switch (action) {
        case OnlineUiAction::none:
            return LauncherUiAction::none;
        case OnlineUiAction::back_to_launcher:
            return LauncherUiAction::none;
        case OnlineUiAction::refresh_public_rooms:
            return LauncherUiAction::online_refresh_rooms;
        case OnlineUiAction::host_room:
            return LauncherUiAction::online_host_room;
        case OnlineUiAction::connect_selected_room:
            return LauncherUiAction::online_connect_room;
        case OnlineUiAction::connect_direct:
            return LauncherUiAction::online_connect_direct;
        case OnlineUiAction::begin_matchmaking:
            return LauncherUiAction::online_begin_matchmaking;
        case OnlineUiAction::cancel_matchmaking:
            return LauncherUiAction::online_cancel_matchmaking;
        case OnlineUiAction::leave_lobby:
            return LauncherUiAction::online_leave_lobby;
        case OnlineUiAction::ready_changed:
            return LauncherUiAction::online_ready_changed;
        case OnlineUiAction::send_chat_message:
            return LauncherUiAction::online_send_chat;
        case OnlineUiAction::start_lobby_game:
            return LauncherUiAction::online_start_lobby_game;
    }
    return LauncherUiAction::none;
}

} // namespace

LauncherUi::~LauncherUi() {
    shutdown();
}

bool LauncherUi::initialize(const std::filesystem::path& background_path) {
    shutdown();

    Gdiplus::GdiplusStartupInput startup_input;
    if (Gdiplus::GdiplusStartup(
            &gdiplus_token_,
            &startup_input,
            nullptr) != Gdiplus::Ok) {
        gdiplus_token_ = 0;
        return false;
    }

    background_.reset(Gdiplus::Image::FromFile(background_path.c_str(), FALSE));
    if (!background_ || background_->GetLastStatus() != Gdiplus::Ok) {
        background_.reset();
    }
    return true;
}

void LauncherUi::shutdown() noexcept {
    background_.reset();
    if (gdiplus_token_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplus_token_);
        gdiplus_token_ = 0;
    }
}

void LauncherUi::show_main() noexcept {
    screen_ = Screen::main_menu;
    main_selection_ = std::min<std::size_t>(main_selection_, 4);
}

void LauncherUi::open_controls() noexcept {
    screen_ = Screen::settings;
    page_ = SettingsPage::controls;
    selected_row_ = 0;
}

void LauncherUi::open_settings() noexcept {
    screen_ = Screen::settings;
    page_ = SettingsPage::graphics;
    selected_row_ = 0;
}

bool LauncherUi::settings_open() const noexcept {
    return screen_ == Screen::settings;
}

bool LauncherUi::online_open() const noexcept {
    return screen_ == Screen::online;
}

std::size_t LauncherUi::selected_control_player() const noexcept {
    return control_player_;
}

GameAction LauncherUi::selected_control_action() const noexcept {
    const auto& actions = all_game_actions();
    if (selected_row_ == 0 || selected_row_ - 1 >= actions.size()) {
        return GameAction::attack_light;
    }
    return actions[selected_row_ - 1];
}

std::size_t LauncherUi::row_count() const noexcept {
    switch (page_) {
        case SettingsPage::graphics: return 7;
        case SettingsPage::audio: return 2;
        case SettingsPage::controls: return 1 + all_game_actions().size();
        case SettingsPage::accessibility: return 2;
    }
    return 0;
}

void LauncherUi::clamp_selected_row() noexcept {
    const auto count = row_count();
    if (count == 0) selected_row_ = 0;
    else if (selected_row_ >= count) selected_row_ = count - 1;
}

void LauncherUi::cycle_page(int direction) noexcept {
    constexpr std::array<SettingsPage, 4> pages{
        SettingsPage::graphics,
        SettingsPage::audio,
        SettingsPage::controls,
        SettingsPage::accessibility,
    };
    page_ = cycle_value(page_, direction, pages);
    selected_row_ = 0;
}

void LauncherUi::cycle_control_player(int direction) noexcept {
    control_player_ = wrap_index(
        control_player_,
        direction,
        input_player_count);
}

LauncherUiAction LauncherUi::activate_main_item() noexcept {
    switch (main_selection_) {
        case 0:
            return LauncherUiAction::start_game;
        case 1:
            return LauncherUiAction::select_disc;
        case 2:
            open_controls();
            return LauncherUiAction::none;
        case 3:
            open_settings();
            return LauncherUiAction::none;
        case 4:
            return LauncherUiAction::exit_app;
        default:
            return LauncherUiAction::none;
    }
}

LauncherUiAction LauncherUi::adjust_setting(
    int direction,
    AppSettings& settings,
    const InputDeviceRegistry& devices) {
    if (direction == 0) direction = 1;

    if (page_ == SettingsPage::graphics) {
        switch (selected_row_) {
            case 0: {
                constexpr std::array modes{
                    DisplayMode::windowed,
                    DisplayMode::fullscreen,
                    DisplayMode::borderless,
                };
                settings.graphics.display_mode =
                    cycle_value(settings.graphics.display_mode, direction, modes);
                break;
            }
            case 1: {
                constexpr std::array<std::pair<int, int>, 4> resolutions{{
                    {1280, 720},
                    {1920, 1080},
                    {2560, 1440},
                    {3840, 2160},
                }};
                std::size_t current = 0;
                for (std::size_t i = 0; i < resolutions.size(); ++i) {
                    if (resolutions[i].first == settings.graphics.width &&
                        resolutions[i].second == settings.graphics.height) {
                        current = i;
                        break;
                    }
                }
                const auto next = resolutions[
                    wrap_index(current, direction, resolutions.size())];
                settings.graphics.width = next.first;
                settings.graphics.height = next.second;
                break;
            }
            case 2:
                settings.graphics.vsync = !settings.graphics.vsync;
                break;
            case 3: {
                constexpr std::array<int, 7> limits{
                    60, 120, 144, 165, 240, 360, 0,
                };
                std::size_t current=0u;
                for(std::size_t i=0u;i<limits.size();++i){
                    if(limits[i]==settings.graphics.frame_limit){
                        current=i;
                        break;
                    }
                }
                settings.graphics.frame_limit=
                    limits[wrap_index(current,direction,limits.size())];
                break;
            }
            case 4: {
                constexpr std::array modes{
                    Msaa::off, Msaa::x2, Msaa::x4,
                    Msaa::x8, Msaa::x16,
                };
                settings.graphics.msaa =
                    cycle_value(settings.graphics.msaa, direction, modes);
                break;
            }
            case 5: {
                constexpr std::array modes{
                    TextureFilter::off, TextureFilter::x2,
                    TextureFilter::x4, TextureFilter::x8,
                    TextureFilter::x16,
                };
                settings.graphics.texture_filter =
                    cycle_value(settings.graphics.texture_filter, direction, modes);
                break;
            }
            case 6: {
                constexpr std::array modes{
                    AspectRatio::ratio_4_3,
                    AspectRatio::ratio_16_9,
                    AspectRatio::ratio_16_10,
                    AspectRatio::ratio_21_9,
                    AspectRatio::ratio_32_9,
                };
                settings.graphics.aspect_ratio =
                    cycle_value(settings.graphics.aspect_ratio, direction, modes);
                break;
            }
            default:
                return LauncherUiAction::none;
        }
        return validate_graphics(settings.graphics)
            ? LauncherUiAction::settings_changed
            : LauncherUiAction::none;
    }

    if (page_ == SettingsPage::audio) {
        auto adjust_volume = [direction](int value) {
            const int delta = direction > 0 ? 5 : -5;
            return std::clamp(value + delta, 0, 100);
        };
        switch (selected_row_) {
            case 0:
                settings.audio.master_volume =
                    adjust_volume(settings.audio.master_volume);
                break;
            case 1:
                settings.audio.mute_when_unfocused =
                    !settings.audio.mute_when_unfocused;
                break;
            default:
                return LauncherUiAction::none;
        }
        return LauncherUiAction::settings_changed;
    }

    if (page_ == SettingsPage::controls) {
        if (selected_row_ != 0 || devices.devices().empty()) {
            return LauncherUiAction::none;
        }

        const auto& current =
            settings.input.players[control_player_].selected_device;
        std::size_t index = 0;
        for (std::size_t i = 0; i < devices.devices().size(); ++i) {
            if (devices.devices()[i].id == current) {
                index = i;
                break;
            }
        }
        index = wrap_index(index, direction, devices.devices().size());
        settings.input.players[control_player_].selected_device =
            devices.devices()[index].id;
        return LauncherUiAction::settings_changed;
    }

    if (page_ == SettingsPage::accessibility) {
        switch (selected_row_) {
            case 0:
                settings.accessibility.high_contrast_ui =
                    !settings.accessibility.high_contrast_ui;
                return LauncherUiAction::settings_changed;
            case 1: {
                constexpr std::array<int, 4> scales{100, 110, 125, 150};
                std::size_t index = 0;
                for (std::size_t i = 0; i < scales.size(); ++i) {
                    if (scales[i] == settings.accessibility.menu_text_scale) {
                        index = i;
                        break;
                    }
                }
                settings.accessibility.menu_text_scale =
                    scales[wrap_index(index, direction, scales.size())];
                return LauncherUiAction::settings_changed;
            }
            default:
                // These options are intentionally visible but not interactive
                // until the native gameplay hooks exist.
                return LauncherUiAction::none;
        }
    }

    return LauncherUiAction::none;
}

void LauncherUi::paint(
    HDC dc,
    const RECT& client,
    const AppSettings& settings,
    const InputDeviceRegistry& devices,
    std::wstring_view source_label,
    std::wstring_view capture_status) {
    if (!dc) return;
    const auto client_width = client.right - client.left;
    const auto client_height = client.bottom - client.top;
    if (client_width <= 0 || client_height <= 0) return;

    if (screen_ == Screen::online) {
        online_ui_.paint(dc, client, online_model_);
        return;
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const float ui_scale=std::min(
        static_cast<float>(client_width)/kUiWidth,
        static_cast<float>(client_height)/kUiHeight);
    const float ui_offset_x=
        (static_cast<float>(client_width)-kUiWidth*ui_scale)*0.5f;
    const float ui_offset_y=
        (static_cast<float>(client_height)-kUiHeight*ui_scale)*0.5f;
    Gdiplus::Matrix ui_transform(
        ui_scale,0.0f,0.0f,ui_scale,ui_offset_x,ui_offset_y);
    graphics.SetTransform(&ui_transform);

    draw_jojo_shell(graphics);

    if (screen_ == Screen::main_menu) {
        draw_string(
            graphics,
            L"MAIN MENU",
            Gdiplus::RectF(555.0f, 112.0f, 390.0f, 42.0f),
            20.0f,
            kJojoIce,
            Gdiplus::FontStyleBold,
            Gdiplus::StringAlignmentFar);

        constexpr std::array<const wchar_t*, 5> labels{
            L"START GAME",
            L"SELECT GAME",
            L"CONTROLS",
            L"OPTIONS",
            L"EXIT",
        };
        constexpr std::array<const wchar_t*, 5> hints{
            L"START THE VALIDATED GAME",
            L"CHOOSE ISO / BIN / CUE",
            L"INPUT DEVICE & BINDINGS",
            L"VIDEO / AUDIO / ACCESSIBILITY",
            L"CLOSE JOJO RECOMPILED",
        };

        for (std::size_t i = 0; i < labels.size(); ++i) {
            const float y=178.0f+static_cast<float>(i)*78.0f;
            const bool selected=i==main_selection_;

            const Gdiplus::PointF row[]={
                {520.0f,y},
                {960.0f,y},
                {936.0f,y+58.0f},
                {496.0f,y+58.0f}};
            Gdiplus::SolidBrush row_brush(
                selected
                    ?Gdiplus::Color(238,16,143,201)
                    :Gdiplus::Color(190,6,17,27));
            graphics.FillPolygon(&row_brush,row,4);

            if(selected){
                const Gdiplus::PointF accent[]={
                    {484.0f,y+10.0f},
                    {515.0f,y+29.0f},
                    {484.0f,y+48.0f}};
                Gdiplus::SolidBrush accent_brush(kJojoIce);
                graphics.FillPolygon(&accent_brush,accent,3);
                Gdiplus::Pen selected_line(kJojoIce,2.0f);
                graphics.DrawLine(&selected_line,520.0f,y+57.0f,936.0f,y+57.0f);
            }

            draw_string(
                graphics,
                labels[i],
                Gdiplus::RectF(548.0f,y+2.0f,350.0f,34.0f),
                selected?24.0f:22.0f,
                kText,
                Gdiplus::FontStyleBold);
            draw_string(
                graphics,
                hints[i],
                Gdiplus::RectF(550.0f,y+31.0f,350.0f,20.0f),
                11.0f,
                selected?kJojoIce:kMuted,
                Gdiplus::FontStyleRegular);
        }

        Gdiplus::SolidBrush info_panel(Gdiplus::Color(205,2,12,19));
        graphics.FillRectangle(&info_panel,54.0f,518.0f,378.0f,125.0f);
        draw_string(
            graphics,
            L"GAME SOURCE",
            Gdiplus::RectF(72.0f,528.0f,150.0f,24.0f),
            13.0f,
            kJojoBlue,
            Gdiplus::FontStyleBold);
        const std::wstring source_text=
            source_label.empty()?L"NO GAME SELECTED":std::wstring(source_label);
        draw_string(
            graphics,
            source_text,
            Gdiplus::RectF(72.0f,553.0f,340.0f,34.0f),
            16.0f,
            source_label.empty()?kMuted:kText,
            Gdiplus::FontStyleBold);
        draw_string(
            graphics,
            L"ENTER confirm  •  ARROWS navigate  •  ESC back",
            Gdiplus::RectF(72.0f,594.0f,340.0f,30.0f),
            12.0f,
            kMuted);

        draw_string(
            graphics,
            L"v"+widen_ascii(core_version()),
            Gdiplus::RectF(840.0f,665.0f,135.0f,30.0f),
            14.0f,
            kJojoIce,
            Gdiplus::FontStyleBold,
            Gdiplus::StringAlignmentFar);
        return;
    }

    const auto panel_color=settings.accessibility.high_contrast_ui
        ?kPanelHighContrast
        :kPanel;
    Gdiplus::SolidBrush panel(panel_color);
    graphics.FillRectangle(&panel,335.0f,118.0f,635.0f,535.0f);

    draw_string(
        graphics,
        page_==SettingsPage::controls?L"CONTROLS":L"OPTIONS",
        Gdiplus::RectF(54.0f,118.0f,245.0f,52.0f),
        34.0f,
        kText,
        Gdiplus::FontStyleBold);
    draw_string(
        graphics,
        L"GAME SETTINGS",
        Gdiplus::RectF(56.0f,163.0f,220.0f,26.0f),
        13.0f,
        kJojoBlue,
        Gdiplus::FontStyleBold);

    constexpr std::array<const wchar_t*,4> tabs{
        L"VIDEO",L"AUDIO",L"CONTROL",L"ACCESSIBILITY"};
    constexpr std::array<SettingsPage,4> pages{
        SettingsPage::graphics,
        SettingsPage::audio,
        SettingsPage::controls,
        SettingsPage::accessibility};
    for(std::size_t i=0;i<tabs.size();++i){
        const float y=218.0f+static_cast<float>(i)*60.0f;
        const bool active=page_==pages[i];
        if(active){
            const Gdiplus::PointF tab_shape[]={
                {52.0f,y},{310.0f,y},{292.0f,y+44.0f},{52.0f,y+44.0f}};
            Gdiplus::SolidBrush tab_brush(kJojoBlueDark);
            graphics.FillPolygon(&tab_brush,tab_shape,4);
        }
        draw_string(
            graphics,
            tabs[i],
            Gdiplus::RectF(72.0f,y,205.0f,44.0f),
            17.0f,
            active?kText:kMuted,
            Gdiplus::FontStyleBold);
    }

    std::vector<std::pair<std::wstring,std::wstring>> rows;
    std::vector<bool> enabled;
    if(page_==SettingsPage::graphics){
        rows={
            {L"DISPLAY MODE",display_mode_name(settings.graphics.display_mode)},
            {L"RESOLUTION",std::to_wstring(settings.graphics.width)+L" x "+
                std::to_wstring(settings.graphics.height)},
            {L"V-SYNC",on_off(settings.graphics.vsync)},
            {L"FRAME LIMIT",settings.graphics.frame_limit==0
                ?L"UNLIMITED"
                :std::to_wstring(settings.graphics.frame_limit)+L" FPS"},
            {L"ANTI-ALIASING",msaa_name(settings.graphics.msaa)},
            {L"TEXTURE FILTER",filter_name(settings.graphics.texture_filter)},
            {L"ASPECT RATIO",aspect_name(settings.graphics.aspect_ratio)},
        };
        enabled.assign(rows.size(),true);
    }else if(page_==SettingsPage::audio){
        rows={
            {L"MASTER VOLUME",std::to_wstring(settings.audio.master_volume)+L"%"},
            {L"MUTE WHEN UNFOCUSED",on_off(settings.audio.mute_when_unfocused)},
        };
        enabled.assign(rows.size(),true);
    }else if(page_==SettingsPage::controls){
        const auto player=control_player_;
        rows.push_back({
            L"DEVICE",
            device_name(settings.input.players[player].selected_device,devices)});
        enabled.push_back(true);
        for(const auto action:all_game_actions()){
            const auto it=settings.input.players[player].bindings.find(action);
            rows.push_back({
                action_name(action),
                it==settings.input.players[player].bindings.end()
                    ?L"UNBOUND"
                    :widen_ascii(it->second.code)});
            enabled.push_back(true);
        }
        draw_string(
            graphics,
            player==0?L"PLAYER 1":L"PLAYER 2",
            Gdiplus::RectF(760.0f,128.0f,175.0f,32.0f),
            15.0f,
            kJojoBlue,
            Gdiplus::FontStyleBold,
            Gdiplus::StringAlignmentFar);
    }else{
        rows={
            {L"HIGH CONTRAST UI",on_off(settings.accessibility.high_contrast_ui)},
            {L"MENU TEXT SCALE",std::to_wstring(settings.accessibility.menu_text_scale)+L"%"},
        };
        enabled={true,true};
    }

    const float text_scale=std::clamp(
        static_cast<float>(settings.accessibility.menu_text_scale)/100.0f,
        1.0f,1.35f);
    const float row_font=std::min(17.0f*text_scale,22.0f);
    const float row_step=page_==SettingsPage::controls?32.0f:56.0f;
    const float row_height=page_==SettingsPage::controls?29.0f:46.0f;

    for(std::size_t i=0;i<rows.size();++i){
        const float y=198.0f+static_cast<float>(i)*row_step;
        if(y+row_height>616.0f) break;
        const bool selected=i==selected_row_;
        const bool active=i<enabled.size()?enabled[i]:true;

        if(selected){
            const Gdiplus::PointF selection_shape[]={
                {365.0f,y},{944.0f,y},{928.0f,y+row_height},{365.0f,y+row_height}};
            Gdiplus::SolidBrush selection(Gdiplus::Color(225,12,121,175));
            graphics.FillPolygon(&selection,selection_shape,4);
        }else{
            Gdiplus::SolidBrush row_bg(Gdiplus::Color(95,16,34,45));
            graphics.FillRectangle(&row_bg,365.0f,y,563.0f,row_height);
        }

        draw_string(
            graphics,
            rows[i].first,
            Gdiplus::RectF(382.0f,y,275.0f,row_height),
            row_font,
            active?kText:kDisabled,
            Gdiplus::FontStyleBold);
        draw_string(
            graphics,
            active?L"<":L"",
            Gdiplus::RectF(663.0f,y,34.0f,row_height),
            row_font,
            selected?kJojoIce:kMuted,
            Gdiplus::FontStyleBold,
            Gdiplus::StringAlignmentCenter);
        draw_string(
            graphics,
            rows[i].second,
            Gdiplus::RectF(700.0f,y,185.0f,row_height),
            std::max(13.0f,row_font-2.0f),
            active?(selected?kText:kJojoIce):kDisabled,
            active?Gdiplus::FontStyleBold:Gdiplus::FontStyleRegular,
            Gdiplus::StringAlignmentCenter);
        draw_string(
            graphics,
            active?L">":L"",
            Gdiplus::RectF(890.0f,y,34.0f,row_height),
            row_font,
            selected?kJojoIce:kMuted,
            Gdiplus::FontStyleBold,
            Gdiplus::StringAlignmentCenter);
    }

    const std::wstring help=
        page_==SettingsPage::controls
            ?(capture_status.empty()
                ?L"ENTER remap  •  P switch player  •  TAB category  •  ESC back"
                :std::wstring(capture_status))
            :L"LEFT / RIGHT change  •  TAB category  •  ESC back";
    draw_string(
        graphics,
        help,
        Gdiplus::RectF(365.0f,620.0f,560.0f,28.0f),
        12.0f,
        capture_status.empty()?kMuted:kJojoIce);
}

LauncherUiAction LauncherUi::mouse_up(
    POINT client_point,
    const RECT& client,
    AppSettings& settings,
    const InputDeviceRegistry& devices) {
    const auto width = client.right - client.left;
    const auto height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return LauncherUiAction::none;

    const float ui_scale=std::min(
        static_cast<float>(width)/kUiWidth,
        static_cast<float>(height)/kUiHeight);
    if(ui_scale<=0.0f) return LauncherUiAction::none;
    const float ui_offset_x=
        (static_cast<float>(width)-kUiWidth*ui_scale)*0.5f;
    const float ui_offset_y=
        (static_cast<float>(height)-kUiHeight*ui_scale)*0.5f;
    const float x=
        (static_cast<float>(client_point.x)-ui_offset_x)/ui_scale;
    const float y=
        (static_cast<float>(client_point.y)-ui_offset_y)/ui_scale;

    if (screen_ == Screen::online) {
        const auto online_action =
            online_ui_.mouse_up(client_point, client, online_model_);
        if (online_action == OnlineUiAction::back_to_launcher) {
            show_main();
            return LauncherUiAction::none;
        }
        return map_online_action(online_action);
    }

    if (screen_ == Screen::main_menu) {
        const std::array<Gdiplus::RectF, 5> menu_rects{{
            {496.0f, 178.0f, 464.0f, 58.0f},
            {496.0f, 256.0f, 464.0f, 58.0f},
            {496.0f, 334.0f, 464.0f, 58.0f},
            {496.0f, 412.0f, 464.0f, 58.0f},
            {496.0f, 490.0f, 464.0f, 58.0f},
        }};
        for (std::size_t i = 0; i < menu_rects.size(); ++i) {
            if (contains(menu_rects[i], x, y)) {
                main_selection_ = i;
                return activate_main_item();
            }
        }
        return LauncherUiAction::none;
    }

    if (contains({52.0f, 620.0f, 250.0f, 65.0f}, x, y)) {
        show_main();
        return LauncherUiAction::none;
    }

    constexpr std::array<SettingsPage, 4> pages{
        SettingsPage::graphics,
        SettingsPage::audio,
        SettingsPage::controls,
        SettingsPage::accessibility};
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const Gdiplus::RectF tab{
            52.0f,
            218.0f + static_cast<float>(i) * 60.0f,
            258.0f,
            44.0f};
        if (contains(tab, x, y)) {
            page_ = pages[i];
            selected_row_ = 0;
            return LauncherUiAction::none;
        }
    }

    if (page_ == SettingsPage::controls &&
        contains({735.0f, 120.0f, 215.0f, 50.0f}, x, y)) {
        cycle_control_player(1);
        selected_row_ = 0;
        return LauncherUiAction::none;
    }

    const auto count = row_count();
    for (std::size_t i = 0; i < count; ++i) {
        const float row_step =
            page_ == SettingsPage::controls ? 32.0f : 56.0f;
        const float row_height =
            page_ == SettingsPage::controls ? 29.0f : 46.0f;
        const float row_y = 198.0f + static_cast<float>(i) * row_step;
        if (!contains({365.0f, row_y, 579.0f, row_height}, x, y)) continue;

        selected_row_ = i;
        if (page_ == SettingsPage::controls && i > 0 &&
            x >= 680.0f && x <= 905.0f) {
            return LauncherUiAction::begin_binding_capture;
        }
        if (x < 700.0f) {
            return adjust_setting(-1, settings, devices);
        }
        if (x > 880.0f) {
            return adjust_setting(1, settings, devices);
        }
        return LauncherUiAction::none;
    }

    return LauncherUiAction::none;
}

LauncherUiAction LauncherUi::key_down(
    WPARAM key,
    AppSettings& settings,
    const InputDeviceRegistry& devices) {
    if (screen_ == Screen::online) {
        const auto online_action=online_ui_.key_down(key,online_model_);
        if(online_action==OnlineUiAction::back_to_launcher){
            show_main();
            return LauncherUiAction::none;
        }
        return map_online_action(online_action);
    }

    if (screen_ == Screen::main_menu) {
        if (key == VK_UP) {
            main_selection_ = wrap_index(main_selection_, -1, 5);
            return LauncherUiAction::none;
        }
        if (key == VK_DOWN) {
            main_selection_ = wrap_index(main_selection_, 1, 5);
            return LauncherUiAction::none;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            return activate_main_item();
        }
        return LauncherUiAction::none;
    }

    if (key == VK_ESCAPE) {
        show_main();
        return LauncherUiAction::none;
    }
    if (key == VK_TAB) {
        cycle_page((GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1);
        return LauncherUiAction::none;
    }
    if (page_ == SettingsPage::controls &&
        (key == 'P' || key == VK_PRIOR || key == VK_NEXT)) {
        cycle_control_player(key == VK_PRIOR ? -1 : 1);
        selected_row_ = 0;
        return LauncherUiAction::none;
    }
    if (key == VK_UP) {
        selected_row_ = wrap_index(selected_row_, -1, row_count());
        return LauncherUiAction::none;
    }
    if (key == VK_DOWN) {
        selected_row_ = wrap_index(selected_row_, 1, row_count());
        return LauncherUiAction::none;
    }
    if (key == VK_LEFT) {
        return adjust_setting(-1, settings, devices);
    }
    if (key == VK_RIGHT) {
        return adjust_setting(1, settings, devices);
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        if (page_ == SettingsPage::controls && selected_row_ > 0) {
            return LauncherUiAction::begin_binding_capture;
        }
        return adjust_setting(1, settings, devices);
    }

    return LauncherUiAction::none;
}

void LauncherUi::char_input(
    wchar_t ch,
    AppSettings& /*settings*/) {
    if (screen_ == Screen::online) {
        online_ui_.char_input(ch, online_model_);
    }
}

} // namespace jojo::win32
#endif
