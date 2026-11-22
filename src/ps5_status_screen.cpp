#include "ps5_status_screen.h"

#include "native_graphics.h"
#include "native_presentation.h"
#include "overlay_canvas.h"
#include "screen_overlay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace sfr {
struct Ps5StatusScreen::Impl {
    NativeGraphics graphics;
    std::unique_ptr<NativePresentation> presentation;
    OverlayFonts fonts;
    std::mutex mutex;
    std::string heading, body, footer;
    float progress = -1.0f;
    uint64_t version = 1, drawn = 0, next_id = 1;
    std::shared_ptr<const OverlayImage> image;

    std::shared_ptr<const OverlayImage> compose() {
        std::string h, b, f;
        float p;
        {
            std::lock_guard lock(mutex);
            h = heading, b = body, f = footer, p = progress;
        }
        const float W = 1280, H = 720;
        OverlayCanvas canvas{uint32_t(W), uint32_t(H)};
        canvas.fill_round_rect(0, 0, W, H, 0, overlay_rgb(0x1B1F3A), overlay_rgb(0x0B0D1A));
        // A card in the middle, in the icon's colours.
        const float cx = 190, cy = 170, cw = W - 380, ch = 380;
        canvas.fill_round_rect(cx, cy, cw, ch, 22, overlay_rgb(0x141A36), overlay_rgb(0x0E1228));
        canvas.stroke_round_rect(cx, cy, cw, ch, 22, 3, overlay_rgb(0x3BE0C8, 0.7f));
        canvas.fill_round_rect(cx + 34, cy + 40, 8, 44, 4, overlay_rgb(0xFF6A3D));
        canvas.text(fonts, h, cx + 60, cy + 76, 38, overlay_rgb(0xFFFFFF), OverlayFonts::bold, cw - 100);
        const auto lines = wrap_overlay_text(fonts, b, 24, OverlayFonts::regular, cw - 120, 7);
        float y = cy + 136;
        for (const auto& line : lines) {
            canvas.text(fonts, line, cx + 60, y, 24, overlay_rgb(0xD8DCF0));
            y += 34;
        }
        if (p >= 0.0f) {
            const float bx = cx + 60, bw = cw - 120, by = cy + ch - 90;
            canvas.fill_round_rect(bx, by, bw, 18, 9, overlay_rgb(0x02103A));
            canvas.fill_round_rect(bx, by, (std::max)(18.0f, bw * std::clamp(p, 0.0f, 1.0f)), 18, 9, overlay_rgb(0xFF8A1F),
                                   overlay_rgb(0xFF3D6E));
        }
        const auto small = wrap_overlay_text(fonts, f, 18, OverlayFonts::regular, cw - 120, 2);
        y = cy + ch - 40 - 24 * float(small.size() > 1);
        for (const auto& line : small) {
            canvas.text(fonts, line, cx + 60, y, 18, overlay_rgb(0x8A93B8));
            y += 24;
        }
        canvas.text(fonts, "FRR \xC2\xB7 RECOMPILED FOR PS5", 60, H - 36, 18, overlay_rgb(0x3BE0C8, 0.8f), OverlayFonts::bold);
        auto out = std::make_shared<OverlayImage>();
        out->width = canvas.width();
        out->height = canvas.height();
        out->rgba = canvas.take();
        out->id = next_id++;
        return out;
    }
};

Ps5StatusScreen::Ps5StatusScreen() : impl_(std::make_unique<Impl>()) {
    impl_->fonts.load({}, {}, {});  // the title's own fonts (overlay_font_candidates)
    impl_->graphics.initialize();
    impl_->presentation = std::make_unique<NativePresentation>(impl_->graphics, 1280, 720);
    Impl* impl = impl_.get();
    set_screen_layer_source([impl](float view_width, float view_height) {
        std::vector<ScreenLayer> layers;
        if (impl->image) layers.push_back(ScreenLayer{impl->image, 0, 0, view_width, view_height, 1.0f});
        return layers;
    });
    set_screen_layers_shown(true);
    std::cerr << "PS5_STATUS_SCREEN shown fonts=" << impl_->fonts.any() << '\n';
}

Ps5StatusScreen::~Ps5StatusScreen() {
    set_screen_layers_shown(false);
    set_screen_layer_source({});
    if (impl_->presentation) impl_->presentation->flush();
}

void Ps5StatusScreen::set(std::string heading, std::string body, std::string footer, float progress) {
    std::lock_guard lock(impl_->mutex);
    if (heading == impl_->heading && body == impl_->body && footer == impl_->footer &&
        std::abs(progress - impl_->progress) < 0.002f)
        return;
    impl_->heading = std::move(heading);
    impl_->body = std::move(body);
    impl_->footer = std::move(footer);
    impl_->progress = progress;
    ++impl_->version;
}

void Ps5StatusScreen::frame() {
    uint64_t version;
    {
        std::lock_guard lock(impl_->mutex);
        version = impl_->version;
    }
    if (version != impl_->drawn) {
        impl_->image = impl_->compose();
        impl_->drawn = version;
    }
    impl_->presentation->pump_events();
    impl_->presentation->clear(NativeClear{.color = true, .depth = true, .stencil = true, .color_value = {0.04f, 0.05f, 0.1f, 1.0f}});
    impl_->presentation->present();
}

void Ps5StatusScreen::run_while(const std::function<void()>& work) {
    std::atomic<bool> done{false};
    std::thread worker([&] {
        work();
        done = true;
    });
    while (!done.load()) frame();
    worker.join();
    frame();
}

void Ps5StatusScreen::run_forever() {
    for (;;) {
        frame();
        if (impl_->presentation->close_requested()) {
            impl_->presentation->flush();
            std::_Exit(0);
        }
    }
}
}
