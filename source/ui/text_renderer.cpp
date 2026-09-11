#include "text_renderer.hpp"

#include <cstdlib>
#include <sstream>
#include <utility>

namespace vitrine {

#ifdef __SWITCH__
bool TextRenderer::openMemoryFont(int size, TTF_Font*& destination) {
    SDL_RWops* source = SDL_RWFromConstMem(fontData_.address, static_cast<int>(fontData_.size));
    if (!source) return false;
    destination = TTF_OpenFontRW(source, 1, size);
    return destination != nullptr;
}
#endif

bool TextRenderer::initialize() {
    if (TTF_Init() != 0) return false;
    initialized_ = true;

#ifdef __SWITCH__
    if (R_FAILED(plInitialize(PlServiceType_User))) return false;
    plInitialized_ = true;
    if (R_FAILED(plGetSharedFontByType(&fontData_, PlSharedFontType_Standard))) return false;
    return openMemoryFont(18, fontSmall_) && openMemoryFont(22, fontBody_) &&
           openMemoryFont(28, fontHeading_) && openMemoryFont(42, fontHero_);
#else
    const char* configured = std::getenv("VITRINE_FONT");
    const char* path = configured ? configured : "C:/Windows/Fonts/segoeui.ttf";
    fontSmall_ = TTF_OpenFont(path, 18);
    fontBody_ = TTF_OpenFont(path, 22);
    fontHeading_ = TTF_OpenFont(path, 28);
    fontHero_ = TTF_OpenFont(path, 42);
    return fontSmall_ && fontBody_ && fontHeading_ && fontHero_;
#endif
}

void TextRenderer::shutdown() {
    if (!initialized_) return;
    clear();
    if (fontSmall_) TTF_CloseFont(fontSmall_);
    if (fontBody_) TTF_CloseFont(fontBody_);
    if (fontHeading_) TTF_CloseFont(fontHeading_);
    if (fontHero_) TTF_CloseFont(fontHero_);
    fontSmall_ = fontBody_ = fontHeading_ = fontHero_ = nullptr;
#ifdef __SWITCH__
    if (plInitialized_) plExit();
    plInitialized_ = false;
#endif
    TTF_Quit();
    initialized_ = false;
}

TextRenderer::~TextRenderer() {
    shutdown();
}

void TextRenderer::clear() {
    for (auto& item : cache_) {
        if (item.second.texture) {
            SDL_DestroyTexture(item.second.texture);
        }
    }
    cache_.clear();
    widthCache_.clear();
    ellipsisCache_.clear();
    wrappedCache_.clear();
}

TTF_Font* TextRenderer::font(int size) const {
    if (size <= 18) return fontSmall_;
    if (size <= 22) return fontBody_;
    if (size <= 30) return fontHeading_;
    return fontHero_;
}

int TextRenderer::width(const std::string& text, int size) const {
    const std::string key = std::to_string(size) + ":" + text;
    const auto cached = widthCache_.find(key);
    if (cached != widthCache_.end()) return cached->second;
    int w = 0;
    TTF_SizeUTF8(font(size), text.c_str(), &w, nullptr);
    widthCache_.emplace(key, w);
    return w;
}

std::string TextRenderer::ellipsize(const std::string& text, int size, int maxWidth) const {
    if (width(text, size) <= maxWidth) return text;
    std::string result = text;
    while (!result.empty() && width(result + "...", size) > maxWidth) {
        result.pop_back();
        while (!result.empty() && (static_cast<unsigned char>(result.back()) & 0xC0) == 0x80) {
            result.pop_back();
        }
    }
    return result + "...";
}

void TextRenderer::draw(SDL_Renderer* renderer, const std::string& text, int x, int y, int size,
                        SDL_Color value, int maxWidth) {
    if (text.empty()) return;
    std::string visible = text;
    if (maxWidth > 0) {
        const std::string ellipsisKey = std::to_string(size) + ":" +
            std::to_string(maxWidth) + ":" + text;
        const auto cached = ellipsisCache_.find(ellipsisKey);
        if (cached != ellipsisCache_.end()) {
            visible = cached->second;
        } else {
            visible = ellipsize(text, size, maxWidth);
            ellipsisCache_.emplace(ellipsisKey, visible);
        }
    }
    const std::string key = std::to_string(size) + ":" + std::to_string(value.r) + ":" +
                            std::to_string(value.g) + ":" + std::to_string(value.b) + ":" + visible;
    auto found = cache_.find(key);
    if (found == cache_.end()) {
        SDL_Surface* surface = TTF_RenderUTF8_Blended(font(size), visible.c_str(), value);
        if (!surface) return;
        Entry entry;
        entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
        entry.w = surface->w;
        entry.h = surface->h;
        SDL_FreeSurface(surface);
        if (!entry.texture) return;
        found = cache_.emplace(key, entry).first;
    }
    SDL_Rect destination{x, y, found->second.w, found->second.h};
    SDL_RenderCopy(renderer, found->second.texture, nullptr, &destination);
}

int TextRenderer::drawWrapped(SDL_Renderer* renderer, const std::string& text, int x, int y, int size,
                              SDL_Color value, int maxWidth, int maxLines) {
    const std::string wrapKey = std::to_string(size) + ":" + std::to_string(maxWidth) + ":" +
        std::to_string(maxLines) + ":" + text;
    auto cached = wrappedCache_.find(wrapKey);
    if (cached == wrappedCache_.end()) {
        std::istringstream words(text);
        std::string line;
        std::string word;
        std::vector<std::string> lines;
        while (words >> word) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && width(candidate, size) > maxWidth) {
                lines.push_back(line);
                line = word;
                if (static_cast<int>(lines.size()) == maxLines) break;
            } else {
                line = candidate;
            }
        }
        if (static_cast<int>(lines.size()) < maxLines && !line.empty()) lines.push_back(line);
        if (!lines.empty() && !words.eof()) {
            lines.back() = ellipsize(lines.back() + "...", size, maxWidth);
        }
        cached = wrappedCache_.emplace(wrapKey, std::move(lines)).first;
    }
    const int lineHeight = size + 8;
    const std::vector<std::string>& lines = cached->second;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        draw(renderer, lines[i], x, y + static_cast<int>(i) * lineHeight, size, value);
    }
    return static_cast<int>(lines.size()) * lineHeight;
}

}  // namespace vitrine
