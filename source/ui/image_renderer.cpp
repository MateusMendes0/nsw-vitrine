#include "image_renderer.hpp"

#include <algorithm>

namespace vitrine {

ImageRenderer::~ImageRenderer() {
    clear();
}

void ImageRenderer::clear() {
    for (auto& item : textures_) {
        if (item.second) {
            SDL_DestroyTexture(item.second);
        }
    }
    textures_.clear();
    textureOrder_.clear();
}

bool ImageRenderer::drawCover(SDL_Renderer* renderer, const std::string& path, int x, int y, int w, int h) {
    SDL_Texture* texture = textureFor(renderer, path);
    if (!texture) return false;

    int sourceWidth = 0;
    int sourceHeight = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &sourceWidth, &sourceHeight);
    if (sourceWidth <= 0 || sourceHeight <= 0) return false;
    const float sourceRatio = static_cast<float>(sourceWidth) / sourceHeight;
    const float destinationRatio = static_cast<float>(w) / h;
    SDL_Rect source{0, 0, sourceWidth, sourceHeight};
    if (sourceRatio > destinationRatio) {
        source.w = static_cast<int>(sourceHeight * destinationRatio);
        source.x = (sourceWidth - source.w) / 2;
    } else {
        source.h = static_cast<int>(sourceWidth / destinationRatio);
        source.y = (sourceHeight - source.h) / 2;
    }
    SDL_Rect destination{x, y, w, h};
    SDL_RenderCopy(renderer, texture, &source, &destination);
    return true;
}

bool ImageRenderer::drawContain(SDL_Renderer* renderer, const std::string& path, int x, int y, int w, int h) {
    SDL_Texture* texture = textureFor(renderer, path);
    if (!texture) return false;
    int sourceWidth = 0;
    int sourceHeight = 0;
    SDL_QueryTexture(texture, nullptr, nullptr, &sourceWidth, &sourceHeight);
    if (sourceWidth <= 0 || sourceHeight <= 0) return false;
    const float scale = std::min(static_cast<float>(w) / sourceWidth,
                                 static_cast<float>(h) / sourceHeight);
    const int destinationWidth = static_cast<int>(sourceWidth * scale);
    const int destinationHeight = static_cast<int>(sourceHeight * scale);
    SDL_Rect destination{x + (w - destinationWidth) / 2, y + (h - destinationHeight) / 2,
                         destinationWidth, destinationHeight};
    SDL_RenderCopy(renderer, texture, nullptr, &destination);
    return true;
}

SDL_Texture* ImageRenderer::textureFor(SDL_Renderer* renderer, const std::string& path) {
    if (path.empty()) return nullptr;
    const auto found = textures_.find(path);
    if (found != textures_.end()) return found->second;

    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) return nullptr;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (!texture) return nullptr;
    while (textures_.size() >= kMaxTextures && !textureOrder_.empty()) {
        const std::string oldest = textureOrder_.front();
        textureOrder_.erase(textureOrder_.begin());
        const auto oldTexture = textures_.find(oldest);
        if (oldTexture != textures_.end()) {
            SDL_DestroyTexture(oldTexture->second);
            textures_.erase(oldTexture);
        }
    }
    textures_[path] = texture;
    textureOrder_.push_back(path);
    return texture;
}

}  // namespace vitrine
