#include "achievement_overlay.h"
#include "image_decode.h"
#include "game_settings.h"
#include "overlay_canvas.h"
#include "screen_overlay.h"
#include "touch_controls.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace sfr {
void AchievementPageNav::move(int by) {
    if (count <= 0) {
        selected = first = 0;
        return;
    }
    selected = std::clamp(selected + by, 0, count - 1);
    if (selected < first) first = selected;
    if (selected >= first + visible) first = selected - visible + 1;
    first = std::clamp(first, 0, (std::max)(0, count - visible));
}

std::vector<size_t> achievement_page_order(const TitleData& title, const UnlockedAchievements& unlocked) {
    std::vector<size_t> order(title.achievements.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    const auto when = [&](size_t i) -> int64_t {
        const auto* u = unlocked.find(title.achievements[i].id);
        return u ? u->unlocked : -1;
    };
    // Position in the unlock list breaks ties of the same second.
    const auto rank = [&](size_t i) -> size_t {
        for (size_t k = 0; k < unlocked.list.size(); ++k)
            if (unlocked.list[k].id == title.achievements[i].id) return k;
        return 0;
    };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const int64_t wa = when(a), wb = when(b);
        if ((wa >= 0) != (wb >= 0)) return wa >= 0;
        if (wa < 0) return false;
        if (wa != wb) return wa > wb;
        return rank(a) > rank(b);
    });
    return order;
}

const AchievementTexts& achievement_texts(uint32_t xbox_language) {
    static const AchievementTexts texts[] = {
        {"Achievement unlocked", "Achievements", "Secret achievement", "Keep playing to unlock this achievement.", "Locked",
         "Close", "Select", "Page"},
        {"実績のロックが解除されました", "実績", "シークレット実績", "プレイを続けてこの実績を解除しよう。", "未解除", "閉じる",
         "選択", "ページ"},
        {"Erfolg freigeschaltet", "Erfolge", "Geheimer Erfolg", "Spiel weiter, um diesen Erfolg freizuschalten.",
         "Gesperrt", "Schließen", "Auswählen", "Seite"},
        {"Succès déverrouillé", "Succès", "Succès secret", "Continuez à jouer pour déverrouiller ce succès.", "Verrouillé",
         "Fermer", "Choisir", "Page"},
        {"Logro desbloqueado", "Logros", "Logro secreto", "Sigue jugando para desbloquear este logro.", "Bloqueado",
         "Cerrar", "Elegir", "Página"},
        {"Obiettivo sbloccato", "Obiettivi", "Obiettivo segreto", "Continua a giocare per sbloccare questo obiettivo.",
         "Bloccato", "Chiudi", "Scegli", "Pagina"},
    };
    return texts[xbox_language >= 1 && xbox_language <= 6 ? xbox_language - 1 : 0];
}

namespace {
constexpr uint16_t pad_up = 0x0001, pad_down = 0x0002, pad_left = 0x0004, pad_right = 0x0008;
constexpr uint16_t pad_back = 0x0020, pad_l3 = 0x0040, pad_r3 = 0x0080, pad_lb = 0x0100, pad_rb = 0x0200;
constexpr uint16_t pad_a = 0x1000, pad_b = 0x2000, pad_y = 0x8000;
constexpr int key_f1 = 0x70, key_escape = 0x1B, key_backspace = 0x08, key_up = 0x26, key_down = 0x28, key_page_up = 0x21,
              key_page_down = 0x22, key_home = 0x24, key_end = 0x23;

std::string local_date(int64_t seconds) {
    const std::time_t t = std::time_t(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d", &local);
    return text;
}

// The look: the game's menus are deep blue panels with light cyan edges and
// white text, with yellow for what is picked; the console's notification a
// dark grey bar.
const OverlayColor panel_top = overlay_rgb(0x0E4AAE, 0.97f), panel_bottom = overlay_rgb(0x061E5C, 0.97f);
const OverlayColor panel_edge = overlay_rgb(0x5FD2FF), header_top = overlay_rgb(0x2C8CF0), header_bottom = overlay_rgb(0x0F55BE);
const OverlayColor row_fill = overlay_rgb(0xFFFFFF, 0.07f), row_picked_top = overlay_rgb(0x34B8F5),
                   row_picked_bottom = overlay_rgb(0x0A6FC4), sonic_yellow = overlay_rgb(0xFFD21E);
const OverlayColor text_white = overlay_rgb(0xFFFFFF), text_soft = overlay_rgb(0xCFE3FF), text_dim = overlay_rgb(0x8FA9D6);
}

struct AchievementOverlay::Impl {
    mutable std::mutex mutex;
    bool configured = false;
    std::optional<TitleData> title;
    std::map<uint32_t, DecodedImage> icons;
    std::filesystem::path file;
    UnlockedAchievements unlocked;
    uint32_t language = 1;
    bool notifications = true;
    Host host;
    ToastQueue toasts;
    std::optional<ToastFrame> toast_frame;

    bool open = false, closing = false;
    AchievementPageNav nav;
    // The tabs: 0 the achievements, 1 the port's settings (game_settings.h).
    int tab = 0;
    int setting = 0;  // the selected one, in the order of settings_order()
    std::map<std::string, int> section_first;  // each section's first row shown
    // The title screen's note that L3 + R3 opens this page (the settings'
    // menu hint): shown while it waits for START and a few seconds after.
    bool hint_shown = false;
    double hint_until = 0;
    std::shared_ptr<const OverlayImage> hint_image;
    float hint_scale = 0;
    std::vector<size_t> order;
    std::vector<AchievementProgress> progress;
    double progress_read_at = -1;

    uint16_t previous_buttons = 0;
    std::map<int, bool> previous_keys;
    int held_direction = 0;
    double held_since = 0, repeated_at = 0;

