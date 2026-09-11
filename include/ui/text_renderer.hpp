#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <string>
#include <unordered_map>
#include <vector>

namespace vitrine {

class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    bool initialize();
    void shutdown();
    void clear();

    int width(const std::string& text, int size) const;
    std::string ellipsize(const std::string& text, int size, int maxWidth) const;

    void draw(SDL_Renderer* renderer, const std::string& text, int x, int y, int size,
              SDL_Color value, int maxWidth = 0);

    int drawWrapped(SDL_Renderer* renderer, const std::string& text, int x, int y, int size,
                    SDL_Color value, int maxWidth, int maxLines);

private:
    struct Entry {
        SDL_Texture* texture = nullptr;
        int w = 0;
        int h = 0;
    };

    TTF_Font* font(int size) const;

#ifdef __SWITCH__
    bool openMemoryFont(int size, TTF_Font*& destination);
    PlFontData fontData_{};
    bool plInitialized_ = false;
#endif

    TTF_Font* fontSmall_ = nullptr;
    TTF_Font* fontBody_ = nullptr;
    TTF_Font* fontHeading_ = nullptr;
    TTF_Font* fontHero_ = nullptr;
    bool initialized_ = false;

    mutable std::unordered_map<std::string, int> widthCache_;
    std::unordered_map<std::string, std::string> ellipsisCache_;
    std::unordered_map<std::string, std::vector<std::string>> wrappedCache_;
    std::unordered_map<std::string, Entry> cache_;
};

}  // namespace vitrine
