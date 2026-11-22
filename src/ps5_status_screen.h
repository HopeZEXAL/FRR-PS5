#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace sfr {
// A plain screen of the port's own (PS5), for before the game runs: the
// first-start install's progress, or why there is nothing to play. Only made
// when one of those is shown, so an ordinary start never opens the display
// twice. The game itself is never started after it: the player closes the
// title and starts it again.
class Ps5StatusScreen {
public:
    Ps5StatusScreen();
    ~Ps5StatusScreen();
    // What is shown: a heading, a paragraph, a line in small print at the
    // bottom, and a bar (progress < 0: none).
    void set(std::string heading, std::string body, std::string footer, float progress = -1.0f);
    // Draws until work() returns (on a thread of its own), then returns.
    void run_while(const std::function<void()>& work);
    // Draws until the title is closed.
    [[noreturn]] void run_forever();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void frame();
};
}