    OverlayFonts fonts;
    bool fonts_loaded = false;
    uint64_t next_image = 1;
    std::shared_ptr<const OverlayImage> toast_image, page_image, dim_image;
    uint16_t toast_image_id = 0;
    float toast_image_scale = 0;
    std::string page_key;
    float scale = 0;  // pictures are drawn for this many pixels per 720p pixel
    float view_width = 1280, view_height = 720;

    // Touch: fingers down and where they started.
    struct Finger { float x0, y0, x, y; double since; };
    std::map<int64_t, Finger> fingers;
    size_t most_fingers = 0;
    double scroll_anchor = 0;

    const TitleAchievement* achievement(uint16_t id) const {
        if (!title) return nullptr;
        for (const auto& a : title->achievements)
            if (a.id == id) return &a;
        return nullptr;
    }
    const DecodedImage* icon(uint32_t image_id) {
        if (!title) return nullptr;
        if (auto found = icons.find(image_id); found != icons.end()) return found->second ? &found->second : nullptr;
        DecodedImage decoded;
        if (const auto png = title->images.find(image_id); png != title->images.end()) decoded = decode_png(png->second);
        auto& stored = icons[image_id] = std::move(decoded);
        return stored ? &stored : nullptr;
    }
    void ensure_fonts() {
        if (fonts_loaded) return;
        fonts_loaded = true;
        fonts.load({}, {}, {});
        if (!fonts.any()) std::cerr << "ACHIEVEMENT_OVERLAY fonts=none (texts are not drawn)\n";
    }
    std::shared_ptr<OverlayImage> image_from(OverlayCanvas& canvas) {
        auto image = std::make_shared<OverlayImage>();
        image->width = canvas.width();
        image->height = canvas.height();
        image->rgba = canvas.take();
        image->id = next_image++;
        return image;
    }

    void refresh_progress(double now) {
        progress_read_at = now;
        progress.clear();
        if (!host.read) return;
        const auto load32 = [&](uint32_t address) -> std::optional<uint32_t> {
            uint8_t bytes[4];
            if (!host.read(address, bytes)) return std::nullopt;
            return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
        };
        const auto record = achievement_record_address(load32);
        if (!record) return;
        std::vector<uint8_t> bytes(achievement_record_size);
        if (!host.read(*record, bytes)) return;
        progress = read_achievement_progress(bytes);
    }

    void set_open(bool want, double now) {
        if (want == open && !closing) return;
        if (want) {
            if (!title) return;
            open = true;
            closing = false;
            order = achievement_page_order(*title, unlocked);
            nav = {};
            nav.count = int(order.size());
            refresh_progress(now);
            if (host.page_shown) host.page_shown(true);
            std::cerr << "ACHIEVEMENT_PAGE open count=" << order.size() << " unlocked=" << unlocked.list.size()
                      << " progress=" << progress.size() << '\n';
        } else if (open) {
            // Closes now; the game gets its input back once every button and
            // key is up, so the B that closed it does not reach the game.
            open = false;
            closing = true;
            std::cerr << "ACHIEVEMENT_PAGE close\n";
        }
    }

    // ------------------------------------------------------------ drawing
    std::shared_ptr<const OverlayImage> compose_toast(const TitleAchievement& a, float s) {
        ensure_fonts();
        const float w = 520, h = 84;
        OverlayCanvas canvas(uint32_t(std::ceil(w * s)), uint32_t(std::ceil(h * s)));
        // The bar: dark grey, rounded to a pill, a light rim.
        canvas.fill_round_rect(0, 0, w * s, h * s, h * s / 2, overlay_rgb(0x3C3C3C, 0.94f), overlay_rgb(0x151515, 0.94f));
        canvas.stroke_round_rect(0, 0, w * s, h * s, h * s / 2, 2 * s, overlay_rgb(0x6A6A6A, 0.9f));
        canvas.stroke_round_rect(2 * s, 2 * s, (w - 4) * s, (h - 4) * s, (h - 4) * s / 2, 1 * s, overlay_rgb(0xFFFFFF, 0.08f));
        // The icon in a ring.
        const float icon = 60, ix = 12, iy = (h - icon) / 2;
        canvas.fill_circle((ix + icon / 2) * s, (iy + icon / 2) * s, (icon / 2 + 3) * s, overlay_rgb(0x8BD43A));
        canvas.fill_circle((ix + icon / 2) * s, (iy + icon / 2) * s, (icon / 2 + 1) * s, overlay_rgb(0x202020));
        if (const DecodedImage* picture = icon_ptr(a.image_id))
            canvas.draw_image(*picture, (ix + 1) * s, (iy + 1) * s, (icon - 2) * s, (icon - 2) * s, (icon / 2 - 1) * s);
        const AchievementTexts& t = achievement_texts(language);
        const float tx = (ix + icon + 16) * s, room = (w - ix - icon - 16 - 34) * s;
        canvas.text(fonts, t.unlocked_toast, tx, 33 * s, 19 * s, overlay_rgb(0xD8D8D8), OverlayFonts::regular, room);
        char points[16];
        std::snprintf(points, sizeof points, "%uG", unsigned(a.gamerscore));
        const float pw = canvas.text(fonts, points, tx, 63 * s, 24 * s, overlay_rgb(0xFFFFFF), OverlayFonts::bold);
        canvas.text(fonts, "-", tx + pw + 8 * s, 63 * s, 24 * s, overlay_rgb(0xB0B0B0), OverlayFonts::regular);
        canvas.text(fonts, a.name, tx + pw + 22 * s, 63 * s, 24 * s, overlay_rgb(0xFFFFFF), OverlayFonts::bold,
                    room - pw - 22 * s);
        return image_from(canvas);
    }
    const DecodedImage* icon_ptr(uint32_t id) { return icon(id); }

