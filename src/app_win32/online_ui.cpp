#ifdef _WIN32
#define NOMINMAX
#include "app_win32/online_ui.h"

#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace jojo::win32 {
namespace {

constexpr float kUiWidth = 1600.0f;
constexpr float kUiHeight = 900.0f;

const Gdiplus::Color kBg(255, 3, 3, 3);
const Gdiplus::Color kPanel(255, 10, 10, 10);
const Gdiplus::Color kWhite(255, 238, 238, 238);
const Gdiplus::Color kMuted(255, 150, 150, 150);
const Gdiplus::Color kBlue(255, 0, 100, 255);
const Gdiplus::Color kOrange(255, 230, 68, 0);
const Gdiplus::Color kGreen(255, 20, 185, 55);
const Gdiplus::Color kRed(255, 245, 25, 25);
const Gdiplus::Color kGray(255, 110, 110, 110);

bool inside(float x, float y, float l, float t, float r, float b) noexcept {
    return x >= l && x <= r && y >= t && y <= b;
}

std::wstring widen(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        required);
    return result;
}

void draw_text(
    Gdiplus::Graphics& g,
    const std::wstring& text,
    float x,
    float y,
    float w,
    float h,
    float size,
    const Gdiplus::Color& color,
    bool bold = false,
    Gdiplus::StringAlignment align = Gdiplus::StringAlignmentNear) {
    Gdiplus::Font font(
        L"Arial Narrow",
        size,
        bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
        Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(color);
    Gdiplus::StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::RectF rect(x, y, w, h);
    g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

void outline(
    Gdiplus::Graphics& g,
    float x,
    float y,
    float w,
    float h,
    const Gdiplus::Color& color = kWhite,
    float thickness = 3.0f) {
    Gdiplus::Pen pen(color, thickness);
    g.DrawRectangle(&pen, x, y, w, h);
}

void fill(
    Gdiplus::Graphics& g,
    float x,
    float y,
    float w,
    float h,
    const Gdiplus::Color& color) {
    Gdiplus::SolidBrush brush(color);
    g.FillRectangle(&brush, x, y, w, h);
}

void button(
    Gdiplus::Graphics& g,
    const wchar_t* label,
    float x,
    float y,
    float w,
    float h,
    const Gdiplus::Color& border = kWhite,
    bool enabled = true) {
    outline(g, x, y, w, h, enabled ? border : kGray, 3.0f);
    draw_text(
        g,
        label,
        x,
        y,
        w,
        h,
        std::max(22.0f, h * 0.44f),
        enabled ? kWhite : kGray,
        true,
        Gdiplus::StringAlignmentCenter);
}

void draw_header_bars(
    Gdiplus::Graphics& g,
    float x,
    float y,
    float width,
    float h) {
    const float half = width * 0.5f;
    outline(g, x, y, width, h, kWhite, 3.0f);
    fill(g, x + 2.0f, y + 2.0f, half - 2.0f, h - 4.0f, kBlue);
    fill(g, x + half, y + 2.0f, half - 2.0f, h - 4.0f, kOrange);
    Gdiplus::Pen divider(kWhite, 3.0f);
    g.DrawLine(&divider, x + half, y, x + half, y + h);
}

void draw_background(Gdiplus::Graphics& g) {
    fill(g, 0, 0, kUiWidth, kUiHeight, kBg);

    Gdiplus::Pen scratch(Gdiplus::Color(20, 255, 255, 255), 1.0f);
    for (int i = 0; i < 38; ++i) {
        const float y = 25.0f + static_cast<float>((i * 71) % 850);
        const float x = static_cast<float>((i * 137) % 1500);
        g.DrawLine(&scratch, x, y, std::min(kUiWidth, x + 220.0f), y + 24.0f);
    }

    // Minimal original JoJo-inspired emblem in place of the reference game's logo.
    Gdiplus::Pen emblem(kWhite, 4.0f);
    g.DrawEllipse(&emblem, 88.0f, 34.0f, 96.0f, 96.0f);
    g.DrawLine(&emblem, 135.0f, 40.0f, 135.0f, 126.0f);
    g.DrawLine(&emblem, 98.0f, 82.0f, 173.0f, 82.0f);
    draw_text(g, L"JOJO", 72.0f, 126.0f, 135.0f, 40.0f, 26.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
}

void draw_close_button(Gdiplus::Graphics& g, float x, float y) {
    fill(g, x, y, 54.0f, 54.0f, kRed);
    draw_text(g, L"×", x, y - 2.0f, 54.0f, 54.0f, 42.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
}


std::string_view keyboard_row_text(std::size_t row) noexcept {
    static constexpr std::array<std::string_view, 4> rows{
        "1234567890",
        "QWERTYUIOP",
        "ASDFGHJKL",
        "ZXCVBNM",
    };
    return row < rows.size() ? rows[row] : std::string_view{};
}

std::size_t keyboard_row_length(std::size_t row) noexcept {
    if (row < 4u) return keyboard_row_text(row).size();
    return row == 4u ? 3u : 0u;
}

void draw_name_keyboard(
    Gdiplus::Graphics& g,
    const OnlineLobbyModel& model,
    std::size_t selected_row,
    std::size_t selected_column) {
    draw_text(
        g, L"PLAYER NAME",
        430.0f, 70.0f, 740.0f, 72.0f,
        50.0f, kWhite, true,
        Gdiplus::StringAlignmentCenter);

    draw_text(g, L"ENTER PLAYER NAME", 350.0f, 165.0f, 330.0f, 48.0f, 27.0f, kWhite, true);
    outline(g, 680.0f, 165.0f, 570.0f, 48.0f, kWhite, 2.0f);
    draw_text(g, widen(model.player_name), 695.0f, 165.0f, 535.0f, 48.0f, 25.0f, kWhite);

    constexpr float start_y = 285.0f;
    constexpr float cell_w = 80.0f;
    constexpr float cell_h = 54.0f;
    constexpr float gap_x = 8.0f;
    constexpr float gap_y = 14.0f;

    for (std::size_t row = 0u; row < 4u; ++row) {
        const auto text = keyboard_row_text(row);
        const float row_width =
            static_cast<float>(text.size()) * cell_w +
            static_cast<float>(text.size() - 1u) * gap_x;
        const float row_x = (kUiWidth - row_width) * 0.5f;
        for (std::size_t column = 0u; column < text.size(); ++column) {
            const float x = row_x + static_cast<float>(column) * (cell_w + gap_x);
            const float y = start_y + static_cast<float>(row) * (cell_h + gap_y);
            const bool selected =
                row == selected_row && column == selected_column;
            if (selected) {
                fill(g, x, y, cell_w, cell_h, Gdiplus::Color(90, 0, 100, 255));
            }
            outline(g, x, y, cell_w, cell_h, selected ? kBlue : kWhite, selected ? 4.0f : 2.0f);
            std::wstring label(1u, static_cast<wchar_t>(text[column]));
            draw_text(
                g, label, x, y, cell_w, cell_h,
                26.0f, selected ? kWhite : kMuted, true,
                Gdiplus::StringAlignmentCenter);
        }
    }

    constexpr float action_y = 585.0f;
    struct ActionKey {
        const wchar_t* label;
        float x;
        float width;
    };
    constexpr std::array<ActionKey, 3> actions{{
        {L"SPACE", 350.0f, 260.0f},
        {L"BACKSPACE", 630.0f, 300.0f},
        {L"ACCEPT", 950.0f, 300.0f},
    }};
    for (std::size_t column = 0u; column < actions.size(); ++column) {
        const auto& key = actions[column];
        const bool selected =
            selected_row == 4u && selected_column == column;
        if (selected) {
            fill(g, key.x, action_y, key.width, 62.0f, Gdiplus::Color(90, 0, 100, 255));
        }
        outline(g, key.x, action_y, key.width, 62.0f, selected ? kBlue : kWhite, selected ? 4.0f : 2.0f);
        draw_text(
            g, key.label, key.x, action_y, key.width, 62.0f,
            24.0f, selected ? kWhite : kMuted, true,
            Gdiplus::StringAlignmentCenter);
    }

    draw_text(
        g,
        L"ARROWS / D-PAD: MOVE     ENTER / A: SELECT     ESC / B: BACK",
        350.0f, 695.0f, 900.0f, 44.0f,
        19.0f, kMuted, true,
        Gdiplus::StringAlignmentCenter);
}

void draw_online_home(Gdiplus::Graphics& g, const OnlineLobbyModel& model) {
    draw_text(g, L"ONLINE", 1090.0f, 110.0f, 300.0f, 70.0f, 56.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    draw_text(g, L"NAME", 255.0f, 140.0f, 120.0f, 46.0f, 27.0f, kWhite, true);
    outline(g, 375.0f, 140.0f, 335.0f, 46.0f, kWhite, 2.0f);
    draw_text(g, widen(model.player_name), 388.0f, 140.0f, 315.0f, 46.0f, 25.0f, kWhite);

    draw_text(g, L"REGION", 255.0f, 202.0f, 120.0f, 46.0f, 27.0f, kWhite, true);
    outline(g, 375.0f, 202.0f, 460.0f, 46.0f, kWhite, 2.0f);
    draw_text(g, widen(model.region), 388.0f, 202.0f, 420.0f, 46.0f, 22.0f, kWhite);
    draw_text(g, L"▼", 800.0f, 202.0f, 28.0f, 46.0f, 18.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    draw_text(g, L"CUSTOM GAME", 1010.0f, 268.0f, 410.0f, 58.0f, 47.0f, kWhite, true);
    draw_text(g, L"MATCHMAKING > CASUAL", 1010.0f, 338.0f, 410.0f, 58.0f, 38.0f, kWhite, true);
    draw_text(g, L"MATCHMAKING > RANKED", 1010.0f, 408.0f, 410.0f, 58.0f, 38.0f, kWhite, true);
    draw_text(g, L"HOW TO PLAY ONLINE", 1010.0f, 478.0f, 410.0f, 58.0f, 36.0f, kWhite, true);

    draw_text(g, L"2 PLAYERS", 1220.0f, 552.0f, 190.0f, 50.0f, 27.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    if (!model.status.empty()) {
        draw_text(g, widen(model.status), 235.0f, 690.0f, 720.0f, 70.0f, 20.0f, kMuted, false);
    }

    draw_text(g, L"←", 85.0f, 785.0f, 90.0f, 62.0f, 55.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    draw_text(g, L"⚙", 1430.0f, 785.0f, 90.0f, 62.0f, 44.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
}

void draw_public_servers(Gdiplus::Graphics& g, const OnlineLobbyModel& model) {
    draw_text(g, L"PUBLIC SERVERS", 520.0f, 70.0f, 560.0f, 70.0f, 54.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    const float x = 540.0f;
    const float y = 150.0f;
    const float w = 520.0f;
    draw_header_bars(g, x, y, w, 44.0f);

    const std::size_t max_rows = 7u;
    for (std::size_t i = 0; i < max_rows; ++i) {
        const float row_y = y + 44.0f + static_cast<float>(i) * 52.0f;
        outline(g, x, row_y, w, 52.0f, kWhite, 2.0f);
        if (i >= model.rooms.size()) continue;

        const auto& room = model.rooms[i];
        if (model.selected_room && *model.selected_room == i) {
            fill(g, x + 2.0f, row_y + 2.0f, w - 4.0f, 48.0f,
                 Gdiplus::Color(65, 255, 255, 255));
        }
        Gdiplus::SolidBrush dot(room.available ? kGreen : kRed);
        g.FillEllipse(&dot, x + 14.0f, row_y + 16.0f, 20.0f, 20.0f);
        draw_text(g, widen(room.name), x + 48.0f, row_y, 345.0f, 52.0f, 22.0f, kWhite, true);
        draw_text(
            g,
            std::to_wstring(room.players) + L"/" + std::to_wstring(room.max_players),
            x + 415.0f,
            row_y,
            90.0f,
            52.0f,
            22.0f,
            kWhite,
            true,
            Gdiplus::StringAlignmentFar);
    }

    if (model.rooms.empty()) {
        draw_text(g, L"NO PUBLIC ROOMS", x, y + 190.0f, w, 80.0f, 27.0f, kMuted, true,
                  Gdiplus::StringAlignmentCenter);
    }

    button(g, L"REFRESH", 1090.0f, 150.0f, 170.0f, 52.0f);

    draw_text(g, L"DIRECT CONNECT", 1090.0f, 238.0f, 390.0f, 46.0f, 26.0f, kWhite, true);
    draw_text(g, L"IP:PORT", 1090.0f, 288.0f, 110.0f, 44.0f, 20.0f, kMuted, true);
    outline(g, 1195.0f, 288.0f, 285.0f, 44.0f, kWhite, 2.0f);
    draw_text(g, widen(model.direct_connect_endpoint), 1205.0f, 288.0f, 265.0f, 44.0f, 19.0f, kWhite);
    button(g, L"CONNECT DIRECT", 1090.0f, 350.0f, 390.0f, 58.0f, kBlue);
    draw_text(g, L"HOST PORT: 27886", 1090.0f, 420.0f, 390.0f, 38.0f, 18.0f, kMuted, true);

    draw_text(g, L"LOBBY", 460.0f, 620.0f, 110.0f, 44.0f, 25.0f, kWhite, true);
    outline(g, 575.0f, 620.0f, 380.0f, 44.0f, kWhite, 2.0f);
    const std::wstring room_name =
        (model.selected_room && *model.selected_room < model.rooms.size())
        ? widen(model.rooms[*model.selected_room].name)
        : L"SELECT A ROOM";
    draw_text(g, room_name, 585.0f, 620.0f, 355.0f, 44.0f, 22.0f, kWhite);

    draw_text(g, L"PASSWORD", 420.0f, 678.0f, 150.0f, 44.0f, 25.0f, kWhite, true);
    outline(g, 575.0f, 678.0f, 380.0f, 44.0f, kWhite, 2.0f);
    draw_text(g, L"••••••••", 585.0f, 678.0f, 355.0f, 44.0f, 22.0f, kMuted);

    button(g, L"HOST", 540.0f, 748.0f, 210.0f, 72.0f);
    button(g, L"CONNECT", 780.0f, 748.0f, 240.0f, 72.0f,
           (model.selected_room ? kBlue : kGray), model.selected_room.has_value());
    draw_text(g, L"←", 85.0f, 790.0f, 90.0f, 62.0f, 55.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    if (!model.status.empty()) {
        draw_text(g, widen(model.status), 1090.0f, 475.0f, 390.0f, 120.0f, 18.0f, kMuted);
    }
}

void draw_create_lobby(Gdiplus::Graphics& g, const OnlineLobbyModel& model) {
    draw_text(g, L"CREATE LOBBY", 520.0f, 70.0f, 560.0f, 70.0f, 54.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    draw_header_bars(g, 540.0f, 150.0f, 520.0f, 44.0f);

    draw_text(g, L"LOBBY", 405.0f, 245.0f, 160.0f, 50.0f, 28.0f, kWhite, true);
    outline(g, 565.0f, 245.0f, 430.0f, 50.0f, kWhite, 2.0f);
    draw_text(g, widen(model.create_room.name), 580.0f, 245.0f, 395.0f, 50.0f, 23.0f, kWhite);

    draw_text(g, L"MAX PLAYERS", 340.0f, 320.0f, 225.0f, 50.0f, 28.0f, kWhite, true);
    outline(g, 565.0f, 320.0f, 430.0f, 50.0f, kWhite, 2.0f);
    draw_text(g, std::to_wstring(model.create_room.max_players), 580.0f, 320.0f, 350.0f, 50.0f, 23.0f, kWhite);
    draw_text(g, L"▼", 950.0f, 320.0f, 35.0f, 50.0f, 18.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    draw_text(g, L"PRIVACY", 405.0f, 395.0f, 160.0f, 50.0f, 28.0f, kWhite, true);
    outline(g, 565.0f, 395.0f, 430.0f, 50.0f, kWhite, 2.0f);
    draw_text(
        g,
        model.create_room.privacy == OnlineRoomPrivacy::public_room ? L"PUBLIC" : L"PRIVATE",
        580.0f, 395.0f, 350.0f, 50.0f, 23.0f, kWhite);
    draw_text(g, L"▼", 950.0f, 395.0f, 35.0f, 50.0f, 18.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    draw_text(g, L"PASSWORD", 360.0f, 470.0f, 205.0f, 50.0f, 28.0f, kWhite, true);
    outline(g, 565.0f, 470.0f, 430.0f, 50.0f,
            model.create_room.privacy == OnlineRoomPrivacy::private_room ? kWhite : kGray,
            2.0f);
    draw_text(g,
              model.create_room.password.empty() ? L"OPTIONAL" : L"••••••••",
              580.0f, 470.0f, 395.0f, 50.0f, 23.0f,
              model.create_room.privacy == OnlineRoomPrivacy::private_room ? kWhite : kGray);

    button(g, L"HOST", 655.0f, 600.0f, 290.0f, 84.0f);
    draw_text(g, L"←", 85.0f, 790.0f, 90.0f, 62.0f, 55.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
}

void draw_find_match(Gdiplus::Graphics& g, const OnlineLobbyModel& model) {
    fill(g, 0, 0, kUiWidth, kUiHeight, Gdiplus::Color(215, 0, 0, 0));

    const float x = 500.0f;
    const float y = 105.0f;
    const float w = 600.0f;
    const float h = 665.0f;
    fill(g, x, y, w, h, kPanel);
    outline(g, x, y, w, h, kWhite, 4.0f);

    draw_close_button(g, x + w - 60.0f, y + 6.0f);
    draw_header_bars(g, x + 120.0f, y + 80.0f, 360.0f, 48.0f);

    draw_text(g, L"2 PLAYERS", x + 100.0f, y + 160.0f, 400.0f, 75.0f, 40.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    button(g, L"FIND", x + 170.0f, y + 255.0f, 260.0f, 90.0f, kBlue);

    const bool casual = model.queue == OnlineMatchQueue::casual;
    button(g, L"CASUAL", x + 95.0f, y + 390.0f, 195.0f, 62.0f, casual ? kBlue : kWhite);
    button(g, L"RANKED", x + 310.0f, y + 390.0f, 195.0f, 62.0f, casual ? kWhite : kOrange);

    draw_text(g, L"CURRENT REGION", x + 90.0f, y + 485.0f, 420.0f, 50.0f, 28.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    outline(g, x + 60.0f, y + 540.0f, 480.0f, 48.0f, kWhite, 2.0f);
    draw_text(g, widen(model.region), x + 75.0f, y + 540.0f, 430.0f, 48.0f, 22.0f, kWhite,
              false, Gdiplus::StringAlignmentCenter);
    draw_text(g, L"▼", x + 500.0f, y + 540.0f, 30.0f, 48.0f, 18.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
}

void draw_searching(Gdiplus::Graphics& g, const OnlineLobbyModel& model) {
    fill(g, 300.0f, 105.0f, 1000.0f, 665.0f, kPanel);
    outline(g, 300.0f, 105.0f, 1000.0f, 665.0f, kWhite, 4.0f);
    draw_header_bars(g, 620.0f, 155.0f, 360.0f, 48.0f);

    const bool connecting = model.screen == OnlineLobbyScreen::connecting;
    draw_text(g,
              connecting ? L"CONNECTING..." : L"SEARCHING OPPONENT",
              440.0f, 260.0f, 720.0f, 95.0f, 50.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    Gdiplus::Pen ring(kWhite, 5.0f);
    g.DrawEllipse(&ring, 690.0f, 390.0f, 220.0f, 220.0f);
    const int active_dot=static_cast<int>((GetTickCount64()/120u)%8u);
    for (int i = 0; i < 8; ++i) {
        const double angle = i * 3.14159265358979323846 / 4.0;
        const float px = 800.0f + static_cast<float>(std::cos(angle) * 95.0) - 8.0f;
        const float py = 500.0f + static_cast<float>(std::sin(angle) * 95.0) - 8.0f;
        Gdiplus::SolidBrush dot(
            i==active_dot ? kBlue : Gdiplus::Color(255,75,75,75));
        g.FillEllipse(&dot, px, py, 16.0f, 16.0f);
    }
    if (!model.status.empty()) {
        draw_text(g, widen(model.status), 450.0f, 630.0f, 700.0f, 55.0f, 19.0f, kMuted,
                  false, Gdiplus::StringAlignmentCenter);
    }
    button(g, L"BACK", 705.0f, 700.0f, 190.0f, 54.0f);
}

void draw_lobby(Gdiplus::Graphics& g, const OnlineLobbyModel& model, const std::wstring& chat) {
    draw_text(g, L"LOBBY", 620.0f, 70.0f, 360.0f, 70.0f, 54.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    draw_header_bars(g, 500.0f, 145.0f, 600.0f, 48.0f);

    // Player 1
    outline(g, 510.0f, 260.0f, 230.0f, 185.0f, kWhite, 3.0f);
    fill(g, 510.0f, 260.0f, 230.0f, 44.0f, kBlue);
    draw_text(g, widen(model.player_name), 520.0f, 262.0f, 210.0f, 40.0f, 22.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    draw_text(g, L"+", 510.0f, 305.0f, 230.0f, 140.0f, 70.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    // Player 2 / opponent
    outline(g, 860.0f, 260.0f, 230.0f, 185.0f, kWhite, 3.0f);
    fill(g, 860.0f, 260.0f, 230.0f, 44.0f, kOrange);
    draw_text(g, widen(model.remote_player_name), 870.0f, 262.0f, 210.0f, 40.0f, 22.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    draw_text(g, L"+", 860.0f, 305.0f, 230.0f, 140.0f, 70.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);

    draw_text(g, L"SPECTATE", 707.0f, 470.0f, 190.0f, 38.0f, 22.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    outline(g, 736.0f, 510.0f, 128.0f, 96.0f, kWhite, 3.0f);
    draw_text(g, L"+", 736.0f, 510.0f, 128.0f, 96.0f, 52.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    draw_text(g, L"SPECTATORS: " + std::to_wstring(model.spectator_count), 715.0f, 616.0f,
              170.0f, 32.0f, 16.0f, kWhite, true, Gdiplus::StringAlignmentCenter);

    button(g, L"CHANGE SLOT", 435.0f, 565.0f, 230.0f, 62.0f);
    outline(g, 470.0f, 652.0f, 34.0f, 34.0f, kWhite, 3.0f);
    if (model.ready) {
        fill(g, 476.0f, 658.0f, 22.0f, 22.0f, kGreen);
    }
    draw_text(g, L"READY", 515.0f, 644.0f, 150.0f, 48.0f, 24.0f, kWhite, true);

    outline(g, 895.0f, 652.0f, 34.0f, 34.0f, kWhite, 3.0f);
    if (model.remote_ready) {
        fill(g, 901.0f, 658.0f, 22.0f, 22.0f, kGreen);
    }
    draw_text(g, L"RIVAL READY", 940.0f, 644.0f, 175.0f, 48.0f, 20.0f, kWhite, true);

    const bool revision_match =
        online_game_revision_matches(model);
    const bool can_start =
        model.local_player_is_host &&
        model.ready &&
        model.remote_ready &&
        revision_match;
    button(g, L"START", 880.0f, 565.0f, 230.0f, 62.0f,
           can_start ? kOrange : kGray,
           can_start);

    const wchar_t* revision_text =
        revision_match
        ? L"GAME REVISION: MATCH"
        : (model.local_game_revision.empty() ||
           model.remote_game_revision.empty()
            ? L"GAME REVISION: WAITING FOR BOTH DISCS"
            : L"GAME REVISION: MISMATCH");
    draw_text(
        g,
        revision_text,
        575.0f,
        692.0f,
        450.0f,
        32.0f,
        16.0f,
        revision_match ? kGreen : kMuted,
        true,
        Gdiplus::StringAlignmentCenter);

    draw_text(g, L"CHAT", 1220.0f, 215.0f, 260.0f, 48.0f, 31.0f, kWhite, true,
              Gdiplus::StringAlignmentCenter);
    outline(g, 1180.0f, 265.0f, 340.0f, 370.0f, kWhite, 3.0f);
    const std::size_t visible_messages = 8u;
    const std::size_t begin =
        model.chat_messages.size() > visible_messages
        ? model.chat_messages.size() - visible_messages
        : 0u;
    float chat_y = 280.0f;
    for (std::size_t i = begin; i < model.chat_messages.size(); ++i) {
        const auto& message = model.chat_messages[i];
        draw_text(
            g,
            widen(message.sender) + L": " + widen(message.text),
            1194.0f,
            chat_y,
            312.0f,
            38.0f,
            16.0f,
            kWhite);
        chat_y += 42.0f;
    }
    outline(g, 1180.0f, 650.0f, 340.0f, 48.0f, kWhite, 2.0f);
    draw_text(g, chat.empty() ? L"TYPE A MESSAGE..." : chat,
              1190.0f, 650.0f, 320.0f, 48.0f, 18.0f, chat.empty() ? kMuted : kWhite);

    draw_close_button(g, 1450.0f, 90.0f);
    if (!model.status.empty()) {
        draw_text(g, widen(model.status), 425.0f, 725.0f, 720.0f, 55.0f, 18.0f, kMuted,
                  false, Gdiplus::StringAlignmentCenter);
    }
}

} // namespace

void OnlineUi::show_home() noexcept {
    text_field_ = TextField::none;
    name_keyboard_open_ = false;
    keyboard_row_ = 0u;
    keyboard_column_ = 0u;
    selected_row_ = 0u;
    selected_room_row_ = 0u;
}

std::string OnlineUi::take_chat_message() {
    if (chat_draft_.empty()) return {};
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, chat_draft_.data(),
        static_cast<int>(chat_draft_.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, chat_draft_.data(),
        static_cast<int>(chat_draft_.size()),
        result.data(), required, nullptr, nullptr);
    chat_draft_.clear();
    text_field_ = TextField::chat;
    return result;
}


void OnlineUi::move_name_keyboard(int dx, int dy) noexcept {
    int row = static_cast<int>(keyboard_row_);
    row += dy;
    if (row < 0) row = 4;
    if (row > 4) row = 0;
    keyboard_row_ = static_cast<std::size_t>(row);

    const auto length = keyboard_row_length(keyboard_row_);
    if (length == 0u) {
        keyboard_column_ = 0u;
        return;
    }

    if (dy != 0) {
        keyboard_column_ = std::min(keyboard_column_, length - 1u);
    }
    if (dx > 0) {
        keyboard_column_ = (keyboard_column_ + 1u) % length;
    } else if (dx < 0) {
        keyboard_column_ =
            keyboard_column_ == 0u ? length - 1u : keyboard_column_ - 1u;
    }
}

void OnlineUi::activate_name_keyboard_key(OnlineLobbyModel& model) {
    if (keyboard_row_ < 4u) {
        const auto row = keyboard_row_text(keyboard_row_);
        if (keyboard_column_ < row.size() && model.player_name.size() < 24u) {
            model.player_name.push_back(row[keyboard_column_]);
        }
        return;
    }

    switch (keyboard_column_) {
        case 0u:
            if (!model.player_name.empty() &&
                model.player_name.back() != ' ' &&
                model.player_name.size() < 24u) {
                model.player_name.push_back(' ');
            }
            break;
        case 1u:
            if (!model.player_name.empty()) model.player_name.pop_back();
            break;
        case 2u:
            if (valid_online_player_name(model.player_name)) {
                name_keyboard_open_ = false;
                text_field_ = TextField::none;
            } else {
                model.status = "PLAYER NAME MUST CONTAIN 1-24 CHARACTERS.";
            }
            break;
        default:
            break;
    }
}

void OnlineUi::cycle_region(
    OnlineLobbyModel& model,
    int direction) noexcept {
    static const std::array<const char*, 6> regions{
        "SOUTH AMERICA - BRAZIL",
        "SOUTH AMERICA - ARGENTINA",
        "NORTH AMERICA - EAST",
        "EUROPE",
        "ASIA",
        "OCEANIA",
    };
    std::size_t index = 0u;
    for (std::size_t i = 0u; i < regions.size(); ++i) {
        if (model.region == regions[i]) {
            index = i;
            break;
        }
    }
    if (direction > 0) index = (index + 1u) % regions.size();
    else index = index == 0u ? regions.size() - 1u : index - 1u;
    model.region = regions[index];
}

void OnlineUi::toggle_queue(OnlineLobbyModel& model) noexcept {
    model.queue = model.queue == OnlineMatchQueue::casual
        ? OnlineMatchQueue::ranked
        : OnlineMatchQueue::casual;
}

void OnlineUi::cycle_privacy(OnlineLobbyModel& model) noexcept {
    model.create_room.privacy =
        model.create_room.privacy == OnlineRoomPrivacy::public_room
        ? OnlineRoomPrivacy::private_room
        : OnlineRoomPrivacy::public_room;
    if (model.create_room.privacy == OnlineRoomPrivacy::public_room) {
        model.create_room.password.clear();
    }
}

void OnlineUi::cycle_max_players(
    OnlineLobbyModel& model,
    int direction) noexcept {
    auto value = static_cast<int>(model.create_room.max_players);
    value += direction > 0 ? 1 : -1;
    if (value > 8) value = 2;
    if (value < 2) value = 8;
    model.create_room.max_players = static_cast<std::uint32_t>(value);
}

void OnlineUi::paint(
    HDC dc,
    const RECT& client,
    OnlineLobbyModel& model) {
    if (!dc) return;
    const LONG cw = client.right - client.left;
    const LONG ch = client.bottom - client.top;
    if (cw <= 0 || ch <= 0) return;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::SolidBrush letterbox(kBg);
    g.FillRectangle(&letterbox, 0, 0, cw, ch);

    const float scale=std::min(
        static_cast<float>(cw)/kUiWidth,
        static_cast<float>(ch)/kUiHeight);
    const float offset_x=
        (static_cast<float>(cw)-kUiWidth*scale)*0.5f;
    const float offset_y=
        (static_cast<float>(ch)-kUiHeight*scale)*0.5f;
    g.TranslateTransform(offset_x,offset_y);
    g.ScaleTransform(scale,scale);

    draw_background(g);

    switch (model.screen) {
        case OnlineLobbyScreen::home:
            if (name_keyboard_open_) {
                draw_name_keyboard(
                    g, model, keyboard_row_, keyboard_column_);
            } else {
                draw_online_home(g, model);
            }
            break;
        case OnlineLobbyScreen::public_servers:
            draw_public_servers(g, model);
            break;
        case OnlineLobbyScreen::create_lobby:
            draw_create_lobby(g, model);
            break;
        case OnlineLobbyScreen::find_match:
            draw_find_match(g, model);
            break;
        case OnlineLobbyScreen::searching:
        case OnlineLobbyScreen::connecting:
            draw_searching(g, model);
            break;
        case OnlineLobbyScreen::lobby:
            draw_lobby(g, model, chat_draft_);
            break;
    }
}

OnlineUiAction OnlineUi::mouse_up(
    POINT p,
    const RECT& client,
    OnlineLobbyModel& model) {
    const LONG cw = client.right - client.left;
    const LONG ch = client.bottom - client.top;
    if (cw <= 0 || ch <= 0) return OnlineUiAction::none;

    const float scale=std::min(
        static_cast<float>(cw)/kUiWidth,
        static_cast<float>(ch)/kUiHeight);
    const float offset_x=
        (static_cast<float>(cw)-kUiWidth*scale)*0.5f;
    const float offset_y=
        (static_cast<float>(ch)-kUiHeight*scale)*0.5f;
    const float x=(static_cast<float>(p.x)-offset_x)/scale;
    const float y=(static_cast<float>(p.y)-offset_y)/scale;
    if(x<0.0f||y<0.0f||x>kUiWidth||y>kUiHeight){
        return OnlineUiAction::none;
    }

    if (model.screen == OnlineLobbyScreen::home && name_keyboard_open_) {
        constexpr float start_y = 285.0f;
        constexpr float cell_w = 80.0f;
        constexpr float cell_h = 54.0f;
        constexpr float gap_x = 8.0f;
        constexpr float gap_y = 14.0f;

        for (std::size_t row = 0u; row < 4u; ++row) {
            const auto text = keyboard_row_text(row);
            const float row_width =
                static_cast<float>(text.size()) * cell_w +
                static_cast<float>(text.size() - 1u) * gap_x;
            const float row_x = (kUiWidth - row_width) * 0.5f;
            const float row_y =
                start_y + static_cast<float>(row) * (cell_h + gap_y);
            for (std::size_t column = 0u; column < text.size(); ++column) {
                const float key_x =
                    row_x + static_cast<float>(column) * (cell_w + gap_x);
                if (inside(
                        x, y,
                        key_x, row_y,
                        key_x + cell_w, row_y + cell_h)) {
                    keyboard_row_ = row;
                    keyboard_column_ = column;
                    activate_name_keyboard_key(model);
                    return OnlineUiAction::none;
                }
            }
        }

        constexpr float action_y = 585.0f;
        constexpr std::array<float, 3> action_x{
            350.0f, 630.0f, 950.0f};
        constexpr std::array<float, 3> action_w{
            260.0f, 300.0f, 300.0f};
        for (std::size_t column = 0u; column < 3u; ++column) {
            if (inside(
                    x, y,
                    action_x[column], action_y,
                    action_x[column] + action_w[column],
                    action_y + 62.0f)) {
                keyboard_row_ = 4u;
                keyboard_column_ = column;
                activate_name_keyboard_key(model);
                return OnlineUiAction::none;
            }
        }

        if (inside(x, y, 60.0f, 770.0f, 190.0f, 865.0f)) {
            name_keyboard_open_ = false;
        }
        return OnlineUiAction::none;
    }

    if (model.screen == OnlineLobbyScreen::home) {
        if (inside(x, y, 375, 140, 710, 186)) {
            name_keyboard_open_ = true;
            keyboard_row_ = 0u;
            keyboard_column_ = 0u;
            text_field_ = TextField::none;
            return OnlineUiAction::none;
        }
        if (inside(x, y, 375, 202, 835, 248)) {
            cycle_region(model, 1);
            return OnlineUiAction::none;
        }
        if (inside(x, y, 1010, 268, 1420, 326)) {
            online_open_public_servers(model);
            return OnlineUiAction::refresh_public_rooms;
        }
        if (inside(x, y, 1010, 338, 1420, 396)) {
            online_open_find_match(model, OnlineMatchQueue::casual);
            return OnlineUiAction::none;
        }
        if (inside(x, y, 1010, 408, 1420, 466)) {
            online_open_find_match(model, OnlineMatchQueue::ranked);
            return OnlineUiAction::none;
        }
        if (inside(x, y, 1010, 478, 1420, 536)) {
            model.status = "ONLINE: HOST/JOIN USES THE NATIVE UDP + ROLLBACK CORE.";
            return OnlineUiAction::none;
        }
        if (inside(x, y, 60, 770, 190, 865)) {
            return OnlineUiAction::back_to_launcher;
        }
        return OnlineUiAction::none;
    }

    if (model.screen == OnlineLobbyScreen::public_servers) {
        const float row_x = 540.0f;
        const float row_y0 = 194.0f;
        for (std::size_t i = 0; i < std::min<std::size_t>(model.rooms.size(), 7u); ++i) {
            const float ry = row_y0 + static_cast<float>(i) * 52.0f;
            if (inside(x, y, row_x, ry, row_x + 520.0f, ry + 52.0f)) {
                const auto selected = online_select_room(model, i);
                if (!selected) model.status = selected.detail;
                return OnlineUiAction::none;
            }
        }
        if (inside(x, y, 1090, 150, 1260, 202)) {
            return OnlineUiAction::refresh_public_rooms;
        }
        if (inside(x, y, 1195, 288, 1480, 332)) {
            text_field_ = TextField::direct_endpoint;
            return OnlineUiAction::none;
        }
        if (inside(x, y, 1090, 350, 1480, 408)) {
            return OnlineUiAction::connect_direct;
        }
        if (inside(x, y, 540, 748, 750, 820)) {
            online_open_create_lobby(model);
            text_field_ = TextField::lobby_name;
            return OnlineUiAction::none;
        }
        if (inside(x, y, 780, 748, 1020, 820) && model.selected_room) {
            return OnlineUiAction::connect_selected_room;
        }
        if (inside(x, y, 60, 770, 190, 865)) {
            online_open_home(model);
            return OnlineUiAction::none;
        }
        return OnlineUiAction::none;
    }

    if (model.screen == OnlineLobbyScreen::create_lobby) {
        if (inside(x, y, 565, 245, 995, 295)) {
            text_field_ = TextField::lobby_name;
            return OnlineUiAction::none;
        }
        if (inside(x, y, 565, 320, 995, 370)) {
            cycle_max_players(model, 1);
            return OnlineUiAction::none;
        }
        if (inside(x, y, 565, 395, 995, 445)) {
            cycle_privacy(model);
            return OnlineUiAction::none;
        }
        if (inside(x, y, 565, 470, 995, 520) &&
            model.create_room.privacy == OnlineRoomPrivacy::private_room) {
            text_field_ = TextField::password;
            return OnlineUiAction::none;
        }
        if (inside(x, y, 655, 600, 945, 684)) {
            return OnlineUiAction::host_room;
        }
        if (inside(x, y, 60, 770, 190, 865)) {
            online_open_public_servers(model);
            return OnlineUiAction::none;
        }
        return OnlineUiAction::none;
    }

    if (model.screen == OnlineLobbyScreen::find_match) {
        if (inside(x, y, 1040, 111, 1094, 165)) {
            online_open_home(model);
            return OnlineUiAction::none;
        }
        if (inside(x, y, 670, 360, 930, 450)) {
            return OnlineUiAction::begin_matchmaking;
        }
        if (inside(x, y, 595, 495, 790, 557)) {
            model.queue = OnlineMatchQueue::casual;
            return OnlineUiAction::none;
        }
        if (inside(x, y, 810, 495, 1005, 557)) {
            model.queue = OnlineMatchQueue::ranked;
            return OnlineUiAction::none;
        }
        if (inside(x, y, 560, 645, 1040, 693)) {
            cycle_region(model, 1);
            return OnlineUiAction::none;
        }
        return OnlineUiAction::none;
    }

    if (model.screen == OnlineLobbyScreen::searching ||
        model.screen == OnlineLobbyScreen::connecting) {
        if (inside(x, y, 705, 700, 895, 754)) {
            return OnlineUiAction::cancel_matchmaking;
        }
        return OnlineUiAction::none;
    }

    if (model.screen == OnlineLobbyScreen::lobby) {
        if (inside(x, y, 1450, 90, 1504, 144)) {
            return OnlineUiAction::leave_lobby;
        }
        if (inside(x, y, 455, 635, 675, 700)) {
            online_set_ready(model, !model.ready);
            return OnlineUiAction::ready_changed;
        }
        if (inside(x, y, 880, 565, 1110, 627) &&
            model.local_player_is_host && model.ready && model.remote_ready &&
            online_game_revision_matches(model)) {
            return OnlineUiAction::start_lobby_game;
        }
        if (inside(x, y, 1180, 650, 1520, 698)) {
            text_field_ = TextField::chat;
            return OnlineUiAction::none;
        }
    }

    return OnlineUiAction::none;
}

OnlineUiAction OnlineUi::key_down(
    WPARAM key,
    OnlineLobbyModel& model) {
    if (model.screen == OnlineLobbyScreen::home && name_keyboard_open_) {
        if (key == VK_ESCAPE) {
            name_keyboard_open_ = false;
            return OnlineUiAction::none;
        }
        if (key == VK_LEFT) {
            move_name_keyboard(-1, 0);
            return OnlineUiAction::none;
        }
        if (key == VK_RIGHT) {
            move_name_keyboard(1, 0);
            return OnlineUiAction::none;
        }
        if (key == VK_UP) {
            move_name_keyboard(0, -1);
            return OnlineUiAction::none;
        }
        if (key == VK_DOWN) {
            move_name_keyboard(0, 1);
            return OnlineUiAction::none;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            activate_name_keyboard_key(model);
            return OnlineUiAction::none;
        }
        if (key == VK_BACK) {
            if (!model.player_name.empty()) model.player_name.pop_back();
            return OnlineUiAction::none;
        }
        return OnlineUiAction::none;
    }

    if (key == VK_ESCAPE) {
        text_field_ = TextField::none;
        switch (model.screen) {
            case OnlineLobbyScreen::home:
                return OnlineUiAction::back_to_launcher;
            case OnlineLobbyScreen::public_servers:
            case OnlineLobbyScreen::find_match:
                online_open_home(model);
                return OnlineUiAction::none;
            case OnlineLobbyScreen::create_lobby:
                online_open_public_servers(model);
                return OnlineUiAction::none;
            case OnlineLobbyScreen::searching:
            case OnlineLobbyScreen::connecting:
                return OnlineUiAction::cancel_matchmaking;
            case OnlineLobbyScreen::lobby:
                return OnlineUiAction::leave_lobby;
        }
    }

    if (model.screen == OnlineLobbyScreen::home) {
        if (key == VK_LEFT) cycle_region(model, -1);
        if (key == VK_RIGHT) cycle_region(model, 1);
    } else if (model.screen == OnlineLobbyScreen::public_servers) {
        if (key == 'D') {
            text_field_ = TextField::direct_endpoint;
            return OnlineUiAction::none;
        }
        if (key == VK_RETURN && text_field_ == TextField::direct_endpoint) {
            return OnlineUiAction::connect_direct;
        }
    } else if (model.screen == OnlineLobbyScreen::find_match) {
        if (key == VK_LEFT || key == VK_RIGHT) toggle_queue(model);
        if (key == VK_RETURN) return OnlineUiAction::begin_matchmaking;
    } else if (model.screen == OnlineLobbyScreen::searching ||
               model.screen == OnlineLobbyScreen::connecting) {
        if (key == VK_RETURN) return OnlineUiAction::cancel_matchmaking;
    } else if (model.screen == OnlineLobbyScreen::lobby) {
        if (text_field_ == TextField::chat && key == VK_RETURN) {
            return chat_draft_.empty()
                ? OnlineUiAction::none
                : OnlineUiAction::send_chat_message;
        }
        if (key == 'R') {
            online_set_ready(model, !model.ready);
            return OnlineUiAction::ready_changed;
        }
        if (key == VK_RETURN && model.local_player_is_host &&
            model.ready && model.remote_ready &&
            online_game_revision_matches(model)) {
            return OnlineUiAction::start_lobby_game;
        }
    }

    return OnlineUiAction::none;
}

void OnlineUi::char_input(
    wchar_t ch,
    OnlineLobbyModel& model) {
    if (model.screen == OnlineLobbyScreen::home && name_keyboard_open_) {
        if (ch == L'\b') {
            if (!model.player_name.empty()) model.player_name.pop_back();
            return;
        }
        if (ch >= 32 && ch <= 126 && model.player_name.size() < 24u) {
            model.player_name.push_back(static_cast<char>(ch));
        }
        return;
    }

    auto edit_string = [ch](std::string& value, std::size_t limit) {
        if (ch == L'\b') {
            if (!value.empty()) value.pop_back();
            return;
        }
        if (ch < 32 || ch > 126 || value.size() >= limit) return;
        value.push_back(static_cast<char>(ch));
    };

    switch (text_field_) {
        case TextField::player_name:
            edit_string(model.player_name, 24u);
            break;
        case TextField::lobby_name:
            edit_string(model.create_room.name, 40u);
            break;
        case TextField::password:
            edit_string(model.create_room.password, 64u);
            break;
        case TextField::direct_endpoint:
            edit_string(model.direct_connect_endpoint, 48u);
            break;
        case TextField::chat:
            if (ch == L'\b') {
                if (!chat_draft_.empty()) chat_draft_.pop_back();
            } else if (ch >= 32 && ch < 0xD800 && chat_draft_.size() < 120u) {
                chat_draft_.push_back(ch);
            }
            break;
        case TextField::none:
            break;
    }
}

} // namespace jojo::win32
#endif
