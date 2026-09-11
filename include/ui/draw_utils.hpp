#pragma once

#include "game.hpp"

#include <SDL2/SDL.h>

namespace vitrine {

SDL_Color color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);

void setColor(SDL_Renderer* renderer, SDL_Color value);

void fillRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color value);

void fillRoundedRect(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, SDL_Color value);

void gradientRect(SDL_Renderer* renderer, int x, int y, int w, int h,
                  vitrine::Color top, vitrine::Color bottom);

void horizontalGradientRect(SDL_Renderer* renderer, int x, int y, int w, int h,
                            SDL_Color left, SDL_Color right);

std::string scoreText(float score);

std::string hoursText(float hours);

class TextRenderer;

void drawKeyHint(SDL_Renderer* renderer, TextRenderer& text, int x, int y,
                 const std::string& key, const std::string& label, bool accent = false);

}  // namespace vitrine