    // A controller button as the game's touch controls draw them.
    void button_glyph(OverlayCanvas& canvas, float x, float baseline, float s, const char* letter, OverlayColor color) {
        const float r = 13 * s;
        canvas.fill_circle(x + r, baseline - 8 * s, r, color);
        canvas.fill_circle(x + r, baseline - 8 * s, r - 2 * s, overlay_rgb(0x0A1F4D));
        const float lw = fonts.measure(letter, 15 * s, OverlayFonts::bold);
        canvas.text(fonts, letter, x + r - lw / 2, baseline - 2.5f * s, 15 * s, color, OverlayFonts::bold);
    }

    // The settings in the page's order: section by section.
    static const std::vector<game_settings::Id>& settings_order() {
        static const std::vector<game_settings::Id> order = [] {
            std::vector<game_settings::Id> out;
            for (const char* section : game_settings::sections())
                for (const auto id : game_settings::in_section(section)) out.push_back(id);
            return out;
        }();
        return order;
    }

    // The Settings tab: the sections down the left, the selected one's
    // settings on the right (name, what it does, when it applies, its value
    // between arrows).
    void compose_settings(OverlayCanvas& canvas, float s, float W, float H) {
        namespace gs = game_settings;
        const auto& order = settings_order();
        const gs::Id chosen_id = order[size_t(std::clamp(setting, 0, int(order.size()) - 1))];
        const gs::Setting& chosen = gs::settings()[size_t(chosen_id)];
        if (gs::restart_pending()) {
            const char* note = "Restart to apply:  Y closes the game";
            const float nw = fonts.measure(note, 20 * s, OverlayFonts::bold);
            canvas.text(fonts, note, (W - 36) * s - nw, 56 * s, 20 * s, sonic_yellow, OverlayFonts::bold);
        }
        // Sections.
        const float top = 96, left_w = 230;
        float y = top;
        for (const char* section : gs::sections()) {
            const bool here = std::strcmp(section, chosen.section) == 0;
            if (here) canvas.fill_round_rect(22 * s, y * s, left_w * s, 52 * s, 12 * s, row_picked_top, row_picked_bottom);
            else canvas.fill_round_rect(22 * s, y * s, left_w * s, 52 * s, 12 * s, row_fill);
            if (here) canvas.fill_round_rect(30 * s, (y + 12) * s, 6 * s, 28 * s, 3 * s, sonic_yellow);
            canvas.text(fonts, section, 48 * s, (y + 34) * s, 22 * s, here ? text_white : text_soft, OverlayFonts::bold,
                        (left_w - 34) * s);
            y += 60;
        }
        // The section's settings: as many rows as fit above the buttons'
        // line, scrolled to keep the selected one shown.
        const float x = 22 + left_w + 14, w = W - x - 22 - 16, row_h = 70, gap = 6;
        const auto items = gs::in_section(chosen.section);
        const int fit = int((H - 52 - top) / (row_h + gap));
        int picked_at = 0;
        for (size_t k = 0; k < items.size(); ++k)
            if (items[k] == chosen_id) picked_at = int(k);
        int& first_row = section_first[chosen.section];
        first_row = std::clamp(first_row, (std::max)(0, picked_at - fit + 1), picked_at);
        first_row = std::clamp(first_row, 0, (std::max)(0, int(items.size()) - fit));
        if (int(items.size()) > fit) {
            const float track_h = fit * (row_h + gap) - gap, bx = W - 30;
            canvas.fill_round_rect(bx * s, top * s, 8 * s, track_h * s, 4 * s, overlay_rgb(0x02103A, 0.8f));
            const float thumb_h = (std::max)(30.0f, track_h * float(fit) / float(items.size()));
            const float thumb_y = top + (track_h - thumb_h) * float(first_row) / float(int(items.size()) - fit);
            canvas.fill_round_rect(bx * s, thumb_y * s, 8 * s, thumb_h * s, 4 * s, panel_edge);
        }
        y = top;
        for (int k = first_row; k < int(items.size()) && k < first_row + fit; ++k) {
            const auto id = items[size_t(k)];
            const gs::Setting& item = gs::settings()[size_t(id)];
            const bool picked = id == chosen_id;
            if (picked) {
                canvas.fill_round_rect(x * s, y * s, w * s, row_h * s, 12 * s, row_picked_top, row_picked_bottom);
                canvas.stroke_round_rect(x * s, y * s, w * s, row_h * s, 12 * s, 3 * s, sonic_yellow);
            } else {
                canvas.fill_round_rect(x * s, y * s, w * s, row_h * s, 12 * s, row_fill);
            }
            const bool locked = item.offline_only && gs::online();
            const int at = gs::index(id);
            const char* label = locked ? "Off online" : item.labels[size_t(at)];
            const float right = x + w - 20, value_left = right - 230, centre = (value_left + right) / 2;
            const float aw = fonts.measure(">", 22 * s, OverlayFonts::bold);
            // The value between the arrows, smaller when it would not fit.
            float value_size = 22;
            while (value_size > 14 && fonts.measure(label, value_size * s, OverlayFonts::bold) > (230 - 2 * aw / s - 16) * s)
                value_size -= 1;
            const float lw = fonts.measure(label, value_size * s, OverlayFonts::bold);
            const OverlayColor arrows = picked && !locked ? sonic_yellow : text_dim;
            canvas.text(fonts, "<", value_left * s, (y + 32) * s, 22 * s, arrows, OverlayFonts::bold);
            canvas.text(fonts, ">", right * s - aw, (y + 32) * s, 22 * s, arrows, OverlayFonts::bold);
            canvas.text(fonts, label, centre * s - lw / 2, (y + 32) * s, value_size * s,
                        locked ? text_dim : at != item.fallback ? sonic_yellow : text_white, OverlayFonts::bold);
            const char* when = item.applies == gs::Applies::restart     ? "Restart"
                               : item.applies == gs::Applies::next_race ? "Next race"
                               : item.applies == gs::Applies::menus     ? "In menus"
                                                                        : "";
            if (*when) {
                const float ww = fonts.measure(when, 14 * s, OverlayFonts::regular);
                canvas.text(fonts, when, centre * s - ww / 2, (y + 56) * s, 14 * s, text_dim);
            }
            const float tx = x + 18, room = value_left - 16 - tx;
            canvas.text(fonts, item.name, tx * s, (y + 28) * s, 21 * s, picked ? text_white : text_soft, OverlayFonts::bold,
                        room * s);
            const auto lines = wrap_overlay_text(fonts, item.description, 15 * s, OverlayFonts::regular, room * s, 2);
            for (size_t l = 0; l < lines.size(); ++l)
                canvas.text(fonts, lines[l], tx * s, (y + 49 + 17 * float(l)) * s, 15 * s, picked ? text_soft : text_dim);
            y += row_h + gap;
        }
        // What the buttons do.
        const float fy = H - 18;
        float fx = 34;
        button_glyph(canvas, fx * s, fy * s, s, "B", overlay_rgb(0xF04040));
        fx += 34;
        fx += canvas.text(fonts, achievement_texts(language).close, fx * s, fy * s, 19 * s, text_white) / s + 24;
        button_glyph(canvas, fx * s, fy * s, s, "A", overlay_rgb(0x60D040));
        fx += 34;
        fx += canvas.text(fonts, "Change (or left / right)", fx * s, fy * s, 19 * s, text_white) / s + 24;
        for (const char* shoulder : {"LB", "RB"}) {
            canvas.fill_round_rect(fx * s, (fy - 20) * s, 36 * s, 24 * s, 8 * s, overlay_rgb(0x0A1F4D));
            const float sw = fonts.measure(shoulder, 14 * s, OverlayFonts::bold);
            canvas.text(fonts, shoulder, (fx + 18) * s - sw / 2, (fy - 3) * s, 14 * s, overlay_rgb(0xFFA040), OverlayFonts::bold);
            fx += 40;
        }
        canvas.text(fonts, "Tabs", (fx + 4) * s, fy * s, 19 * s, text_white);
        const std::string saved = "Saved to settings.env";
        const float kw = fonts.measure(saved, 17 * s, OverlayFonts::regular);
        canvas.text(fonts, saved, (W - 36) * s - kw, fy * s, 17 * s, text_dim);
    }

