#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace vitrine {

class ImageRenderer {
public:
    ImageRenderer() = default;
    ~ImageRenderer();

    void clear();

    bool drawCover(SDL_Renderer* renderer, const std::string& path, int x, int y, int w, int h);
    bool drawContain(SDL_Renderer* renderer, const std::string& path, int x, int y, int w, int h);

private:
    SDL_Texture* textureFor(SDL_Renderer* renderer, const std::string& path);

    static constexpr std::size_t kMaxTextures = 18;
    std::unordered_map<std::string, SDL_Texture*> textures_;
    std::vector<std::string> textureOrder_;
};

}  // namespace vitrine
