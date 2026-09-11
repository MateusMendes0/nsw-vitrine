#include "draw_utils.hpp"
#include "text_renderer.hpp"

#include <algorithm>
#include <cmath>

namespace vitrine {

SDL_Color color(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    return {r, g, b, a};
}

void setColor(SDL_Renderer* renderer, SDL_Color value) {
    SDL_SetRenderDrawColor(renderer, value.r, value.g, value.b, value.a);
}

void fillRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color value) {
    setColor(renderer, value);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);
}

void fillRoundedRect(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, SDL_Color value) {
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    fillRect(renderer, x + radius, y, w - radius * 2, h, value);
    fillRect(renderer, x, y + radius, w, h - radius * 2, value);
    setColor(renderer, value);
    for (int dy = 0; dy < radius; ++dy) {
        const int dx = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - (radius - dy) * (radius - dy))));
        SDL_RenderDrawLine(renderer, x + radius - dx, y + dy, x + w - radius + dx - 1, y + dy);
        SDL_RenderDrawLine(renderer, x + radius - dx, y + h - dy - 1, x + w - radius + dx - 1, y + h - dy - 1);
    }
}

void gradientRect(SDL_Renderer* renderer, int x, int y, int w, int h,
                  vitrine::Color top, vitrine::Color bottom) {
    for (int row = 0; row < h; ++row) {
        const float t = h <= 1 ? 0.0f : static_cast<float>(row) / static_cast<float>(h - 1);
        SDL_Color line{
            static_cast<Uint8>(top.r + (bottom.r - top.r) * t),
            static_cast<Uint8>(top.g + (bottom.g - top.g) * t),
            static_cast<Uint8>(top.b + (bottom.b - top.b) * t), 255};
        setColor(renderer, line);
        SDL_RenderDrawLine(renderer, x, y + row, x + w - 1, y + row);
    }
}

void horizontalGradientRect(SDL_Renderer* renderer, int x, int y, int w, int h,
                            SDL_Color left, SDL_Color right) {
    for (int column = 0; column < w; ++column) {
        const float t = w <= 1 ? 0.0f : static_cast<float>(column) / static_cast<float>(w - 1);
        SDL_Color line{
            static_cast<Uint8>(left.r + (right.r - left.r) * t),
            static_cast<Uint8>(left.g + (right.g - left.g) * t),
            static_cast<Uint8>(left.b + (right.b - left.b) * t),
            static_cast<Uint8>(left.a + (right.a - left.a) * t)};
        setColor(renderer, line);
        SDL_RenderDrawLine(renderer, x + column, y, x + column, y + h - 1);
    }
}

std::string scoreText(float score) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.0f", score);
    return buffer;
}

std::string hoursText(float hours) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), hours == std::floor(hours) ? "%.0fh" : "%.1fh", hours);
    return buffer;
}

void drawKeyHint(SDL_Renderer* renderer, TextRenderer& text, int x, int y,
                 const std::string& key, const std::string& label, bool accent) {
    const int keyWidth = std::max(28, text.width(key, 18) + 12);
    fillRoundedRect(renderer, x, y, keyWidth, 28, 14,
                    accent ? color(117, 226, 255) : color(229, 235, 247));
    const int keyTextWidth = text.width(key, 18);
    text.draw(renderer, key, x + (keyWidth - keyTextWidth) / 2, y + 4, 18,
              accent ? color(7, 45, 78) : color(20, 29, 49));
    text.draw(renderer, label, x + keyWidth + 9, y + 4, 18,
              accent ? color(222, 246, 255) : color(204, 213, 231));
}

}  // namespace vitrine