    std::shared_ptr<const OverlayImage> compose_page(float s) {
        ensure_fonts();
        const float W = 1040, H = 620, rows_top = 96, row_h = 88, row_gap = 8;
        OverlayCanvas canvas(uint32_t(std::ceil(W * s)), uint32_t(std::ceil(H * s)));
        const AchievementTexts& t = achievement_texts(language);
        // The panel and its edges.
        canvas.fill_round_rect(0, 0, W * s, H * s, 22 * s, panel_top, panel_bottom);
        canvas.stroke_round_rect(0, 0, W * s, H * s, 22 * s, 4 * s, panel_edge);
        canvas.stroke_round_rect(6 * s, 6 * s, (W - 12) * s, (H - 12) * s, 17 * s, 1.5f * s, overlay_rgb(0xFFFFFF, 0.25f));
        // The header band with the title and the totals.
        canvas.fill_round_rect(14 * s, 14 * s, (W - 28) * s, 66 * s, 14 * s, header_top, header_bottom);
        canvas.fill_round_rect(26 * s, 26 * s, 10 * s, 42 * s, 4 * s, sonic_yellow);
        // The tabs, the one shown in white with a yellow bar under it.
        {
            float tx = 50;
            const char* names[2] = {t.title, "Settings"};
            for (int k = 0; k < 2; ++k) {
                const float tw = canvas.text(fonts, names[k], tx * s, 60 * s, 30 * s, k == tab ? text_white : text_dim,
                                             OverlayFonts::bold, 300 * s) / s;
                if (k == tab) canvas.fill_round_rect(tx * s, 66 * s, tw * s, 4 * s, 2 * s, sonic_yellow);
                tx += tw + 30;
            }
        }
        if (tab == 1) {
            compose_settings(canvas, s, W, H);
            return image_from(canvas);
        }
        int got = 0, score = 0, total = 0;
        for (const auto& a : title->achievements) {
            total += a.gamerscore;
            if (unlocked.find(a.id)) {
                ++got;
                score += a.gamerscore;
            }
        }
        char totals[64];
        std::snprintf(totals, sizeof totals, "%d / %d", got, int(title->achievements.size()));
        char points[64];
        std::snprintf(points, sizeof points, "%d / %d G", score, total);
        const float pw = fonts.measure(points, 26 * s, OverlayFonts::bold);
        canvas.text(fonts, points, (W - 36) * s - pw, 58 * s, 26 * s, sonic_yellow, OverlayFonts::bold);
        const float tw = fonts.measure(totals, 26 * s, OverlayFonts::bold);
        canvas.text(fonts, totals, (W - 36 - 40) * s - pw - tw, 58 * s, 26 * s, text_white, OverlayFonts::bold);

        // The rows.
        for (int k = 0; k < nav.visible; ++k) {
            const int index = nav.first + k;
            if (index >= nav.count) break;
            const TitleAchievement& a = title->achievements[order[size_t(index)]];
            const UnlockedAchievement* u = unlocked.find(a.id);
            const bool picked = index == nav.selected;
            const bool hidden = !u && a.secret();
            const float y = rows_top + float(k) * (row_h + row_gap), x = 22, w = W - 44 - 18;
            if (picked) {
                canvas.fill_round_rect(x * s, y * s, w * s, row_h * s, 14 * s, row_picked_top, row_picked_bottom);
                canvas.stroke_round_rect(x * s, y * s, w * s, row_h * s, 14 * s, 3 * s, sonic_yellow);
            } else {
                canvas.fill_round_rect(x * s, y * s, w * s, row_h * s, 14 * s, row_fill);
            }
            // Icon (grey while locked; a question mark for a secret one).
            const float icon = 68, ix = x + 12, iy = y + (row_h - icon) / 2;
            canvas.fill_round_rect((ix - 2) * s, (iy - 2) * s, (icon + 4) * s, (icon + 4) * s, 10 * s, overlay_rgb(0x03123A, 0.9f));
            if (hidden) {
                const float qw = fonts.measure("?", 44 * s, OverlayFonts::bold);
                canvas.text(fonts, "?", (ix + icon / 2) * s - qw / 2, (iy + icon / 2 + 16) * s, 44 * s, text_dim, OverlayFonts::bold);
            } else if (const DecodedImage* picture = icon_ptr(a.image_id)) {
                canvas.draw_image(*picture, ix * s, iy * s, icon * s, icon * s, 8 * s, 1.0f, !u);
            }
            // Gamerscore and the date or progress on the right.
            const float right = x + w - 18;
            char gs[16];
            std::snprintf(gs, sizeof gs, "%u G", unsigned(a.gamerscore));
            const float gw = fonts.measure(gs, 26 * s, OverlayFonts::bold);
            canvas.text(fonts, gs, right * s - gw, (y + 36) * s, 26 * s, u ? sonic_yellow : text_soft, OverlayFonts::bold);
            const AchievementProgress* p = find_progress(progress, a.id);
            float text_right = right - 200;
            if (u) {
                const std::string date = local_date(u->unlocked);
                const float dw = fonts.measure(date, 18 * s, OverlayFonts::regular);
                canvas.text(fonts, date, right * s - dw, (y + 70) * s, 18 * s, text_white);
            } else if (p) {
                // A bar with "n / target".
                const float bw = 170, bx = right - bw, by = y + 56;
                canvas.fill_round_rect(bx * s, by * s, bw * s, 12 * s, 6 * s, overlay_rgb(0x02103A, 0.95f));
                const float k2 = p->target ? float(p->value) / float(p->target) : 0.0f;
                if (k2 > 0)
                    canvas.fill_round_rect(bx * s, by * s, (std::max)(12.0f, bw * k2) * s, 12 * s, 6 * s, sonic_yellow,
                                           overlay_rgb(0xF0A000));
                char count[48];
                std::snprintf(count, sizeof count, "%u / %u", unsigned(p->value), unsigned(p->target));
                const float cw = fonts.measure(count, 17 * s, OverlayFonts::bold);
                canvas.text(fonts, count, (bx - 10) * s - cw, (y + 68) * s, 17 * s, text_white, OverlayFonts::bold);
                text_right = bx - 10 - cw / s - 14;
            } else {
                const float lw = fonts.measure(t.locked, 18 * s, OverlayFonts::regular);
                canvas.text(fonts, t.locked, right * s - lw, (y + 70) * s, 18 * s, text_dim);
            }
            // Name and description.
            const float tx = ix + icon + 18;
            const float name_room = (right - 110 - tx) * s;
            canvas.text(fonts, hidden ? t.secret_name : a.name, tx * s, (y + 32) * s, 24 * s, u || picked ? text_white : text_soft,
                        OverlayFonts::bold, name_room);
            const std::string& body = hidden ? std::string(t.secret_text) : u ? a.description : a.locked_description;
            const auto lines = wrap_overlay_text(fonts, body, 17 * s, OverlayFonts::regular, (text_right - tx) * s, 2);
            for (size_t l = 0; l < lines.size(); ++l)
                canvas.text(fonts, lines[l], tx * s, (y + 56 + 21 * float(l)) * s, 17 * s, u || picked ? text_soft : text_dim);
        }
        // Scroll bar.
        if (nav.count > nav.visible) {
            const float track_y = rows_top, track_h = nav.visible * (row_h + row_gap) - row_gap, bx = W - 30;
            canvas.fill_round_rect(bx * s, track_y * s, 8 * s, track_h * s, 4 * s, overlay_rgb(0x02103A, 0.8f));
            const float thumb_h = (std::max)(30.0f, track_h * float(nav.visible) / float(nav.count));
            const float thumb_y = track_y + (track_h - thumb_h) * float(nav.first) / float(nav.count - nav.visible);
            canvas.fill_round_rect(bx * s, thumb_y * s, 8 * s, thumb_h * s, 4 * s, panel_edge);
        }
        // What the buttons do, at the bottom.
        const float fy = H - 18;
        float fx = 34;
        button_glyph(canvas, fx * s, fy * s, s, "B", overlay_rgb(0xF04040));
        fx += 34;
        fx += canvas.text(fonts, t.close, fx * s, fy * s, 19 * s, text_white) / s + 28;
        // A D-pad: a cross in a dark disc.
        canvas.fill_circle((fx + 13) * s, (fy - 8) * s, 13 * s, overlay_rgb(0x0A1F4D));
        canvas.fill_round_rect((fx + 10) * s, (fy - 17) * s, 6 * s, 18 * s, 1.5f * s, text_white);
        canvas.fill_round_rect((fx + 4) * s, (fy - 11) * s, 18 * s, 6 * s, 1.5f * s, text_white);
        fx += 34;
        fx += canvas.text(fonts, t.select, fx * s, fy * s, 19 * s, text_white) / s + 28;
        for (const char* shoulder : {"LB", "RB"}) {
            canvas.fill_round_rect(fx * s, (fy - 20) * s, 36 * s, 24 * s, 8 * s, overlay_rgb(0x0A1F4D));
            const float lw = fonts.measure(shoulder, 14 * s, OverlayFonts::bold);
            canvas.text(fonts, shoulder, (fx + 18) * s - lw / 2, (fy - 3) * s, 14 * s, overlay_rgb(0xFFA040), OverlayFonts::bold);
            fx += 40;
        }
        fx += 4;
        canvas.text(fonts, "Tabs", fx * s, fy * s, 19 * s, text_white);
        // How it opens and closes, on the right.
        const std::string keys = "L3 + R3  /  F1";
        const float kw = fonts.measure(keys, 17 * s, OverlayFonts::regular);
        canvas.text(fonts, keys, (W - 36) * s - kw, fy * s, 17 * s, text_dim);
        return image_from(canvas);
    }

    std::shared_ptr<const OverlayImage> compose_hint(float s) {
        ensure_fonts();
        const char* text = "Recompiled menu: settings and achievements";
        const float tw = fonts.measure(text, 19 * s, OverlayFonts::regular) / s;
        const float w = 30 + 92 + 12 + tw + 24, h = 46;
        OverlayCanvas canvas(uint32_t(std::ceil(w * s)), uint32_t(std::ceil(h * s)));
        canvas.fill_round_rect(0, 0, w * s, h * s, h * s / 2, overlay_rgb(0x0E4AAE, 0.92f), overlay_rgb(0x061E5C, 0.92f));
        canvas.stroke_round_rect(0, 0, w * s, h * s, h * s / 2, 2 * s, panel_edge);
        float x = 18;
        for (const char* stick : {"L3", "R3"}) {
            canvas.fill_round_rect(x * s, 11 * s, 36 * s, 24 * s, 8 * s, overlay_rgb(0x0A1F4D));
            const float sw = fonts.measure(stick, 14 * s, OverlayFonts::bold);
            canvas.text(fonts, stick, (x + 18) * s - sw / 2, 28 * s, 14 * s, sonic_yellow, OverlayFonts::bold);
            x += 46;
        }
        canvas.text(fonts, text, (x + 4) * s, 30 * s, 19 * s, text_white);
        return image_from(canvas);
    }

    std::vector<ScreenLayer> layers(float vw, float vh) {
        std::vector<ScreenLayer> out;
        if (vw <= 0 || vh <= 0 || !title) return out;
        view_width = vw;
        view_height = vh;
        const float u = vh / 720.0f;
        const float wanted = std::clamp(std::round(u * 4.0f) / 4.0f, 0.5f, 2.0f);
        if (wanted > scale) scale = wanted;
        if (open) {
            if (!dim_image) {
                auto dim = std::make_shared<OverlayImage>();
                dim->width = dim->height = 1;
                dim->rgba = {0, 0, 0, 150};
                dim->id = next_image++;
                dim_image = dim;
            }
            out.push_back({dim_image, 0, 0, vw, vh, 1.0f});
            std::ostringstream key;
            key << scale << ':' << tab << ':' << nav.first << ':' << nav.selected << ':' << unlocked.list.size() << ':'
                << setting << ':' << game_settings::generation() << ':';
            for (const auto& p : progress) key << p.value << ',';
            if (!page_image || key.str() != page_key) {
                page_key = key.str();
                page_image = compose_page(scale);
            }
            const float w = 1040 * u, h = 620 * u;
            out.push_back({page_image, (vw - w) / 2, (vh - h) / 2, w, h, 1.0f});
        }
        if (hint_shown && !open) {
            if (!hint_image || hint_scale != scale) {
                hint_image = compose_hint(scale);
                hint_scale = scale;
            }
            const float w = float(hint_image->width) / scale * u, h = float(hint_image->height) / scale * u;
            out.push_back({hint_image, 40 * u, vh - h - 40 * u, w, h, 1.0f});
        }
        if (toast_frame) {
            const TitleAchievement* a = achievement(toast_frame->id);
            if (a) {
                if (!toast_image || toast_image_id != a->id || toast_image_scale != scale) {
                    toast_image = compose_toast(*a, scale);
                    toast_image_id = a->id;
                    toast_image_scale = scale;
                }
                const float w = 520 * u, h = 84 * u;
                const float y = vh - h - 52 * u + toast_frame->lift * 34 * u;
                out.push_back({toast_image, (vw - w) / 2, y, w, h, toast_frame->opacity});
            }
        }
        return out;
    }

    // Where the notification is, in picture fractions (for a tap).
    bool in_toast(float x, float y) const {
        if (!toast_frame) return false;
        const float u = view_height / 720.0f, w = 520 * u, h = 84 * u;
        const float px = x * view_width, py = y * view_height;
        const float left = (view_width - w) / 2, top = view_height - h - 52 * u;
        return px >= left && px <= left + w && py >= top && py <= top + h;
    }
    // The page's row under a point, -1 for none; -2 outside the panel.
    int row_at(float x, float y) const {
        const float u = view_height / 720.0f, w = 1040 * u, h = 620 * u;
        const float left = (view_width - w) / 2, top = (view_height - h) / 2;
        const float px = (x * view_width - left) / u, py = (y * view_height - top) / u;
        if (px < 0 || py < 0 || px > 1040 || py > 620) return -2;
        if (py < 96) return -1;
        const int k = int((py - 96) / 96.0f);
        if (k < 0 || k >= nav.visible || nav.first + k >= nav.count) return -1;
        return nav.first + k;
    }
};

AchievementOverlay& AchievementOverlay::instance() {
    static AchievementOverlay* const overlay = new AchievementOverlay();  // never destroyed: any thread may end the game
    return *overlay;
}
AchievementOverlay::AchievementOverlay() : impl_(new Impl()) {}

void AchievementOverlay::configure(std::vector<uint8_t> xdbf, std::filesystem::path unlocked_file, uint32_t xbox_language,
                                   bool notifications, Host host) {
    std::string error;
    auto title = parse_title_data(xdbf, xbox_language ? xbox_language : 1, &error);
    // A language whose table leaves names empty falls back to English, as
    // the launcher's page does.
    if (title && title->locale != 1 &&
        std::any_of(title->achievements.begin(), title->achievements.end(), [](const TitleAchievement& a) { return a.name.empty(); }))
        title = parse_title_data(xdbf, 1, &error);
    std::lock_guard lock(impl_->mutex);
    impl_->title = std::move(title);
    impl_->file = std::move(unlocked_file);
    impl_->unlocked = read_unlocked(impl_->file);
    impl_->language = xbox_language;
    impl_->notifications = notifications;
    impl_->host = std::move(host);
    impl_->configured = impl_->title.has_value();
    std::cerr << "ACHIEVEMENT_OVERLAY configured=" << impl_->configured << " achievements="
              << (impl_->title ? impl_->title->achievements.size() : 0) << " unlocked=" << impl_->unlocked.list.size()
              << " notifications=" << notifications << (impl_->title ? "" : " error=") << (impl_->title ? "" : error) << '\n';
    if (impl_->configured)
        set_screen_layer_source([this](float w, float h) {
            std::lock_guard inner(impl_->mutex);
            return impl_->layers(w, h);
        });
}

bool AchievementOverlay::configured() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->configured;
}

std::vector<uint32_t> AchievementOverlay::awarded(std::span<const uint32_t> ids, int64_t now) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->configured) return {};
    std::vector<uint32_t> known;
    for (const uint32_t id : ids)
        if (impl_->achievement(uint16_t(id))) known.push_back(id);
    const auto added = add_unlocked(impl_->unlocked, known, now);
    for (const uint32_t id : added) {
        const TitleAchievement* a = impl_->achievement(uint16_t(id));
        std::cerr << "ACHIEVEMENT_UNLOCKED id=" << id << " name=\"" << (a ? a->name : "") << "\" gamerscore="
                  << (a ? a->gamerscore : 0) << " toast=" << impl_->notifications << '\n';
        if (impl_->notifications) impl_->toasts.push(uint16_t(id));
    }
    if (!added.empty() && impl_->open) impl_->page_key.clear();
    return added;
}

bool AchievementOverlay::page_open() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->open;
}

void AchievementOverlay::open_page(bool open) {
    std::lock_guard lock(impl_->mutex);
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    impl_->set_open(open, now);
    set_screen_layers_shown(impl_->open || impl_->toast_frame.has_value());
}

namespace {
// "a:b,c:d" as pairs of numbers.
std::vector<std::vector<uint64_t>> number_groups(const char* text) {
    std::vector<std::vector<uint64_t>> groups;
    if (!text) return groups;
    // Groups split at ',' or ';', numbers at ':' or '@' (the second pair for
    // test runners that use the first as their own separators).
    std::string all(text);
    for (char& c : all) c = c == ';' ? ',' : c == '@' ? ':' : c;
    std::stringstream items(all);
    std::string item;
    while (std::getline(items, item, ',')) {
        std::vector<uint64_t> numbers;
        std::stringstream parts(item);
        std::string part;
        while (std::getline(parts, part, ':')) numbers.push_back(std::strtoull(part.c_str(), nullptr, 10));
        groups.push_back(std::move(numbers));
    }
    return groups;
}
}

void AchievementOverlay::poll(double now, uint64_t present) {
    {
        // The test switches (header), outside the lock: they call in again.
        static const auto awards = number_groups(std::getenv("SFR_TEST_ACHIEVEMENT_AWARD"));
        static const auto page = number_groups(std::getenv("SFR_TEST_ACHIEVEMENT_PAGE"));
        for (const auto& award : awards)
            if (award.size() == 2 && award[0] == present) {
                const uint32_t id = uint32_t(award[1]);
                awarded(std::span<const uint32_t>(&id, 1), unix_now());
            }
        for (const auto& step : page) {
            if (step.size() >= 1 && step[0] == present) {
                open_page(true);
                std::lock_guard lock(impl_->mutex);
                if (step.size() >= 3) impl_->nav.move(int(step[2]));
            }
            if (step.size() >= 2 && step[1] == present) open_page(false);
        }
        static const char* trace = std::getenv("SFR_ACHIEVEMENT_TRACE");
        if (trace && *trace && present % 1200 == 0) {
            std::lock_guard lock(impl_->mutex);
            Impl& m = *impl_;
            const auto load32 = [&](uint32_t address) -> std::optional<uint32_t> {
                uint8_t b[4];
                if (!m.host.read || !m.host.read(address, b)) return std::nullopt;
                return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
            };
            const auto record = achievement_record_address(load32);
            std::cerr << "ACHIEVEMENT_PROGRESS present=" << present << " record=0x" << std::hex << record.value_or(0) << std::dec;
            if (record) {
                std::vector<uint8_t> bytes(achievement_record_size);
                if (m.host.read(*record, bytes)) {
                    for (const auto& p : read_achievement_progress(bytes)) std::cerr << ' ' << p.id << '=' << p.value << '/' << p.target;
                    std::ofstream(trace, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                }
            }
            std::cerr << '\n';
        }
    }
    std::lock_guard lock(impl_->mutex);
    Impl& m = *impl_;
    if (!m.configured) return;
    const uint16_t buttons = m.host.pad_buttons ? m.host.pad_buttons() : 0;
    const auto key = [&](int k) { return m.host.key_down && m.host.key_down(k); };
    std::map<int, bool> keys;
    for (const int k : {key_f1, key_escape, key_backspace, key_up, key_down, key_page_up, key_page_down, key_home, key_end})
        keys[k] = key(k);
    const auto pressed_key = [&](int k) { return keys[k] && !m.previous_keys[k]; };
    const auto pressed = [&](uint16_t b) { return (buttons & b) && !(m.previous_buttons & b); };
    const bool sticks = (buttons & (pad_l3 | pad_r3)) == (pad_l3 | pad_r3);
    const bool sticks_before = (m.previous_buttons & (pad_l3 | pad_r3)) == (pad_l3 | pad_r3);

    if ((sticks && !sticks_before) || pressed_key(key_f1)) {
        m.set_open(!m.open, now);
    } else if (m.open) {
        if (pressed(pad_b) || pressed(pad_back) || pressed_key(key_escape) || pressed_key(key_backspace)) m.set_open(false, now);
    }
    if (m.open) {
        // LB / RB switch the tabs; on the Mods tab A and right step the
        // setting forward, left back.
        if (pressed(pad_lb) || pressed(pad_rb)) m.tab ^= 1;
        // The settings: one list, section after section (up and down cross
        // from one into the next).
        AchievementPageNav settings_list;
        settings_list.count = int(Impl::settings_order().size());
        settings_list.visible = settings_list.count;
        settings_list.selected = m.setting;
        AchievementPageNav& list = m.tab == 1 ? settings_list : m.nav;
        if (m.tab == 1) {
            const auto id = Impl::settings_order()[size_t(std::clamp(m.setting, 0, settings_list.count - 1))];
            const auto& item = game_settings::settings()[size_t(id)];
            if (!(item.offline_only && game_settings::online())) {
                if (pressed(pad_a) || pressed(pad_right)) game_settings::step(id, 1);
                if (pressed(pad_left)) game_settings::step(id, -1);
            }
            // Y with a restart waiting: the settings are written and the game
            // closes, to be started again.
            if (pressed(pad_y) && game_settings::restart_pending()) {
                std::cerr << "GAME_SETTINGS closing the game to apply\n";
                game_settings::save_now();
                std::cerr.flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(300));  // the log's writer
                std::_Exit(0);
            }
        }
        // Up and down repeat while held: after 0.4 s, ten a second.
        int direction = 0;
        if ((buttons & pad_up) || keys[key_up]) direction = -1;
        if ((buttons & pad_down) || keys[key_down]) direction = 1;
        if (direction != m.held_direction) {
            m.held_direction = direction;
            m.held_since = m.repeated_at = now;
            if (direction) list.move(direction);
        } else if (direction && now - m.held_since > 0.4 && now - m.repeated_at > 0.1) {
            m.repeated_at = now;
            list.move(direction);
        }
        if (m.tab == 0) {
            if (pressed(pad_left) || pressed_key(key_page_up)) m.nav.page(-1);
            if (pressed(pad_right) || pressed_key(key_page_down)) m.nav.page(1);
        }
        if (pressed_key(key_home)) list.move(-list.count);
        if (pressed_key(key_end)) list.move(list.count);
        if (m.tab == 1) m.setting = settings_list.selected;
        if (now - m.progress_read_at > 1.0) m.refresh_progress(now);
    }
    if (m.closing) {
        const bool any_key = std::any_of(keys.begin(), keys.end(), [](const auto& k) { return k.second; });
        if (!buttons && !any_key && m.fingers.empty()) {
            m.closing = false;
            if (m.host.page_shown) m.host.page_shown(false);
        }
    }
    m.previous_buttons = buttons;
    m.previous_keys = keys;
    m.toast_frame = m.notifications ? m.toasts.at(now) : std::nullopt;
    if (game_settings::value(game_settings::menu_hint) && touch_title_waiting()) m.hint_until = now + 6.0;
    m.hint_shown = game_settings::value(game_settings::menu_hint) && now < m.hint_until;
    set_screen_layers_shown(m.open || m.toast_frame.has_value() || m.hint_shown);
}

void AchievementOverlay::touches(std::span<const Touch> touches, double now) {
    std::lock_guard lock(impl_->mutex);
    Impl& m = *impl_;
    if (!m.configured) return;
    // Fingers that lifted: a tap is a short touch that hardly moved.
    std::map<int64_t, Impl::Finger> current;
    for (const Touch& t : touches) {
        auto found = m.fingers.find(t.id);
        Impl::Finger f = found != m.fingers.end() ? found->second : Impl::Finger{t.x, t.y, t.x, t.y, now};
        if (m.open && m.tab == 0 && found != m.fingers.end() && m.fingers.size() == 1) {
            // One finger dragging scrolls a row per row height moved.
            const float u = m.view_height / 720.0f;
            const float moved = (f.y0 - t.y) * m.view_height / (96.0f * u);
            const int rows = int(moved - float(m.scroll_anchor));
            if (rows != 0) {
                m.nav.first = std::clamp(m.nav.first + rows, 0, (std::max)(0, m.nav.count - m.nav.visible));
                m.nav.selected = std::clamp(m.nav.selected, m.nav.first, m.nav.first + m.nav.visible - 1);
                m.scroll_anchor += rows;
            }
        }
        f.x = t.x;
        f.y = t.y;
        current[t.id] = f;
    }
    m.most_fingers = (std::max)(m.most_fingers, current.size());
    for (const auto& [id, f] : m.fingers) {
        if (current.count(id)) continue;
        const bool tap = now - f.since < 0.5 && std::fabs(f.x - f.x0) < 0.03f && std::fabs(f.y - f.y0) < 0.04f;
        if (current.empty() && tap) {
            if (m.most_fingers >= 3) {
                m.set_open(!m.open, now);
            } else if (m.most_fingers == 1) {
                if (!m.open && m.in_toast(f.x, f.y)) {
                    m.set_open(true, now);
                } else if (m.open) {
                    const int row = m.row_at(f.x, f.y);
                    if (row == -2) m.set_open(false, now);
                    else if (row >= 0 && m.tab == 0) m.nav.selected = row;
                }
            }
        }
    }
    if (current.empty()) {
        m.most_fingers = 0;
        m.scroll_anchor = 0;
    }
    m.fingers = std::move(current);
    set_screen_layers_shown(m.open || m.toast_frame.has_value() || m.hint_shown);
}
}
