#include "catalog.hpp"
#include "api_client.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kCoverColumns = 5;
constexpr int kCoverRows = 1;
constexpr int kClassicColumns = 4;
constexpr int kClassicRows = 2;
constexpr const char* kAppVersion = "1.2.0";
constexpr const char* kAppAuthor = "Mateus Mendes";

SDL_Color color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) { return {r, g, b, a}; }

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

class TextRenderer {
public:
    bool initialize() {
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

    void shutdown() {
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

    ~TextRenderer() { shutdown(); }

    void clear() {
        for (auto& item : cache_) SDL_DestroyTexture(item.second.texture);
        cache_.clear();
    }

    int width(const std::string& text, int size) const {
        int w = 0;
        TTF_SizeUTF8(font(size), text.c_str(), &w, nullptr);
        return w;
    }

    std::string ellipsize(const std::string& text, int size, int maxWidth) const {
        if (width(text, size) <= maxWidth) return text;
        std::string result = text;
        while (!result.empty() && width(result + "...", size) > maxWidth) {
            result.pop_back();
            while (!result.empty() && (static_cast<unsigned char>(result.back()) & 0xC0) == 0x80) result.pop_back();
        }
        return result + "...";
    }

    void draw(SDL_Renderer* renderer, const std::string& text, int x, int y, int size,
              SDL_Color value, int maxWidth = 0) {
        if (text.empty()) return;
        const std::string visible = maxWidth > 0 ? ellipsize(text, size, maxWidth) : text;
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

    int drawWrapped(SDL_Renderer* renderer, const std::string& text, int x, int y, int size,
                    SDL_Color value, int maxWidth, int maxLines) {
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
        const int lineHeight = size + 8;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::string visible = lines[i];
            if (i + 1 == lines.size() && !words.eof()) visible = ellipsize(visible + "...", size, maxWidth);
            draw(renderer, visible, x, y + static_cast<int>(i) * lineHeight, size, value);
        }
        return static_cast<int>(lines.size()) * lineHeight;
    }

private:
    struct Entry { SDL_Texture* texture = nullptr; int w = 0; int h = 0; };

    TTF_Font* font(int size) const {
        if (size <= 18) return fontSmall_;
        if (size <= 22) return fontBody_;
        if (size <= 30) return fontHeading_;
        return fontHero_;
    }

#ifdef __SWITCH__
    bool openMemoryFont(int size, TTF_Font*& destination) {
        SDL_RWops* source = SDL_RWFromConstMem(fontData_.address, static_cast<int>(fontData_.size));
        if (!source) return false;
        destination = TTF_OpenFontRW(source, 1, size);
        return destination != nullptr;
    }
    PlFontData fontData_{};
    bool plInitialized_ = false;
#endif
    TTF_Font* fontSmall_ = nullptr;
    TTF_Font* fontBody_ = nullptr;
    TTF_Font* fontHeading_ = nullptr;
    TTF_Font* fontHero_ = nullptr;
    bool initialized_ = false;
    std::unordered_map<std::string, Entry> cache_;
};

class ImageRenderer {
public:
    ~ImageRenderer() { clear(); }

    void clear() {
        for (auto& item : textures_) SDL_DestroyTexture(item.second);
        textures_.clear();
        textureOrder_.clear();
    }

    bool drawCover(SDL_Renderer* renderer, const std::string& path, int x, int y, int w, int h) {
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

    bool drawContain(SDL_Renderer* renderer, const std::string& path, int x, int y, int w, int h) {
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

private:
    SDL_Texture* textureFor(SDL_Renderer* renderer, const std::string& path) {
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

    static constexpr std::size_t kMaxTextures = 18;
    std::unordered_map<std::string, SDL_Texture*> textures_;
    std::vector<std::string> textureOrder_;
};

struct Input {
    bool up = false, down = false, left = false, right = false;
    bool accept = false, back = false, search = false, sort = false;
    bool previousGenre = false, nextGenre = false;
    bool viewMode = false;
    bool favorite = false;
    bool backlog = false;
    bool surprise = false;
    bool sync = false;
    bool quit = false;
};

struct CoverRequest {
    vitrine::Game game;
    bool portrait = true;
};

class App {
public:
    explicit App(bool networkReady)
        : favoriteCatalog_(std::vector<vitrine::Game>{}),
          backlogCatalog_(std::vector<vitrine::Game>{}), networkReady_(networkReady) {
        surpriseSeed_ ^= SDL_GetTicks();
        apiInitialized_ = api_.initialize();
        favoriteCatalog_.replace(api_.loadFavorites());
        for (const vitrine::Game& game : favoriteCatalog_.all()) favoriteIds_.insert(game.id);
        backlogCatalog_.replace(api_.loadBacklog());
        for (const vitrine::Game& game : backlogCatalog_.all()) backlogStatuses_[game.id] = game.backlogStatus;
        const vitrine::ApiResult cached = api_.loadCache();
        if (cached.success) {
            catalog_.replace(cached.games);
            usingApi_ = true;
            currentPage_ = 1;
            hasMore_ = cached.hasMore;
            status_ = cached.message;
        } else {
            status_ = networkReady_ ? "Dados demonstrativos • pressione - para atualizar" :
                                      "Offline • usando dados demonstrativos";
        }
        rebuildGenres();
        refresh();
        startGridReveal();
        coverWorker_ = std::thread([this]() { coverWorkerLoop(); });
        queueVisibleCovers();
        startInitialSync();
    }

    ~App() {
        {
            std::lock_guard<std::mutex> lock(coverMutex_);
            stopCoverWorker_ = true;
        }
        coverCondition_.notify_one();
        if (coverWorker_.joinable()) coverWorker_.join();
        if (screenshotThread_.joinable()) screenshotThread_.join();
        if (initialSyncThread_.joinable()) initialSyncThread_.join();
    }

    void handle(const Input& input) {
        finishInitialSync();
        finishScreenshotLoad();
        if (about_) {
            handleAbout(input);
            return;
        }
        if (backlogPanel_) {
            handleBacklogPanel(input);
            return;
        }
        if (detailClosing_) return;
        if (screenshotFullscreen_) {
            if (!detailScreenshots_.empty()) {
                if (input.left) {
                    screenshotIndex_ = (screenshotIndex_ - 1 + static_cast<int>(detailScreenshots_.size())) %
                                       static_cast<int>(detailScreenshots_.size());
                }
                if (input.right) {
                    screenshotIndex_ = (screenshotIndex_ + 1) % static_cast<int>(detailScreenshots_.size());
                }
            }
            if (input.back || input.accept) screenshotFullscreen_ = false;
            return;
        }
        if (filterPanel_) {
            handleFilterPanel(input);
            return;
        }
        if (details_) {
            if (!detailScreenshots_.empty()) {
                if (input.left) {
                    screenshotIndex_ = (screenshotIndex_ - 1 + static_cast<int>(detailScreenshots_.size())) %
                                       static_cast<int>(detailScreenshots_.size());
                }
                if (input.right) {
                    screenshotIndex_ = (screenshotIndex_ + 1) % static_cast<int>(detailScreenshots_.size());
                }
            }
            if (input.favorite) toggleFavorite(detailGame_);
            if (input.backlog) {
                openBacklogPanel(detailGame_, true);
                return;
            }
            if (input.search) {
                loadSimilarGames(detailGame_);
                return;
            }
            if (input.back) {
                details_ = false;
                detailClosing_ = true;
                detailTransitionStart_ = SDL_GetTicks();
            }
            if (input.accept && !detailScreenshots_.empty()) screenshotFullscreen_ = true;
            return;
        }
        if (input.sync) {
            openAbout();
            return;
        }
        if (discoveryFocus_) {
            handleDiscoveryRibbon(input);
            return;
        }
        if (input.sort) { openFilterPanel(0); return; }
        if (input.previousGenre) { switchMainTab(-1); return; }
        if (input.nextGenre) { switchMainTab(1); return; }
        if (input.viewMode) {
            classicView_ = !classicView_;
            visibleCoverSignature_.clear();
            queueVisibleCovers();
            return;
        }
        if (input.back) {
            if (!backlogTab_ && !favoritesTab_ && discoveryIndex_ != 0) {
                applyDiscoverySection(0);
                return;
            }
            if (backlogTab_ || favoritesTab_) {
                backlogTab_ = false;
                favoritesTab_ = false;
                selected_ = 0;
                discoveryFocus_ = false;
                refresh();
                visibleCoverSignature_.clear();
                queueVisibleCovers();
                tabTransitionStart_ = SDL_GetTicks();
                status_ = discoveryIndex_ == 0 ? "Catalogo completo" : discoveryLabel(discoveryIndex_);
            }
            return;
        }
        if (input.search) openSearch();
        if (input.surprise) {
            surpriseMe();
            return;
        }
        if (input.backlog && !games_.empty()) {
            openBacklogPanel(*games_[selected_], false);
            return;
        }
        if (input.favorite && !games_.empty()) {
            toggleFavorite(*games_[selected_]);
            return;
        }
        const int columns = gridColumns();
        if (input.up && !favoritesTab_ && !backlogTab_ &&
            (games_.empty() || selected_ < columns)) {
            discoveryFocus_ = true;
            discoveryCursor_ = discoveryIndex_;
            status_ = "Escolha uma secao de descoberta";
            return;
        }
        if (input.down && !favoritesTab_ && !backlogTab_ && usingApi_ && hasMore_ &&
            (games_.empty() || selected_ + columns >= static_cast<int>(games_.size()))) {
            loadNextPage();
        }
        if (games_.empty()) return;

        int next = selected_;
        if (input.left && selected_ % columns > 0) --next;
        if (input.right && selected_ % columns < columns - 1 && selected_ + 1 < static_cast<int>(games_.size())) ++next;
        if (input.up && selected_ >= columns) next -= columns;
        if (input.down && selected_ + columns < static_cast<int>(games_.size())) next += columns;
        next = std::max(0, std::min(next, static_cast<int>(games_.size()) - 1));
        if (next != selected_) {
            previousSelected_ = selected_;
            selected_ = next;
            selectionAnimationStart_ = SDL_GetTicks();
        }
        queueVisibleCovers();
        if (input.accept) openSelectedDetails();
    }

    void render(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images) {
        renderBackground(renderer);
        renderHeader(renderer, text, images);
        renderToolbar(renderer, text);
        renderDiscoveryRibbon(renderer, text);
        renderGrid(renderer, text, images);
        renderFooter(renderer, text);
        if (details_) renderDetailsWithTransition(renderer, text, images, detailGame_);
        else if (detailClosing_) renderDetailsClosing(renderer, text, images, detailGame_);
        if (screenshotFullscreen_) renderScreenshotFullscreen(renderer, text, images);
        if (filterPanel_) renderFilterPanel(renderer, text);
        if (backlogPanel_) renderBacklogPanel(renderer, text);
        if (about_) renderAbout(renderer, text);
        renderTabTransition(renderer);
    }

    void releaseRendererResources() {
        if (detailTransitionTexture_) SDL_DestroyTexture(detailTransitionTexture_);
        detailTransitionTexture_ = nullptr;
    }

    void appendSearchText(const char* value) {
        filter_.query += value;
        refresh();
    }

    void eraseSearchCharacter() {
        if (filter_.query.empty()) return;
        filter_.query.pop_back();
        while (!filter_.query.empty() && (static_cast<unsigned char>(filter_.query.back()) & 0xC0) == 0x80) {
            filter_.query.pop_back();
        }
        refresh();
    }

private:
    int gridColumns() const { return classicView_ ? kClassicColumns : kCoverColumns; }
    int gridRows() const { return classicView_ ? kClassicRows : kCoverRows; }
    int visibleGameCount() const { return gridColumns() * gridRows(); }

    void startGridReveal() {
        gridRevealStart_ = SDL_GetTicks();
    }

    float gridRevealProgress(int slot) const {
        if (gridRevealStart_ == 0) return 1.0f;
        const float elapsed = static_cast<float>(SDL_GetTicks() - gridRevealStart_);
        const float local = elapsed - 130.0f - static_cast<float>(slot) * 72.0f;
        const float linear = std::max(0.0f, std::min(1.0f, local / 260.0f));
        return 1.0f - std::pow(1.0f - linear, 3.0f);
    }

    bool isFavorite(const std::string& id) const {
        return favoriteIds_.find(id) != favoriteIds_.end();
    }

    void switchMainTab(int direction) {
        int current = backlogTab_ ? 1 : (favoritesTab_ ? 2 : 0);
        current = (current + direction + 3) % 3;
        backlogTab_ = current == 1;
        favoritesTab_ = current == 2;
        discoveryFocus_ = false;
        selected_ = 0;
        previousSelected_ = -1;
        refresh();
        visibleCoverSignature_.clear();
        queueVisibleCovers();
        tabTransitionStart_ = SDL_GetTicks();
        status_ = backlogTab_ ? "Minha lista" :
                  (favoritesTab_ ? "Aba Favoritos" :
                   (discoveryIndex_ == 0 ? "Catalogo completo" : discoveryLabel(discoveryIndex_)));
    }

    static std::string formatCacheSize(std::uint64_t bytes) {
        char buffer[32] = {};
        if (bytes >= 1024ull * 1024ull) {
            std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                          static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ull) {
            std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%llu bytes",
                          static_cast<unsigned long long>(bytes));
        }
        return buffer;
    }

    void openAbout() {
        about_ = true;
        aboutOption_ = 0;
        aboutConfirmClear_ = false;
        aboutCacheBytes_ = api_.cacheSizeBytes();
        aboutMessage_.clear();
    }

    void handleAbout(const Input& input) {
        if (aboutConfirmClear_) {
            if (input.back) {
                aboutConfirmClear_ = false;
                aboutMessage_ = "Limpeza cancelada";
                return;
            }
            if (!input.accept) return;
            if (initialSyncRunning_) {
                aboutConfirmClear_ = false;
                aboutMessage_ = "Aguarde a atualizacao inicial terminar";
                return;
            }
            std::string error;
            const bool cleared = api_.clearCache(error);
            aboutCacheBytes_ = api_.cacheSizeBytes();
            aboutConfirmClear_ = false;
            if (!cleared) {
                aboutMessage_ = error.empty() ? "Falha ao limpar o cache" : error;
                return;
            }
            {
                std::lock_guard<std::mutex> lock(coverMutex_);
                coverQueue_.clear();
                queuedCoverIds_.clear();
                processedCoverIds_.clear();
            }
            aboutMessage_ = "Cache limpo; Favoritos e Minha lista foram preservados";
            status_ = "Cache local limpo";
            return;
        }

        if (input.back) {
            about_ = false;
            return;
        }
        if (input.left && aboutOption_ > 0) --aboutOption_;
        if (input.right && aboutOption_ < 2) ++aboutOption_;
        if (!input.accept) return;
        if (aboutOption_ == 0) {
            about_ = false;
            synchronizeCatalog();
        } else if (aboutOption_ == 1) {
            aboutConfirmClear_ = true;
            aboutMessage_.clear();
        } else {
            about_ = false;
        }
    }

    vitrine::BacklogStatus backlogStatus(const std::string& id) const {
        const auto found = backlogStatuses_.find(id);
        return found == backlogStatuses_.end() ? vitrine::BacklogStatus::None : found->second;
    }

    void setBacklogStatus(const vitrine::Game& source, vitrine::BacklogStatus newStatus) {
        const vitrine::Game sourceCopy = source;
        std::vector<vitrine::Game> entries = backlogCatalog_.all();
        const auto found = std::find_if(entries.begin(), entries.end(), [&sourceCopy](const vitrine::Game& item) {
            return item.id == sourceCopy.id;
        });
        if (newStatus == vitrine::BacklogStatus::None) {
            if (found != entries.end()) entries.erase(found);
            backlogStatuses_.erase(sourceCopy.id);
        } else {
            vitrine::Game updated = sourceCopy;
            updated.backlogStatus = newStatus;
            if (found == entries.end()) entries.push_back(std::move(updated));
            else *found = std::move(updated);
            backlogStatuses_[sourceCopy.id] = newStatus;
        }
        backlogCatalog_.replace(std::move(entries));
        if (!api_.saveBacklog(backlogCatalog_.all())) {
            status_ = "Nao foi possivel salvar a Minha lista";
        } else {
            status_ = newStatus == vitrine::BacklogStatus::None
                ? sourceCopy.title + " removido da Minha lista"
                : sourceCopy.title + " • " + vitrine::backlogStatusLabel(newStatus);
        }
        if (backlogTab_) refresh();
        visibleCoverSignature_.clear();
        queueVisibleCovers();
    }

    void openBacklogPanel(const vitrine::Game& game, bool fromDetails) {
        backlogPanelGame_ = game;
        backlogPanelFromDetails_ = fromDetails;
        backlogOption_ = static_cast<int>(backlogStatus(game.id));
        backlogPanel_ = true;
    }

    void handleBacklogPanel(const Input& input) {
        if (input.back || input.backlog) {
            backlogPanel_ = false;
            return;
        }
        if (input.up && backlogOption_ > 0) --backlogOption_;
        if (input.down && backlogOption_ < 4) ++backlogOption_;
        if (input.left && backlogOption_ > 0) --backlogOption_;
        if (input.right && backlogOption_ < 4) ++backlogOption_;
        if (!input.accept) return;
        setBacklogStatus(backlogPanelGame_, static_cast<vitrine::BacklogStatus>(backlogOption_));
        if (backlogPanelFromDetails_) detailGame_.backlogStatus = static_cast<vitrine::BacklogStatus>(backlogOption_);
        backlogPanel_ = false;
    }

    void openSelectedDetails() {
        if (games_.empty()) return;
        std::string coverError;
        const bool hasImage = classicView_ ? !games_[selected_]->imageUrl.empty() :
                                             !games_[selected_]->coverImageUrl.empty();
        const bool imageReady = classicView_ ? api_.ensureCover(*games_[selected_], coverError) :
                                               api_.ensurePortraitCover(*games_[selected_], coverError);
        if (hasImage && !imageReady) status_ = coverError;
        detailGameId_ = games_[selected_]->id;
        detailGame_ = *games_[selected_];
        detailGame_.backlogStatus = backlogStatus(detailGame_.id);
        detailScreenshots_.clear();
        screenshotIndex_ = 0;
        screenshotFullscreen_ = false;
        startScreenshotLoad(*games_[selected_]);
        detailTransitionStart_ = SDL_GetTicks();
        detailClosing_ = false;
        details_ = true;
    }

    void surpriseMe() {
        if (games_.empty()) {
            status_ = "Nenhum jogo disponivel para sortear";
            return;
        }
        surpriseSeed_ ^= surpriseSeed_ << 13;
        surpriseSeed_ ^= surpriseSeed_ >> 17;
        surpriseSeed_ ^= surpriseSeed_ << 5;
        int next = static_cast<int>(surpriseSeed_ % static_cast<std::uint32_t>(games_.size()));
        if (games_.size() > 1 && next == selected_) next = (next + 1) % static_cast<int>(games_.size());
        previousSelected_ = selected_;
        selected_ = next;
        selectionAnimationStart_ = SDL_GetTicks();
        queueVisibleCovers();
        status_ = "Surpresa: " + games_[selected_]->title;
        openSelectedDetails();
    }

    void toggleFavorite(const vitrine::Game& game) {
        const std::string id = game.id;
        const std::string title = game.title;
        std::vector<vitrine::Game> favorites = favoriteCatalog_.all();
        const auto found = std::find_if(favorites.begin(), favorites.end(), [&id](const vitrine::Game& item) {
            return item.id == id;
        });
        const bool removing = found != favorites.end();
        if (removing) {
            favorites.erase(found);
            favoriteIds_.erase(id);
        } else {
            favorites.push_back(game);
            favoriteIds_.insert(id);
        }
        favoriteCatalog_.replace(std::move(favorites));
        if (!api_.saveFavorites(favoriteCatalog_.all())) {
            status_ = "Nao foi possivel salvar os favoritos";
        } else {
            status_ = removing ? title + " removido dos favoritos" : title + " adicionado aos favoritos";
        }
        if (favoritesTab_) refresh();
        visibleCoverSignature_.clear();
        queueVisibleCovers();
    }

    int currentFilterOption() const {
        if (filterSection_ == 0) return genreIndex_;
        if (filterSection_ == 1) return highlightIndex_;
        if (filterSection_ == 3) return backlogFilterIndex_;
        return static_cast<int>(filter_.sort);
    }

    int filterOptionCount() const {
        if (filterSection_ == 0) return static_cast<int>(genres_.size());
        if (filterSection_ == 1) return 3;
        if (filterSection_ == 3) return 5;
        return 5;
    }

    int filterColumns() const {
        if (filterSection_ == 0) return 4;
        if (filterSection_ == 1) return 3;
        if (filterSection_ == 3) return 5;
        return 3;
    }

    void openFilterPanel(int section) {
        const int lastSection = backlogTab_ ? 3 : 2;
        filterSection_ = std::max(0, std::min(section, lastSection));
        filterOption_ = currentFilterOption();
        filterPanel_ = true;
    }

    void switchFilterSection(int direction) {
        const int sectionCount = backlogTab_ ? 4 : 3;
        filterSection_ = (filterSection_ + direction + sectionCount) % sectionCount;
        filterOption_ = currentFilterOption();
    }

    void handleFilterPanel(const Input& input) {
        if (input.back) {
            filterPanel_ = false;
            return;
        }
        if (input.previousGenre) switchFilterSection(-1);
        if (input.nextGenre) switchFilterSection(1);

        const int count = filterOptionCount();
        const int columns = filterColumns();
        if (input.left && filterOption_ % columns > 0) --filterOption_;
        if (input.right && filterOption_ % columns < columns - 1 && filterOption_ + 1 < count) ++filterOption_;
        if (input.up && filterOption_ >= columns) filterOption_ -= columns;
        if (input.down && filterOption_ + columns < count) filterOption_ += columns;

        if (!input.accept) return;
        filterPanel_ = false;
        if (filterSection_ == 0) {
            if (genreIndex_ != filterOption_) {
                genreIndex_ = filterOption_;
                if (favoritesTab_ || backlogTab_) refresh(); else loadCurrentFiltersFirstPage();
            }
        } else if (filterSection_ == 1) {
            if (highlightIndex_ != filterOption_) {
                highlightIndex_ = filterOption_;
                filter_.acclaimedOnly = highlightIndex_ == 1;
                filter_.upcomingOnly = highlightIndex_ == 2;
                if (favoritesTab_ || backlogTab_) refresh(); else loadCurrentFiltersFirstPage();
            }
        } else if (filterSection_ == 2) {
            if (static_cast<int>(filter_.sort) != filterOption_) {
                filter_.sort = static_cast<vitrine::SortMode>(filterOption_);
                updateDiscoverySourceOrdering();
                if (favoritesTab_ || backlogTab_) refresh(); else loadCurrentFiltersFirstPage();
            }
        } else {
            backlogFilterIndex_ = filterOption_;
            refresh();
        }
        visibleCoverSignature_.clear();
        queueVisibleCovers();
    }

    void queueVisibleCovers() {
        if (games_.empty()) return;
        const int columns = gridColumns();
        const int rows = gridRows();
        const int visibleCount = visibleGameCount();
        const int selectedRow = selected_ / columns;
        const int firstRow = std::max(0, selectedRow - (rows - 1));
        const int firstIndex = firstRow * columns;
        std::vector<vitrine::Game> visible;
        std::string signature = classicView_ ? "classic;" : "covers;";
        visible.reserve(visibleCount);
        for (int slot = 0; slot < visibleCount; ++slot) {
            const int index = firstIndex + slot;
            if (index >= static_cast<int>(games_.size())) break;
            const vitrine::Game& game = *games_[index];
            signature += game.id + ";";
            const std::string& imageUrl = classicView_ ? game.imageUrl : game.coverImageUrl;
            if (!imageUrl.empty()) visible.push_back(game);
        }
        if (signature == visibleCoverSignature_) return;
        visibleCoverSignature_ = signature;

        {
            std::lock_guard<std::mutex> lock(coverMutex_);
            coverQueue_.clear();
            queuedCoverIds_.clear();
            // O item selecionado vem primeiro; os demais seguem a ordem visual.
            if (selected_ >= firstIndex && selected_ < firstIndex + visibleCount) {
                const vitrine::Game& selectedGame = *games_[selected_];
                const bool portrait = !classicView_;
                const std::string& imageUrl = portrait ? selectedGame.coverImageUrl : selectedGame.imageUrl;
                const std::string requestId = std::string(portrait ? "poster:" : "backdrop:") + selectedGame.id;
                if (!imageUrl.empty() &&
                    processedCoverIds_.find(requestId) == processedCoverIds_.end() &&
                    requestId != inFlightCoverId_) {
                    coverQueue_.push_back({selectedGame, portrait});
                    queuedCoverIds_.insert(requestId);
                }
            }
            for (const vitrine::Game& game : visible) {
                const bool portrait = !classicView_;
                const std::string requestId = std::string(portrait ? "poster:" : "backdrop:") + game.id;
                if (processedCoverIds_.find(requestId) != processedCoverIds_.end() ||
                    queuedCoverIds_.find(requestId) != queuedCoverIds_.end() ||
                    requestId == inFlightCoverId_) {
                    continue;
                }
                coverQueue_.push_back({game, portrait});
                queuedCoverIds_.insert(requestId);
            }
        }
        coverCondition_.notify_one();
    }

    void coverWorkerLoop() {
        while (true) {
            CoverRequest request;
            std::string requestId;
            {
                std::unique_lock<std::mutex> lock(coverMutex_);
                coverCondition_.wait(lock, [this]() { return stopCoverWorker_ || !coverQueue_.empty(); });
                if (stopCoverWorker_) return;
                request = coverQueue_.front();
                coverQueue_.pop_front();
                requestId = std::string(request.portrait ? "poster:" : "backdrop:") + request.game.id;
                inFlightCoverId_ = requestId;
            }

            std::string ignoredError;
            const bool loaded = request.portrait ? api_.ensurePortraitCover(request.game, ignoredError) :
                                                   api_.ensureCover(request.game, ignoredError);
            {
                std::lock_guard<std::mutex> lock(coverMutex_);
                queuedCoverIds_.erase(requestId);
                if (loaded) processedCoverIds_.insert(requestId);
                if (inFlightCoverId_ == requestId) inFlightCoverId_.clear();
            }
        }
    }

    void rebuildGenres() {
        const std::string selectedGenre = genreIndex_ < static_cast<int>(genres_.size()) ? genres_[genreIndex_] : "Todos";
        if (usingApi_) {
            genres_ = {"Todos", "Acao", "Aventura", "Arcade", "Cartas", "Casual", "Corrida",
                       "Esporte", "Estrategia", "Familia", "Hack and Slash", "Indie", "Luta",
                       "Musica", "Pinball", "Plataforma", "Puzzle", "RPG", "Simulacao",
                       "Tatica", "Tiro", "Visual Novel"};
        } else {
            genres_ = catalog_.genres();
            genres_.insert(genres_.begin(), "Todos");
        }
        const auto found = std::find(genres_.begin(), genres_.end(), selectedGenre);
        genreIndex_ = found == genres_.end() ? 0 : static_cast<int>(found - genres_.begin());
    }

    std::string activeGenreSlug() const {
        if (genreIndex_ <= 0 || genreIndex_ >= static_cast<int>(genres_.size())) return {};
        const std::string& genre = genres_[genreIndex_];
        if (genre == "Acao") return "action";
        if (genre == "Aventura") return "adventure";
        if (genre == "Arcade") return "arcade";
        if (genre == "Cartas") return "card-and-board-game";
        if (genre == "Casual") return "casual";
        if (genre == "Corrida") return "racing";
        if (genre == "Esporte") return "sports";
        if (genre == "Estrategia") return "strategy";
        if (genre == "Familia") return "family";
        if (genre == "Hack and Slash") return "hack-and-slash-beat-em-up";
        if (genre == "Indie") return "indie";
        if (genre == "Luta") return "fighting";
        if (genre == "Musica") return "music";
        if (genre == "Pinball") return "pinball";
        if (genre == "Plataforma") return "platformer";
        if (genre == "Puzzle") return "puzzle";
        if (genre == "RPG") return "role-playing-games-rpg";
        if (genre == "Simulacao") return "simulation";
        if (genre == "Tatica") return "tactical";
        if (genre == "Tiro") return "shooter";
        if (genre == "Visual Novel") return "visual-novel";
        return {};
    }

    std::string activeOrderingSlug() const {
        switch (filter_.sort) {
            case vitrine::SortMode::Score: return "-metacritic";
            case vitrine::SortMode::Popular: return "-popular";
            case vitrine::SortMode::Title: return "name";
            case vitrine::SortMode::Shortest: return "-metacritic";
            case vitrine::SortMode::Release: return "-released";
        }
        return "-metacritic";
    }

    const char* discoveryLabel(int index) const {
        static const char* labels[] = {
            "Todos", "Populares", "Lancamentos", "Bem avaliados", "Indies", "Joias escondidas"
        };
        return labels[std::max(0, std::min(index, 5))];
    }

    std::string activeDiscoverySlug() const {
        static const char* slugs[] = {
            "", "popular", "releases", "top-rated", "indies", "hidden-gems"
        };
        return slugs[std::max(0, std::min(discoveryIndex_, 5))];
    }

    void updateDiscoverySourceOrdering() {
        filter_.preserveSourceOrder =
            ((discoveryIndex_ == 1 || discoveryIndex_ == 4) && filter_.sort == vitrine::SortMode::Popular) ||
            (discoveryIndex_ == 2 && filter_.sort == vitrine::SortMode::Release);
    }

    void captureDiscoveryReturnPoint() {
        discoveryReturnGames_ = catalog_.all();
        discoveryReturnFilter_ = filter_;
        discoveryReturnGenreIndex_ = genreIndex_;
        discoveryReturnHighlightIndex_ = highlightIndex_;
        discoveryReturnSelected_ = selected_;
        discoveryReturnPage_ = currentPage_;
        discoveryReturnHasMore_ = hasMore_;
        hasDiscoveryReturnPoint_ = true;
    }

    void restoreDiscoveryReturnPoint() {
        discoveryIndex_ = 0;
        discoveryCursor_ = 0;
        discoveryFocus_ = false;
        if (hasDiscoveryReturnPoint_) {
            catalog_.replace(std::move(discoveryReturnGames_));
            filter_ = discoveryReturnFilter_;
            genreIndex_ = discoveryReturnGenreIndex_;
            highlightIndex_ = discoveryReturnHighlightIndex_;
            selected_ = discoveryReturnSelected_;
            currentPage_ = discoveryReturnPage_;
            hasMore_ = discoveryReturnHasMore_;
            hasDiscoveryReturnPoint_ = false;
            refresh();
            status_ = "Catalogo completo restaurado";
        } else {
            loadCurrentFiltersFirstPage();
        }
        visibleCoverSignature_.clear();
        queueVisibleCovers();
        tabTransitionStart_ = SDL_GetTicks();
    }

    void applyDiscoverySection(int nextIndex) {
        nextIndex = std::max(0, std::min(nextIndex, 5));
        if (nextIndex == 0) {
            restoreDiscoveryReturnPoint();
            return;
        }
        if (nextIndex == discoveryIndex_) {
            discoveryFocus_ = false;
            return;
        }

        const int previousIndex = discoveryIndex_;
        const vitrine::SortMode previousSort = filter_.sort;
        const bool previousSourceOrder = filter_.preserveSourceOrder;
        if (discoveryIndex_ == 0) captureDiscoveryReturnPoint();
        discoveryIndex_ = nextIndex;
        discoveryCursor_ = nextIndex;
        filter_.sort = nextIndex == 1 || nextIndex == 4
            ? vitrine::SortMode::Popular
            : (nextIndex == 2 ? vitrine::SortMode::Release : vitrine::SortMode::Score);
        updateDiscoverySourceOrdering();
        status_ = "Carregando " + std::string(discoveryLabel(nextIndex)) + "...";
        const vitrine::ApiResult result = fetchPage(activeGenreSlug(), 1, filter_.query);
        if (!result.success) {
            discoveryIndex_ = previousIndex;
            discoveryCursor_ = previousIndex;
            filter_.sort = previousSort;
            filter_.preserveSourceOrder = previousSourceOrder;
            if (previousIndex == 0) hasDiscoveryReturnPoint_ = false;
            status_ = result.message.empty() ? "Secao indisponivel sem rede ou cache" : result.message;
            return;
        }
        catalog_.replace(result.games);
        selected_ = 0;
        previousSelected_ = -1;
        currentPage_ = 1;
        hasMore_ = result.hasMore;
        discoveryFocus_ = false;
        refresh();
        visibleCoverSignature_.clear();
        queueVisibleCovers();
        tabTransitionStart_ = SDL_GetTicks();
        status_ = std::string(discoveryLabel(nextIndex)) + " • " +
                  std::to_string(games_.size()) + (hasMore_ ? "+ jogos" : " jogos");
    }

    void handleDiscoveryRibbon(const Input& input) {
        if (input.left && discoveryCursor_ > 0) --discoveryCursor_;
        if (input.right && discoveryCursor_ < 5) ++discoveryCursor_;
        if (input.down) {
            discoveryFocus_ = false;
            status_ = discoveryIndex_ == 0 ? "Catalogo completo" : discoveryLabel(discoveryIndex_);
            return;
        }
        if (input.back) {
            if (discoveryIndex_ != 0) applyDiscoverySection(0);
            else discoveryFocus_ = false;
            return;
        }
        if (input.accept) applyDiscoverySection(discoveryCursor_);
    }

    std::string activeStatusParam() const {
        if (highlightIndex_ == 2) return "upcoming";
        return "";
    }

    int activeMinRating() const {
        if (highlightIndex_ == 1) return 80;
        return 0;
    }

    const char* highlightFilterLabel() const {
        static const char* labels[] = {"Todos", "Aclamados (80+)", "Lancamentos"};
        return labels[highlightIndex_];
    }

    void startInitialSync() {
        if (!networkReady_ || !apiInitialized_ || initialSyncRunning_) return;
        initialSyncRunning_ = true;
        initialSyncDone_.store(false, std::memory_order_release);
        status_ = usingApi_ ? "Catalogo salvo • atualizando..." : "Atualizando catalogo automaticamente...";
        initialSyncThread_ = std::thread([this]() {
            pendingInitialSync_ = api_.synchronize("", 1, "", "-metacritic", "", 0);
            initialSyncDone_.store(true, std::memory_order_release);
        });
    }

    void finishInitialSync() {
        if (!initialSyncRunning_ || !initialSyncDone_.load(std::memory_order_acquire)) return;
        if (initialSyncThread_.joinable()) initialSyncThread_.join();
        initialSyncRunning_ = false;

        const bool initialViewStillActive = !favoritesTab_ && !backlogTab_ && currentPage_ == 1 &&
            filter_.query.empty() && genreIndex_ == 0 && highlightIndex_ == 0 && discoveryIndex_ == 0 &&
            filter_.sort == vitrine::SortMode::Score;
        if (!pendingInitialSync_.success || !initialViewStillActive) {
            if (!pendingInitialSync_.success && !usingApi_) {
                status_ = pendingInitialSync_.message.empty()
                    ? "Offline • usando dados demonstrativos"
                    : pendingInitialSync_.message + " • usando dados demonstrativos";
            }
            return;
        }

        catalog_.replace(std::move(pendingInitialSync_.games));
        usingApi_ = true;
        currentPage_ = 1;
        hasMore_ = pendingInitialSync_.hasMore;
        rebuildGenres();
        refresh();
        startGridReveal();
        visibleCoverSignature_.clear();
        queueVisibleCovers();
        status_ = "Catalogo atualizado automaticamente";
    }

    void synchronizeCatalog() {
        if (initialSyncRunning_) {
            status_ = "Atualizacao automatica em andamento";
            return;
        }
        if (!networkReady_ || !apiInitialized_) {
            status_ = "Rede indisponivel; o cache continua ativo";
            return;
        }
        status_ = "Atualizando catalogo...";
        const vitrine::ApiResult result = api_.synchronize(activeGenreSlug(), 1, filter_.query,
                                                          activeOrderingSlug(), activeStatusParam(),
                                                          activeMinRating(), "", activeDiscoverySlug());
        status_ = result.message;
        if (!result.success) return;
        catalog_.replace(result.games);
        usingApi_ = true;
        selected_ = 0;
        currentPage_ = 1;
        hasMore_ = result.hasMore;
        rebuildGenres();
        refresh();
    }

    vitrine::ApiResult fetchPage(const std::string& genreSlug, int page,
                                 const std::string& query) {
        vitrine::ApiResult result;
        const std::string ordering = activeOrderingSlug();
        const std::string status = activeStatusParam();
        const int minRating = activeMinRating();
        if (networkReady_ && apiInitialized_) {
            result = api_.synchronize(genreSlug, page, query, ordering, status, minRating,
                                      "", activeDiscoverySlug());
        }
        if (!result.success) {
            const vitrine::ApiResult cached = api_.loadCache(genreSlug, page, query, ordering, status, minRating,
                                                             "", activeDiscoverySlug());
            if (cached.success) return cached;
        }
        return result;
    }

    void loadCurrentFiltersFirstPage() {
        if (!usingApi_) {
            refresh();
            return;
        }
        const std::string slug = activeGenreSlug();
        const vitrine::ApiResult result = fetchPage(slug, 1, filter_.query);
        if (!result.success) {
            status_ = result.message.empty() ? "Filtro indisponivel sem rede ou cache" : result.message;
            refresh();
            return;
        }
        catalog_.replace(result.games);
        selected_ = 0;
        currentPage_ = 1;
        hasMore_ = result.hasMore;
        status_ = "Catalogo atualizado • pagina 1";
        refresh();
    }

    void loadGenreFirstPage() {
        loadCurrentFiltersFirstPage();
    }

    void loadSimilarGames(const vitrine::Game& game) {
        if (game.id.rfind("igdb-", 0) != 0) return;
        std::vector<vitrine::Game> similar;
        std::string error;
        status_ = "Buscando jogos semelhantes...";
        if (!api_.fetchSimilarGames(game, similar, error) || similar.empty()) {
            status_ = error.empty() ? "Nenhum jogo semelhante encontrado" : error;
            return;
        }
        catalog_.replace(std::move(similar));
        usingApi_ = true;
        details_ = false;
        selected_ = 0;
        currentPage_ = 1;
        hasMore_ = false;
        favoritesTab_ = false;
        backlogTab_ = false;
        discoveryIndex_ = 0;
        discoveryCursor_ = 0;
        filter_.preserveSourceOrder = false;
        hasDiscoveryReturnPoint_ = false;
        discoveryReturnGames_.clear();
        status_ = "Semelhantes a " + game.title;
        refresh();
        visibleCoverSignature_.clear();
        queueVisibleCovers();
    }

    void loadNextPage() {
        if (!usingApi_ || !hasMore_) return;
        const int nextPage = currentPage_ + 1;
        const vitrine::ApiResult result = fetchPage(activeGenreSlug(), nextPage, filter_.query);
        if (!result.success) {
            status_ = result.message.empty() ? "Nao foi possivel carregar a proxima pagina" : result.message;
            return;
        }

        std::vector<vitrine::Game> combined = catalog_.all();
        for (const vitrine::Game& incoming : result.games) {
            const bool duplicate = std::any_of(combined.begin(), combined.end(), [&incoming](const vitrine::Game& existing) {
                return existing.id == incoming.id;
            });
            if (!duplicate) combined.push_back(incoming);
        }
        catalog_.replace(std::move(combined));
        currentPage_ = nextPage;
        hasMore_ = result.hasMore;
        status_ = "Pagina " + std::to_string(currentPage_) + " carregada • " +
                  std::to_string(catalog_.all().size()) + " jogos";
        refresh();
    }

    void loadSearchFirstPage() {
        if (favoritesTab_ || backlogTab_) {
            refresh();
            status_ = backlogTab_ ? "Busca na Minha lista" :
                      (filter_.query.empty() ? "Aba Favoritos" : "Busca nos favoritos");
            return;
        }

        status_ = filter_.query.empty() ? "Carregando catalogo..." : "Pesquisando na IGDB...";
        const vitrine::ApiResult result = fetchPage(activeGenreSlug(), 1, filter_.query);
        if (!result.success) {
            status_ = result.message.empty() ? "Busca indisponivel sem rede ou cache" : result.message;
            refresh();
            return;
        }

        catalog_.replace(result.games);
        usingApi_ = true;
        selected_ = 0;
        currentPage_ = 1;
        hasMore_ = result.hasMore;
        status_ = filter_.query.empty()
                      ? "Catalogo completo carregado"
                      : "Busca por " + filter_.query + " • " +
                            std::to_string(result.games.size()) +
                            (result.hasMore ? "+ jogos" : " jogos");
        refresh();
    }

    void startScreenshotLoad(const vitrine::Game& game) {
        if (game.id.rfind("igdb-", 0) != 0) return;
        if (screenshotLoading_) {
            screenshotQueued_ = true;
            return;
        }
        if (screenshotThread_.joinable()) screenshotThread_.join();
        screenshotLoading_ = true;
        screenshotQueued_ = false;
        screenshotDone_.store(false, std::memory_order_release);
        pendingScreenshotPaths_.clear();
        pendingScreenshotError_.clear();
        pendingDetailError_.clear();
        pendingDetailLoaded_ = false;
        pendingDetailGame_ = game;
        pendingScreenshotGameId_ = game.id;
        const vitrine::Game gameCopy = game;
        screenshotThread_ = std::thread([this, gameCopy]() {
            pendingDetailLoaded_ = api_.fetchDetails(gameCopy, pendingDetailGame_, pendingDetailError_);
            api_.ensureScreenshots(gameCopy, pendingScreenshotPaths_, pendingScreenshotError_);
            screenshotDone_.store(true, std::memory_order_release);
        });
    }

    void finishScreenshotLoad() {
        if (!screenshotLoading_ || !screenshotDone_.load(std::memory_order_acquire)) return;
        if (screenshotThread_.joinable()) screenshotThread_.join();
        screenshotLoading_ = false;
        if (pendingScreenshotGameId_ == detailGameId_) {
            detailGame_ = pendingDetailGame_;
            detailScreenshots_ = pendingScreenshotPaths_;
            screenshotIndex_ = 0;
            if (pendingDetailLoaded_) {
                const auto enrich = [this](vitrine::Game& item) {
                    item.mainHours = detailGame_.mainHours;
                    item.completionHours = detailGame_.completionHours;
                    item.averagePlaytime = detailGame_.averagePlaytime;
                    item.description = detailGame_.description;
                    item.tagline = detailGame_.tagline;
                    item.studio = detailGame_.studio;
                    item.publisher = detailGame_.publisher;
                    item.releaseDate = detailGame_.releaseDate;
                    item.ageRating = detailGame_.ageRating;
                    item.website = detailGame_.website;
                    item.ratingsCount = detailGame_.ratingsCount;
                    item.themes = detailGame_.themes;
                    item.gameModes = detailGame_.gameModes;
                    item.perspectives = detailGame_.perspectives;
                    item.franchise = detailGame_.franchise;
                    item.videosCount = detailGame_.videosCount;
                };
                std::vector<vitrine::Game> all = catalog_.all();
                for (auto& item : all) {
                    if (item.id == detailGame_.id) {
                        enrich(item);
                        break;
                    }
                }
                catalog_.replace(std::move(all));

                std::vector<vitrine::Game> favs = favoriteCatalog_.all();
                for (auto& item : favs) {
                    if (item.id == detailGame_.id) {
                        enrich(item);
                        break;
                    }
                }
                favoriteCatalog_.replace(std::move(favs));

                std::vector<vitrine::Game> backlog = backlogCatalog_.all();
                for (auto& item : backlog) {
                    if (item.id == detailGame_.id) {
                        enrich(item);
                        break;
                    }
                }
                backlogCatalog_.replace(std::move(backlog));
                api_.saveBacklog(backlogCatalog_.all());
                refresh();
            }
            if (pendingDetailLoaded_ && !detailScreenshots_.empty()) {
                status_ = "Detalhes e " + std::to_string(detailScreenshots_.size()) +
                          " screenshots carregados";
            } else if (pendingDetailLoaded_) {
                status_ = "Detalhes completos carregados";
            } else if (!pendingDetailError_.empty()) {
                status_ = pendingDetailError_;
            } else if (!pendingScreenshotError_.empty()) {
                status_ = pendingScreenshotError_;
            }
        }
        pendingScreenshotPaths_.clear();
        pendingScreenshotError_.clear();
        pendingDetailError_.clear();
        if (screenshotQueued_ && details_) {
            screenshotQueued_ = false;
            startScreenshotLoad(detailGame_);
        }
    }

    void refresh() {
        filter_.genre = genreIndex_ == 0 ? "" : genres_[genreIndex_];
        const vitrine::Catalog& source = backlogTab_ ? backlogCatalog_ :
                                         (favoritesTab_ ? favoriteCatalog_ : catalog_);
        games_ = source.filtered(filter_);
        if (backlogTab_ && backlogFilterIndex_ > 0) {
            const auto expected = static_cast<vitrine::BacklogStatus>(backlogFilterIndex_);
            games_.erase(std::remove_if(games_.begin(), games_.end(), [expected](const vitrine::Game* game) {
                return game->backlogStatus != expected;
            }), games_.end());
        }
        selected_ = std::max(0, std::min(selected_, static_cast<int>(games_.size()) - 1));
    }

    const char* backlogFilterLabel() const {
        static const char* labels[] = {"Todos", "Quero jogar", "Jogando", "Finalizados", "Abandonados"};
        return labels[backlogFilterIndex_];
    }

    void resetFiltersForSearch() {
        genreIndex_ = 0;
        highlightIndex_ = 0;
        backlogFilterIndex_ = 0;
        filter_.genre.clear();
        filter_.acclaimedOnly = false;
        filter_.upcomingOnly = false;
        filter_.sort = vitrine::SortMode::Score;
        filter_.preserveSourceOrder = false;
        discoveryIndex_ = 0;
        discoveryCursor_ = 0;
        discoveryFocus_ = false;
        hasDiscoveryReturnPoint_ = false;
        discoveryReturnGames_.clear();
    }

    void openSearch() {
#ifdef __SWITCH__
        SwkbdConfig keyboard;
        char output[96] = {};
        if (R_FAILED(swkbdCreate(&keyboard, 0))) return;
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, "Pesquisar na Vitrine");
        swkbdConfigSetGuideText(&keyboard, "Titulo, estudio ou genero");
        swkbdConfigSetOkButtonText(&keyboard, "Buscar");
        swkbdConfigSetStringLenMax(&keyboard, 80);
        if (!filter_.query.empty()) swkbdConfigSetInitialText(&keyboard, filter_.query.c_str());
        if (R_SUCCEEDED(swkbdShow(&keyboard, output, sizeof(output)))) {
            resetFiltersForSearch();
            filter_.query = output;
            loadSearchFirstPage();
        }
        swkbdClose(&keyboard);
#else
        desktopTyping_ = !desktopTyping_;
        if (desktopTyping_) {
            resetFiltersForSearch();
            refresh();
            SDL_StartTextInput();
        } else {
            SDL_StopTextInput();
            loadSearchFirstPage();
        }
#endif
    }

    void renderBackground(SDL_Renderer* renderer) {
        for (int y = 0; y < kHeight; ++y) {
            const float t = static_cast<float>(y) / kHeight;
            setColor(renderer, color(static_cast<Uint8>(12 + 6 * t), static_cast<Uint8>(17 + 5 * t), static_cast<Uint8>(30 + 10 * t)));
            SDL_RenderDrawLine(renderer, 0, y, kWidth, y);
        }
        setColor(renderer, color(42, 82, 190, 22));
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < 160; ++i) SDL_RenderDrawLine(renderer, 760 - i, 0, 1280, 520 - i);
    }

    std::string filterOptionLabel(int index) const {
        if (filterSection_ == 0) return genres_[index];
        if (filterSection_ == 1) {
            static const char* highlights[] = {"Todos", "Aclamados (80+)", "Lancamentos"};
            return highlights[index];
        }
        if (filterSection_ == 3) {
            static const char* statuses[] = {"Todos", "Quero jogar", "Jogando", "Finalizados", "Abandonados"};
            return statuses[index];
        }
        static const char* sorts[] = {"Maior score", "Mais populares", "A-Z", "Mais curtos", "Lancamento"};
        return sorts[index];
    }

    void renderFilterPanel(SDL_Renderer* renderer, TextRenderer& text) {
        fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 6, 13, 218));
        fillRoundedRect(renderer, 82, 60, 1116, 600, 24, color(16, 22, 37));
        text.draw(renderer, "Filtros e ordenacao", 122, 89, 28, color(244, 246, 252));
        text.draw(renderer, "Escolha diretamente; a API so atualiza ao aplicar.", 122, 126, 18, color(132, 145, 174));

        static const char* tabs[] = {"Genero", "Destaque", "Ordenar", "Status"};
        const int tabCount = backlogTab_ ? 4 : 3;
        for (int tab = 0; tab < tabCount; ++tab) {
            const int tabWidth = backlogTab_ ? 247 : 334;
            const int gap = 16;
            const int x = 122 + tab * (tabWidth + gap);
            const bool active = tab == filterSection_;
            fillRoundedRect(renderer, x, 164, tabWidth, 52, 14, active ? color(75, 91, 205) : color(25, 33, 51));
            const int labelWidth = text.width(tabs[tab], 22);
            text.draw(renderer, tabs[tab], x + (tabWidth - labelWidth) / 2, 178, 22,
                      active ? color(247, 248, 253) : color(145, 156, 181));
        }

        const int count = filterOptionCount();
        const int columns = filterColumns();
        const int gap = 16;
        const int areaWidth = 1036;
        const int optionWidth = (areaWidth - gap * (columns - 1)) / columns;
        const int optionHeight = filterSection_ == 0 ? 52 : 72;
        const int startY = filterSection_ == 0 ? 232 : 282;
        const int currentApplied = currentFilterOption();
        for (int index = 0; index < count; ++index) {
            const int column = index % columns;
            const int row = index / columns;
            const int x = 122 + column * (optionWidth + gap);
            const int y = startY + row * (optionHeight + (filterSection_ == 0 ? 10 : 16));
            const bool focused = index == filterOption_;
            const bool applied = index == currentApplied;
            if (focused) fillRoundedRect(renderer, x - 4, y - 4, optionWidth + 8, optionHeight + 8, 15, color(116, 133, 255));
            fillRoundedRect(renderer, x, y, optionWidth, optionHeight, 12,
                            focused ? color(38, 48, 76) : color(23, 30, 47));
            if (applied) fillRoundedRect(renderer, x + 14, y + optionHeight / 2 - 5, 10, 10, 5, color(112, 224, 172));
            const std::string label = filterOptionLabel(index);
            text.draw(renderer, label, x + (applied ? 34 : 18), y + optionHeight / 2 - 12,
                      filterSection_ == 0 ? 18 : 22,
                      focused ? color(246, 248, 253) : color(190, 199, 218), optionWidth - 48);
        }

        text.draw(renderer, "L / R  Categoria", 122, 620, 18, color(151, 167, 255));
        text.draw(renderer, "A  Aplicar", 958, 620, 18, color(214, 220, 234));
        text.draw(renderer, "B  Voltar", 1082, 620, 18, color(160, 172, 196));
    }

    void renderBacklogPanel(SDL_Renderer* renderer, TextRenderer& text) {
        fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 6, 13, 205));
        fillRoundedRect(renderer, 320, 112, 640, 496, 24, color(16, 22, 37));
        text.draw(renderer, "Minha lista", 360, 145, 28, color(244, 246, 252));
        text.draw(renderer, backlogPanelGame_.title, 360, 184, 18, color(151, 167, 255), 550);
        text.draw(renderer, "Escolha o estado deste jogo", 360, 217, 18, color(132, 145, 174));

        for (int option = 0; option < 5; ++option) {
            const int y = 254 + option * 62;
            const bool focused = option == backlogOption_;
            if (focused) fillRoundedRect(renderer, 352, y - 4, 576, 56, 14, color(112, 130, 255));
            fillRoundedRect(renderer, 356, y, 568, 48, 11,
                            focused ? color(38, 48, 76) : color(24, 31, 49));
            const auto value = static_cast<vitrine::BacklogStatus>(option);
            const char* optionLabel = option == 0 && backlogStatus(backlogPanelGame_.id) != vitrine::BacklogStatus::None
                ? "Remover da lista"
                : vitrine::backlogStatusLabel(value);
            text.draw(renderer, optionLabel, 380, y + 11, 18,
                      focused ? color(246, 248, 253) : color(187, 197, 217));
            if (backlogStatus(backlogPanelGame_.id) == value) {
                fillRoundedRect(renderer, 885, y + 17, 10, 10, 5, color(112, 224, 172));
            }
        }
        text.draw(renderer, "A  Aplicar", 360, 570, 18, color(224, 230, 246));
        text.draw(renderer, "B / ZR  Voltar", 754, 570, 18, color(160, 172, 196));
    }

    void renderAbout(SDL_Renderer* renderer, TextRenderer& text) {
        fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 6, 13, 224));
        fillRoundedRect(renderer, 130, 48, 1020, 624, 26, color(16, 22, 37));
        fillRoundedRect(renderer, 130, 48, 7, 624, 3, color(112, 130, 255));

        text.draw(renderer, "VITRINE", 178, 82, 42, color(246, 248, 252));
        text.draw(renderer, "Sobre o aplicativo", 180, 132, 18, color(132, 145, 174));
        fillRoundedRect(renderer, 972, 82, 126, 42, 13, color(46, 60, 108));
        const std::string version = "v" + std::string(kAppVersion);
        const int versionWidth = text.width(version, 22);
        text.draw(renderer, version, 972 + (126 - versionWidth) / 2, 91, 22, color(227, 232, 250));

        fillRoundedRect(renderer, 178, 178, 452, 72, 15, color(23, 30, 48));
        text.draw(renderer, "CRIADO POR", 198, 190, 18, color(113, 129, 166));
        text.draw(renderer, kAppAuthor, 198, 218, 22, color(239, 242, 250));
        fillRoundedRect(renderer, 650, 178, 448, 72, 15, color(23, 30, 48));
        text.draw(renderer, "CONEXAO", 670, 190, 18, color(113, 129, 166));
        const std::string connection = networkReady_ && apiInitialized_
            ? (initialSyncRunning_ ? "Rede disponivel • atualizando" : "Rede disponivel")
            : "Offline • usando dados locais";
        text.draw(renderer, connection, 670, 218, 22,
                  networkReady_ && apiInitialized_ ? color(116, 235, 181) : color(216, 178, 118), 400);

        fillRoundedRect(renderer, 178, 266, 452, 72, 15, color(23, 30, 48));
        text.draw(renderer, "FONTE DOS DADOS", 198, 278, 18, color(113, 129, 166));
        text.draw(renderer, usingApi_ ? "IGDB via Cloudflare Worker" : "Catalogo demonstrativo",
                  198, 306, 22, color(225, 230, 244), 400);
        fillRoundedRect(renderer, 650, 266, 448, 72, 15, color(23, 30, 48));
        text.draw(renderer, "CACHE LOCAL", 670, 278, 18, color(113, 129, 166));
        text.draw(renderer, formatCacheSize(aboutCacheBytes_), 670, 306, 22, color(225, 230, 244));

        text.draw(renderer, "ACOES", 178, 365, 18, color(113, 129, 166));
        static const char* titles[] = {"Atualizar dados", "Limpar cache", "Fechar"};
        static const char* subtitles[] = {
            "Consulta o catalogo agora", "Capas, detalhes e catalogo", "Voltar para a vitrine"
        };
        for (int option = 0; option < 3; ++option) {
            const int x = 178 + option * 308;
            const bool focused = option == aboutOption_;
            if (focused) fillRoundedRect(renderer, x - 4, 394, 292, 108, 18, color(112, 130, 255));
            fillRoundedRect(renderer, x, 398, 284, 100, 14,
                            focused ? color(38, 48, 76) : color(23, 30, 48));
            text.draw(renderer, titles[option], x + 20, 418, 22,
                      focused ? color(246, 248, 253) : color(205, 213, 230), 244);
            text.draw(renderer, subtitles[option], x + 20, 454, 18, color(126, 140, 171), 244);
        }

        text.draw(renderer, aboutMessage_, 178, 528, 18, color(151, 167, 255), 920);
        text.draw(renderer, "Favoritos e Minha lista nao fazem parte do cache e nunca sao apagados.",
                  178, 563, 18, color(130, 143, 170), 920);
        text.draw(renderer, "Esquerda / Direita  Escolher", 178, 626, 18, color(151, 167, 255));
        text.draw(renderer, "A  Confirmar", 828, 626, 18, color(224, 230, 246));
        text.draw(renderer, "B  Voltar", 990, 626, 18, color(178, 189, 211));

        if (!aboutConfirmClear_) return;
        fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 6, 13, 174));
        fillRoundedRect(renderer, 300, 220, 680, 280, 24, color(22, 29, 47));
        fillRoundedRect(renderer, 300, 220, 6, 280, 3, color(218, 151, 86));
        text.draw(renderer, "Limpar o cache?", 350, 263, 28, color(246, 248, 252));
        text.drawWrapped(renderer,
                         "Capas, screenshots, detalhes e paginas do catalogo serao removidos. "
                         "Eles poderao ser baixados novamente quando necessario.",
                         350, 312, 22, color(188, 198, 219), 580, 3);
        text.draw(renderer, "Favoritos e Minha lista serao preservados.",
                  350, 405, 18, color(116, 235, 181));
        text.draw(renderer, "A  Limpar agora", 350, 454, 18, color(232, 210, 187));
        text.draw(renderer, "B  Cancelar", 800, 454, 18, color(183, 193, 214));
    }

    void renderHeader(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images) {
        fillRect(renderer, 0, 0, kWidth, 92, color(10, 14, 25, 232));
#ifdef __SWITCH__
        const std::string logoPath = "romfs:/logo.png";
#else
        const std::string logoPath = "logo.png";
#endif
        if (!images.drawCover(renderer, logoPath, 42, 25, 42, 42)) {
            fillRoundedRect(renderer, 42, 25, 42, 42, 12, color(81, 105, 246));
            fillRoundedRect(renderer, 53, 36, 20, 20, 6, color(198, 210, 255));
        }
        text.draw(renderer, "VITRINE", 98, 25, 28, color(242, 245, 255));
        text.draw(renderer, "descubra seu proximo jogo", 98, 55, 18, color(125, 137, 165));

        fillRoundedRect(renderer, 702, 23, 390, 48, 16, color(27, 34, 52));
        text.draw(renderer, "Y", 721, 35, 18, color(147, 164, 255));
        const std::string searchText = filter_.query.empty() ? "Pesquisar jogos..." : filter_.query;
        text.draw(renderer, searchText, 755, 34, 18,
                  filter_.query.empty() ? color(112, 122, 145) : color(231, 235, 245), 315);
        fillRoundedRect(renderer, 1110, 23, 126, 48, 16, color(27, 34, 52));
        text.draw(renderer, "X  Filtros", 1127, 35, 18, color(185, 194, 215));
    }

    void renderToolbar(SDL_Renderer* renderer, TextRenderer& text) {
        fillRoundedRect(renderer, 42, 103, 98, 42, 13,
                        (!backlogTab_ && !favoritesTab_) ? color(66, 82, 190) : color(26, 33, 51));
        fillRoundedRect(renderer, 148, 103, 130, 42, 13,
                        backlogTab_ ? color(66, 82, 190) : color(26, 33, 51));
        fillRoundedRect(renderer, 286, 103, 112, 42, 13,
                        favoritesTab_ ? color(66, 82, 190) : color(26, 33, 51));
        text.draw(renderer, "Explorar", 55, 112, 18,
                  (!backlogTab_ && !favoritesTab_) ? color(244, 246, 252) : color(154, 166, 191));
        text.draw(renderer, "Minha lista", 163, 112, 18,
                  backlogTab_ ? color(244, 246, 252) : color(154, 166, 191));
        text.draw(renderer, "Favoritos", 298, 112, 18,
                  favoritesTab_ ? color(244, 246, 252) : color(154, 166, 191));
        const std::string count = std::to_string(games_.size()) +
                                  (hasMore_ && usingApi_ && !favoritesTab_ && !backlogTab_ ? "+" : "") +
                                  (games_.size() == 1 ? " item" : " itens");
        text.draw(renderer, count, 408, 116, 18, color(119, 131, 157), 70);
        fillRoundedRect(renderer, 484, 108, 2, 32, 1, color(47, 57, 78));

        drawFilterChip(renderer, text, 496, 103, 220, "Genero", genres_[genreIndex_]);
        drawFilterChip(renderer, text, 724, 103, 220, backlogTab_ ? "Status" : "Destaque",
                       backlogTab_ ? backlogFilterLabel() : highlightFilterLabel());
        drawFilterChip(renderer, text, 952, 103, 286, "Ordenar", vitrine::sortModeLabel(filter_.sort));
    }

    void drawFilterChip(SDL_Renderer* renderer, TextRenderer& text, int x, int y, int w,
                        const std::string& title, const std::string& value) {
        fillRoundedRect(renderer, x, y, w, 42, 13, color(26, 33, 51));
        text.draw(renderer, title, x + 13, y + 11, 18, color(126, 139, 167));
        const int valueX = x + 22 + text.width(title, 18);
        text.draw(renderer, value, valueX, y + 11, 18, color(214, 220, 234), x + w - valueX - 27);
        const int chevronX = x + w - 16;
        const int chevronY = y + 20;
        setColor(renderer, color(151, 167, 255));
        for (int thickness = 0; thickness < 2; ++thickness) {
            SDL_RenderDrawLine(renderer, chevronX - 5, chevronY - 2 + thickness,
                              chevronX, chevronY + 3 + thickness);
            SDL_RenderDrawLine(renderer, chevronX, chevronY + 3 + thickness,
                              chevronX + 5, chevronY - 2 + thickness);
        }
    }

    void renderDiscoveryRibbon(SDL_Renderer* renderer, TextRenderer& text) {
        if (backlogTab_ || favoritesTab_) return;
        text.draw(renderer, "DESCOBRIR", 42, 158, 18, color(112, 126, 157));
        static const int widths[] = {76, 112, 132, 158, 82, 164};
        int x = 150;
        for (int index = 0; index < 6; ++index) {
            const bool active = index == discoveryIndex_;
            const bool focused = discoveryFocus_ && index == discoveryCursor_;
            if (focused) {
                fillRoundedRect(renderer, x - 3, 151, widths[index] + 6, 34, 12,
                                color(116, 133, 255));
            }
            fillRoundedRect(renderer, x, 154, widths[index], 28, 9,
                            active ? color(66, 82, 190) :
                            (focused ? color(37, 47, 74) : color(23, 30, 47)));
            const int labelWidth = text.width(discoveryLabel(index), 18);
            text.draw(renderer, discoveryLabel(index), x + (widths[index] - labelWidth) / 2, 157, 18,
                      active || focused ? color(244, 246, 252) : color(154, 166, 191));
            x += widths[index] + 8;
        }
        if (discoveryFocus_) {
            text.draw(renderer, "A  Abrir", 1052, 157, 18, color(205, 213, 231));
            text.draw(renderer, "Baixo  Jogos", 1135, 157, 18, color(137, 151, 181));
        } else {
            text.draw(renderer, "Cima  Navegar", 1112, 157, 18, color(137, 151, 181));
        }
    }

    void renderGrid(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images) {
        if (games_.empty()) {
            text.draw(renderer, backlogTab_ ? "Sua lista esta vazia" :
                      (favoritesTab_ ? "Nenhum jogo favorito" : "Nenhum item encontrado"),
                      450, 300, 28, color(231, 235, 245));
            text.draw(renderer, backlogTab_ ? "Na aba Explorar, use ZR para adicionar jogos." :
                      (favoritesTab_ ? "Na aba Explorar, use L3 para adicionar jogos." :
                                       "Tente outro termo ou altere os filtros."),
                      443, 344, 18, color(126, 137, 160));
            return;
        }

        const int columns = gridColumns();
        const int rows = gridRows();
        const int visibleCount = visibleGameCount();
        const int selectedRow = selected_ / columns;
        const int firstRow = std::max(0, selectedRow - (rows - 1));
        const int firstIndex = firstRow * columns;
        const bool discoveryVisible = !backlogTab_ && !favoritesTab_;
        const int gridY = discoveryVisible ? 190 : 160;
        const int classicRowStride = discoveryVisible ? 236 : 244;
        int renderedGames = 0;
        for (int slot = 0; slot < visibleCount; ++slot) {
            const int index = firstIndex + slot;
            if (index >= static_cast<int>(games_.size())) break;
            const int column = slot % columns;
            const int row = slot / columns;
            const float reveal = gridRevealProgress(slot);
            if (classicView_) {
                drawCard(renderer, text, images, *games_[index],
                         42 + column * 307, gridY + row * classicRowStride, index == selected_, reveal);
            } else {
                const float focus = selectionFocus(index);
                drawCoverCard(renderer, text, images, *games_[index],
                              42 + column * 244, gridY + row * 372, focus, reveal);
            }
            ++renderedGames;
        }
        if (!classicView_) {
            drawSelectedSummary(renderer, text, *games_[selected_], gridRevealProgress(renderedGames));
        }
    }

    float selectionFocus(int index) const {
        if (previousSelected_ < 0 || selectionAnimationStart_ == 0) return index == selected_ ? 1.0f : 0.0f;
        const float elapsed = static_cast<float>(SDL_GetTicks() - selectionAnimationStart_);
        const float linear = std::max(0.0f, std::min(1.0f, elapsed / 170.0f));
        const float eased = linear * linear * (3.0f - 2.0f * linear);
        if (index == selected_) return eased;
        if (index == previousSelected_) return 1.0f - eased;
        return 0.0f;
    }

    void drawBacklogBadge(SDL_Renderer* renderer, TextRenderer& text,
                          const vitrine::Game& game, int right, int y) {
        const vitrine::BacklogStatus value = backlogStatus(game.id);
        if (value == vitrine::BacklogStatus::None) return;
        SDL_Color background = color(48, 74, 142, 232);
        std::string label = "QUERO";
        if (value == vitrine::BacklogStatus::Playing) {
            background = color(132, 93, 34, 232);
            label = "JOGANDO";
        } else if (value == vitrine::BacklogStatus::Completed) {
            background = color(35, 105, 76, 232);
            label = "FEITO";
        } else if (value == vitrine::BacklogStatus::Dropped) {
            background = color(79, 68, 91, 232);
            label = "PAROU";
        }
        const int width = text.width(label, 18) + 18;
        fillRoundedRect(renderer, right - width, y, width, 27, 8, background);
        text.draw(renderer, label, right - width + 9, y + 4, 18, color(241, 244, 250));
    }

    void drawCoverCard(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                       const vitrine::Game& game, int x, int y, float focus, float reveal) {
        if (reveal <= 0.01f) return;
        y += static_cast<int>(std::round(28.0f * (1.0f - reveal)));
        const int cardX = x - static_cast<int>(std::round(4.0f * focus));
        const int cardY = y - static_cast<int>(std::round(6.0f * focus));
        const int cardWidth = 220 + static_cast<int>(std::round(8.0f * focus));
        const int cardHeight = 364 + static_cast<int>(std::round(12.0f * focus));
        const int coverX = cardX + 8;
        const int coverY = cardY + 8;
        const int coverWidth = cardWidth - 16;
        const int coverHeight = 288 + static_cast<int>(std::round(10.0f * focus));

        if (focus > 0.01f) {
            fillRoundedRect(renderer, cardX - 5, cardY - 5, cardWidth + 10, cardHeight + 10,
                            18, color(112, 130, 255, static_cast<Uint8>(255.0f * focus)));
        }
        fillRoundedRect(renderer, cardX, cardY, cardWidth, cardHeight, 13,
                        color(static_cast<Uint8>(20 + 5 * focus),
                              static_cast<Uint8>(27 + 6 * focus),
                              static_cast<Uint8>(43 + 10 * focus)));
        gradientRect(renderer, coverX, coverY, coverWidth, coverHeight, game.coverTop, game.coverBottom);
        const bool hasImage = images.drawCover(renderer, game.localCoverImagePath,
                                               coverX, coverY, coverWidth, coverHeight);
        if (hasImage && focus < 0.99f) {
            fillRect(renderer, coverX, coverY, coverWidth, coverHeight,
                     color(5, 8, 15, static_cast<Uint8>(38.0f * (1.0f - focus))));
        }

        if (isFavorite(game.id)) {
            fillRoundedRect(renderer, coverX + 10, coverY + 10, 32, 32, 16, color(87, 65, 145, 230));
            text.draw(renderer, "♥", coverX + 17, coverY + 14, 18, color(242, 222, 255));
        }
        drawBacklogBadge(renderer, text, game, coverX + coverWidth - 10, coverY + 10);

        if (!hasImage) {
            const int iconX = coverX + (coverWidth - 72) / 2;
            const int iconY = coverY + (coverHeight - 72) / 2;
            fillRoundedRect(renderer, iconX, iconY, 72, 72, 36, color(255, 255, 255, 35));
            setColor(renderer, color(255, 255, 255, 58));
            SDL_RenderDrawLine(renderer, iconX + 8, iconY + 63, iconX + 64, iconY + 8);
            SDL_RenderDrawLine(renderer, iconX + 22, iconY + 9, iconX + 74, iconY + 61);
        }

        const int titleY = coverY + coverHeight + 10;
        text.draw(renderer, game.title, cardX + 10, titleY, 18, color(242, 245, 252), cardWidth - 20);
        const std::string year = game.releaseYear > 0 ? std::to_string(game.releaseYear) : "----";
        text.draw(renderer, year, cardX + 10, titleY + 31, 18, color(128, 140, 166));
        const std::string score = game.score > 0.0f ? scoreText(game.score) : "--";
        const int scoreWidth = text.width(score, 18);
        text.draw(renderer, score, cardX + cardWidth - 19 - scoreWidth, titleY + 31, 18,
                  game.score > 0.0f ? color(116, 235, 181) : color(128, 140, 166));
        if (reveal < 0.999f) {
            fillRoundedRect(renderer, cardX - 5, cardY - 5, cardWidth + 10, cardHeight + 10,
                            18, color(7, 10, 18, static_cast<Uint8>(245.0f * (1.0f - reveal))));
        }
    }

    void drawSelectedSummary(SDL_Renderer* renderer, TextRenderer& text, const vitrine::Game& game,
                             float reveal) {
        if (reveal <= 0.01f) return;
        const int baseSummaryY = (!backlogTab_ && !favoritesTab_) ? 572 : 544;
        const int summaryY = baseSummaryY + static_cast<int>(std::round(14.0f * (1.0f - reveal)));
        const int summaryHeight = (!backlogTab_ && !favoritesTab_) ? 66 : 94;
        const int firstLineY = summaryY + 9;
        const int secondLineY = summaryY + 38;
        fillRoundedRect(renderer, 42, summaryY, 1196, summaryHeight, 15, color(17, 23, 38));
        fillRoundedRect(renderer, 42, summaryY, 5, summaryHeight, 3, color(112, 130, 255));

        std::string genres;
        for (std::size_t index = 0; index < game.genres.size() && index < 2; ++index) {
            if (!genres.empty()) genres += "  •  ";
            genres += game.genres[index];
        }
        if (genres.empty()) genres = "Genero nao informado";
        text.draw(renderer, genres, 66, firstLineY, 18, color(151, 167, 255), 330);

        const std::string playtime = game.mainHours > 0.0f
            ? (game.averagePlaytime ? "Tempo medio  " : "Historia  ") + hoursText(game.mainHours)
            : "Tempo medio  --";
        text.draw(renderer, playtime, 430, firstLineY, 18, color(184, 193, 213), 250);
        text.draw(renderer, game.studio.empty() ? "Desenvolvedora nao informada" : game.studio,
                  714, firstLineY, 18, color(184, 193, 213), 330);
        text.draw(renderer, "A  Ver detalhes", 1059, firstLineY, 18, color(230, 234, 246), 155);

        const vitrine::BacklogStatus libraryStatus = backlogStatus(game.id);
        const std::string libraryAction = libraryStatus == vitrine::BacklogStatus::None
            ? "ZR  + Lista"
            : "ZR  " + std::string(vitrine::backlogStatusLabel(libraryStatus));
        text.draw(renderer, game.tagline, 66, secondLineY, 18, color(126, 138, 164), 820);
        text.draw(renderer, libraryAction, 900, secondLineY, 18, color(151, 167, 255), 155);
        text.draw(renderer, isFavorite(game.id) ? "L3  Remover" : "L3  + Favoritar",
                  1068, secondLineY, 18, color(232, 220, 255), 150);
        if (reveal < 0.999f) {
            fillRoundedRect(renderer, 42, summaryY, 1196, summaryHeight, 15,
                            color(7, 10, 18, static_cast<Uint8>(235.0f * (1.0f - reveal))));
        }
    }

    void drawCard(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images, const vitrine::Game& game,
                  int x, int y, bool selected, float reveal) {
        if (reveal <= 0.01f) return;
        y += static_cast<int>(std::round(24.0f * (1.0f - reveal)));
        if (selected) {
            fillRoundedRect(renderer, x - 5, y - 5, 287, 226, 17, color(112, 130, 255));
            fillRoundedRect(renderer, x - 2, y - 2, 281, 220, 14, color(22, 29, 46));
        } else {
            fillRoundedRect(renderer, x, y, 277, 216, 14, color(20, 27, 43));
        }
        gradientRect(renderer, x + 8, y + 8, 261, 111, game.coverTop, game.coverBottom);
        const bool hasImage = images.drawCover(renderer, game.localImagePath, x + 8, y + 8, 261, 111);
        if (hasImage) fillRect(renderer, x + 8, y + 8, 261, 111, color(7, 11, 19, 42));
        fillRoundedRect(renderer, x + 20, y + 20, 58, 26, 8, color(8, 13, 24, 205));
        text.draw(renderer, vitrine::gameTypeLabel(game.type), x + 31, y + 24, 18, color(230, 234, 244));
        if (isFavorite(game.id)) {
            fillRoundedRect(renderer, x + 226, y + 18, 32, 32, 16, color(87, 65, 145, 230));
            text.draw(renderer, "♥", x + 233, y + 22, 18, color(242, 222, 255));
        }

        if (!hasImage) {
            setColor(renderer, color(255, 255, 255, 58));
            SDL_RenderDrawLine(renderer, x + 182, y + 23, x + 244, y + 87);
            SDL_RenderDrawLine(renderer, x + 150, y + 90, x + 242, y + 36);
            fillRoundedRect(renderer, x + 205, y + 62, 30, 30, 15, color(255, 255, 255, 40));
        }

        text.draw(renderer, game.title, x + 12, y + 128, 22, color(240, 243, 251), 252);
        const std::string genre = game.genres.empty() ? "" : game.genres.front();
        text.draw(renderer, genre + "  •  " + std::to_string(game.releaseYear), x + 12, y + 158, 18, color(119, 133, 162), 160);
        if (game.score > 0.0f) {
            fillRoundedRect(renderer, x + 208, y + 154, 55, 28, 9, color(35, 96, 74));
            text.draw(renderer, scoreText(game.score), x + 219, y + 159, 18, color(116, 235, 181));
        }
        if (game.mainHours > 0.0f) {
            const std::string timePrefix = game.averagePlaytime ? "Tempo medio " : "Historia ";
            text.draw(renderer, timePrefix + hoursText(game.mainHours), x + 12, y + 188, 18, color(184, 193, 213));
        } else {
            text.draw(renderer, "Tempo medio --", x + 12, y + 188, 18, color(130, 142, 168));
        }
        if (selected) text.draw(renderer, "A", x + 236, y + 188, 18, color(151, 167, 255));
        if (reveal < 0.999f) {
            fillRoundedRect(renderer, x - 5, y - 5, 287, 226, 17,
                            color(7, 10, 18, static_cast<Uint8>(245.0f * (1.0f - reveal))));
        }
    }

    void renderFooter(SDL_Renderer* renderer, TextRenderer& text) {
        fillRect(renderer, 0, 652, kWidth, 68, color(9, 13, 23, 245));
        if (discoveryFocus_) {
            text.draw(renderer, "Esquerda / Direita  Secao", 42, 674, 18, color(151, 167, 255));
            text.draw(renderer, "A  Abrir", 300, 674, 18, color(224, 230, 246));
            text.draw(renderer, "Baixo  Voltar aos jogos", 410, 674, 18, color(204, 211, 226));
            text.draw(renderer, discoveryIndex_ == 0 ? "B  Cancelar" : "B  Todos", 675, 674, 18,
                      color(204, 211, 226));
            text.draw(renderer, "-  Sobre", 800, 674, 18, color(151, 167, 255));
            text.draw(renderer, status_, 900, 674, 18, color(122, 137, 170), 205);
            text.draw(renderer, "+  Sair", 1138, 674, 18, color(139, 150, 173));
            return;
        }
        text.draw(renderer, "A  Detalhes", 42, 674, 18, color(204, 211, 226));
        text.draw(renderer, "L/R  Abas", 166, 674, 18, color(151, 167, 255));
        text.draw(renderer, "X  Filtros", 270, 674, 18, color(204, 211, 226));
        if (backlogTab_ || favoritesTab_) {
            text.draw(renderer, "B  Explorar", 376, 674, 18, color(204, 211, 226));
        }
        text.draw(renderer, "Y  Buscar", 488, 674, 18, color(204, 211, 226));
        text.draw(renderer, "ZL  Surpresa", 586, 674, 18, color(151, 167, 255));
        text.draw(renderer, "R3  Vista", 724, 674, 18, color(151, 167, 255));
        text.draw(renderer, "-  Sobre", 818, 674, 18, color(151, 167, 255));
        text.draw(renderer, status_, 912, 674, 18, color(122, 137, 170), 194);
        text.draw(renderer, "+  Sair", 1138, 674, 18, color(139, 150, 173));
    }

    void renderDetailsWithTransition(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                                     const vitrine::Game& game) {
        const float elapsed = static_cast<float>(SDL_GetTicks() - detailTransitionStart_);
        const float linear = std::max(0.0f, std::min(1.0f, elapsed / 240.0f));
        const float eased = 1.0f - std::pow(1.0f - linear, 3.0f);
        if (linear >= 1.0f) {
            renderDetails(renderer, text, images, game);
            return;
        }

        if (!detailTransitionTexture_) {
            detailTransitionTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                         SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
            if (detailTransitionTexture_) SDL_SetTextureBlendMode(detailTransitionTexture_, SDL_BLENDMODE_BLEND);
        }
        if (!detailTransitionTexture_) {
            renderDetails(renderer, text, images, game);
            return;
        }

        SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, detailTransitionTexture_) != 0) {
            renderDetails(renderer, text, images, game);
            return;
        }
        setColor(renderer, color(0, 0, 0, 0));
        SDL_RenderClear(renderer);
        renderDetails(renderer, text, images, game);
        SDL_SetRenderTarget(renderer, previousTarget);

        fillRect(renderer, 0, 0, kWidth, kHeight,
                 color(3, 6, 12, static_cast<Uint8>(120.0f * eased)));
        SDL_SetTextureAlphaMod(detailTransitionTexture_, static_cast<Uint8>(255.0f * eased));
        const int offset = static_cast<int>(std::round(88.0f * (1.0f - eased)));
        SDL_Rect destination{offset, 0, kWidth, kHeight};
        SDL_RenderCopy(renderer, detailTransitionTexture_, nullptr, &destination);
    }

    void renderDetailsClosing(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                              const vitrine::Game& game) {
        const float elapsed = static_cast<float>(SDL_GetTicks() - detailTransitionStart_);
        const float linear = std::max(0.0f, std::min(1.0f, elapsed / 210.0f));
        const float eased = linear * linear * (3.0f - 2.0f * linear);
        if (linear >= 1.0f) {
            detailClosing_ = false;
            return;
        }
        if (!detailTransitionTexture_) {
            detailTransitionTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                         SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
            if (detailTransitionTexture_) SDL_SetTextureBlendMode(detailTransitionTexture_, SDL_BLENDMODE_BLEND);
        }
        if (!detailTransitionTexture_) {
            detailClosing_ = false;
            return;
        }
        SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
        if (SDL_SetRenderTarget(renderer, detailTransitionTexture_) != 0) {
            detailClosing_ = false;
            return;
        }
        setColor(renderer, color(0, 0, 0, 0));
        SDL_RenderClear(renderer);
        renderDetails(renderer, text, images, game);
        SDL_SetRenderTarget(renderer, previousTarget);

        const float remaining = 1.0f - eased;
        fillRect(renderer, 0, 0, kWidth, kHeight,
                 color(3, 6, 12, static_cast<Uint8>(120.0f * remaining)));
        SDL_SetTextureAlphaMod(detailTransitionTexture_, static_cast<Uint8>(255.0f * remaining));
        SDL_Rect destination{static_cast<int>(std::round(88.0f * eased)), 0, kWidth, kHeight};
        SDL_RenderCopy(renderer, detailTransitionTexture_, nullptr, &destination);
    }

    void renderTabTransition(SDL_Renderer* renderer) {
        if (tabTransitionStart_ == 0 || details_ || detailClosing_) return;
        const float elapsed = static_cast<float>(SDL_GetTicks() - tabTransitionStart_);
        const float linear = std::max(0.0f, std::min(1.0f, elapsed / 180.0f));
        if (linear >= 1.0f) {
            tabTransitionStart_ = 0;
            return;
        }
        const float eased = 1.0f - std::pow(1.0f - linear, 3.0f);
        fillRect(renderer, 0, 92, kWidth, 560,
                 color(5, 8, 16, static_cast<Uint8>(82.0f * (1.0f - eased))));
    }

    void renderDetails(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                       const vitrine::Game& game) {
        fillRect(renderer, 0, 0, kWidth, kHeight, color(7, 10, 18, 250));
        gradientRect(renderer, 0, 0, 404, kHeight, game.coverTop, game.coverBottom);
        const std::string heroImage = detailScreenshots_.empty() ? game.localCoverImagePath :
                                      detailScreenshots_[screenshotIndex_];
        images.drawCover(renderer, heroImage, 0, 0, 404, kHeight);
        fillRect(renderer, 0, 0, 404, kHeight, color(4, 8, 17, 72));
        fillRect(renderer, 0, 398, 404, 322, color(4, 8, 17, 214));

        std::string genres;
        for (std::size_t i = 0; i < game.genres.size(); ++i) {
            if (i) genres += "  •  ";
            genres += game.genres[i];
        }
        std::string credits = game.studio;
        if (!game.publisher.empty()) credits += "  •  " + game.publisher;
        std::string release = !game.releaseDate.empty() ? game.releaseDate :
                              (game.releaseYear > 0 ? std::to_string(game.releaseYear) : "Data indefinida");
        if (!game.ageRating.empty()) release += "  •  " + game.ageRating;
        std::string discoveryFacts;
        if (!game.franchise.empty()) discoveryFacts = "Franquia: " + game.franchise;
        if (!game.gameModes.empty()) {
            if (!discoveryFacts.empty()) discoveryFacts += "  •  ";
            discoveryFacts += game.gameModes.front();
        }
        if (!game.perspectives.empty()) {
            if (!discoveryFacts.empty()) discoveryFacts += "  •  ";
            discoveryFacts += game.perspectives.front();
        }
        if (!game.themes.empty()) {
            if (!discoveryFacts.empty()) discoveryFacts += "  •  ";
            discoveryFacts += "Tema: " + game.themes.front();
        }

        fillRoundedRect(renderer, 32, 32, 90, 34, 10, color(7, 12, 24, 205));
        text.draw(renderer, vitrine::gameTypeLabel(game.type), 52, 39, 18, color(239, 242, 250));
        const bool favorite = isFavorite(game.id);
        fillRoundedRect(renderer, 132, 32, favorite ? 116 : 128, 34, 10,
                        favorite ? color(87, 65, 145, 220) : color(20, 27, 43, 220));
        text.draw(renderer, favorite ? "♥  Favorito" : "+  Favoritar", 143, 39, 18,
                  favorite ? color(242, 222, 255) : color(224, 230, 246));
        const vitrine::BacklogStatus libraryStatus = backlogStatus(game.id);
        fillRoundedRect(renderer, 270, 32, 112, 34, 10,
                        libraryStatus == vitrine::BacklogStatus::None ? color(20, 27, 43, 220) :
                                                                       color(48, 74, 142, 230));
        text.draw(renderer, libraryStatus == vitrine::BacklogStatus::None ? "+  Lista" :
                  vitrine::backlogStatusLabel(libraryStatus), 281, 39, 18,
                  color(224, 230, 246), 92);

        fillRoundedRect(renderer, 32, 430, 5, 92, 2, color(112, 130, 255));
        text.drawWrapped(renderer, game.tagline, 52, 424, 28, color(245, 247, 252), 320, 3);
        text.draw(renderer, game.studio, 52, 548, 18, color(224, 230, 246), 320);
        if (!game.publisher.empty()) {
            text.draw(renderer, "Publicadora: " + game.publisher, 52, 578, 18,
                      color(192, 202, 225), 320);
        }
        text.draw(renderer, release, 52, 608, 18, color(192, 202, 225), 320);

        text.draw(renderer, "NINTENDO SWITCH  /  " + std::string(vitrine::gameTypeLabel(game.type)),
                  454, 38, 18, color(151, 167, 255));
        text.draw(renderer, game.title, 454, 68, 42, color(246, 248, 252), 626);
        fillRoundedRect(renderer, 1102, 42, 104, 78, 16, color(24, 34, 55));
        text.draw(renderer, "NOTA", 1132, 52, 18, color(126, 139, 167));
        const std::string score = game.score > 0 ? scoreText(game.score) : "--";
        const int scoreWidth = text.width(score, 28);
        text.draw(renderer, score, 1154 - scoreWidth / 2, 78, 28,
                  game.score > 0 ? color(116, 235, 181) : color(180, 190, 210));

        text.draw(renderer, genres, 456, 126, 18, color(137, 153, 190), 710);
        text.draw(renderer, credits, 456, 153, 18, color(157, 170, 199), 710);
        text.draw(renderer, discoveryFacts, 456, 180, 18, color(151, 167, 190), 710);
        text.draw(renderer, "SOBRE O JOGO", 454, 207, 18, color(119, 136, 175));
        fillRoundedRect(renderer, 454, 237, 4, 67, 2, color(112, 130, 255));
        const std::string editorialDescription = game.description.empty() ? game.tagline : game.description;
        text.drawWrapped(renderer, editorialDescription, 474, 229, 22, color(211, 218, 234), 716, 3);

        drawMetric(renderer, text, 454, 322, 244, "LANCAMENTO", release);
        drawMetric(renderer, text, 714, 322, 244, game.averagePlaytime ? "TEMPO MEDIO" : "HISTORIA",
                   game.mainHours > 0.0f ? hoursText(game.mainHours) : "--");
        drawMetric(renderer, text, 974, 322, 244, "COMPLETAR",
                   game.completionHours > 0.0f ? hoursText(game.completionHours) : "--");

        const std::string galleryTitle = game.videosCount > 0
            ? "GALERIA  •  " + std::to_string(game.videosCount) +
                  (game.videosCount == 1 ? " VIDEO" : " VIDEOS")
            : "GALERIA";
        text.draw(renderer, galleryTitle, 454, 452, 18, color(119, 136, 175));
        if (!detailScreenshots_.empty()) {
            text.draw(renderer, std::to_string(detailScreenshots_.size()) +
                      " imagens   ← / →  Alternar   A  Tela cheia", 824, 452, 18,
                      color(151, 167, 255));
            const std::size_t thumbnailCount = std::min<std::size_t>(3, detailScreenshots_.size());
            for (std::size_t index = 0; index < thumbnailCount; ++index) {
                const int x = 454 + static_cast<int>(index) * 252;
                if (static_cast<int>(index) == screenshotIndex_) {
                    fillRoundedRect(renderer, x - 4, 479, 240, 130, 11, color(112, 130, 255));
                }
                fillRoundedRect(renderer, x, 483, 232, 122, 8, color(20, 28, 46));
                images.drawCover(renderer, detailScreenshots_[index], x + 3, 486, 226, 116);
            }
        } else {
            fillRoundedRect(renderer, 454, 483, 764, 122, 14, color(19, 27, 44));
            text.draw(renderer, screenshotLoading_ ? "Buscando detalhes e screenshots..." :
                                                    "Screenshots indisponiveis para este item.",
                      482, 529, 18, color(146, 158, 184));
        }
        text.draw(renderer, "Somente informacao • este app nao baixa nem instala conteudo.",
                  454, 620, 18, color(131, 144, 173));
        if (!game.sourceName.empty()) text.draw(renderer, "Dados: " + game.sourceName, 454, 645, 18, color(137, 153, 190));
        text.draw(renderer, "ZR  Minha lista", 548, 669, 18, color(151, 167, 255));
        text.draw(renderer, "Y  Semelhantes", 700, 669, 18, color(151, 167, 255));
        text.draw(renderer, favorite ? "L3  Remover" : "L3  + Favoritar",
                  900, 669, 18, color(232, 220, 255));
        text.draw(renderer, "B  Voltar", 1093, 669, 18, color(203, 211, 227));
    }

    void renderScreenshotFullscreen(SDL_Renderer* renderer, TextRenderer& text,
                                    ImageRenderer& images) {
        fillRect(renderer, 0, 0, kWidth, kHeight, color(2, 3, 7));
        if (!detailScreenshots_.empty()) {
            images.drawContain(renderer, detailScreenshots_[screenshotIndex_], 18, 18, 1244, 630);
        }
        fillRect(renderer, 0, 648, kWidth, 72, color(5, 8, 15, 245));
        const std::string counter = "Screenshot " + std::to_string(screenshotIndex_ + 1) + " de " +
                                    std::to_string(detailScreenshots_.size());
        text.draw(renderer, counter, 42, 672, 18, color(224, 229, 241));
        text.draw(renderer, "← / →  Alternar", 503, 672, 18, color(151, 167, 255));
        text.draw(renderer, "A / B  Voltar", 1086, 672, 18, color(203, 211, 227));
    }

    void drawMetric(SDL_Renderer* renderer, TextRenderer& text, int x, int y, int w,
                    const std::string& label, const std::string& value) {
        fillRoundedRect(renderer, x, y, w, 100, 17, color(20, 28, 46));
        text.draw(renderer, label, x + 20, y + 17, 18, color(119, 136, 175));
        text.draw(renderer, value, x + 20, y + 49, 28, color(240, 243, 250));
    }

    static std::string scoreText(float score) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%.0f", score);
        return buffer;
    }

    static std::string hoursText(float hours) {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), hours == std::floor(hours) ? "%.0fh" : "%.1fh", hours);
        return buffer;
    }

    vitrine::Catalog catalog_;
    vitrine::Catalog favoriteCatalog_;
    vitrine::Catalog backlogCatalog_;
    vitrine::CatalogFilter filter_;
    vitrine::CatalogApiClient api_;
    std::vector<std::string> genres_;
    std::vector<const vitrine::Game*> games_;
    int genreIndex_ = 0;
    int highlightIndex_ = 0;
    int backlogFilterIndex_ = 0;
    int discoveryIndex_ = 0;
    int discoveryCursor_ = 0;
    int selected_ = 0;
    int previousSelected_ = -1;
    Uint32 selectionAnimationStart_ = 0;
    Uint32 gridRevealStart_ = 0;
    Uint32 detailTransitionStart_ = 0;
    Uint32 tabTransitionStart_ = 0;
    std::uint32_t surpriseSeed_ = 0x9e3779b9u;
    int currentPage_ = 1;
    int filterSection_ = 0;
    int filterOption_ = 0;
    bool details_ = false;
    bool detailClosing_ = false;
    bool screenshotFullscreen_ = false;
    // A vitrine de capas e o padrao. A vista classica preserva integralmente
    // os cards horizontais anteriores e pode ser recuperada com R3.
    bool classicView_ = false;
    bool backlogTab_ = false;
    bool favoritesTab_ = false;
    bool filterPanel_ = false;
    bool backlogPanel_ = false;
    bool backlogPanelFromDetails_ = false;
    bool about_ = false;
    bool aboutConfirmClear_ = false;
    bool discoveryFocus_ = false;
    bool hasDiscoveryReturnPoint_ = false;
    int backlogOption_ = 0;
    int aboutOption_ = 0;
    bool desktopTyping_ = false;
    bool networkReady_ = false;
    bool apiInitialized_ = false;
    bool usingApi_ = false;
    bool hasMore_ = false;
    std::string status_;
    std::vector<vitrine::Game> discoveryReturnGames_;
    vitrine::CatalogFilter discoveryReturnFilter_{};
    int discoveryReturnGenreIndex_ = 0;
    int discoveryReturnHighlightIndex_ = 0;
    int discoveryReturnSelected_ = 0;
    int discoveryReturnPage_ = 1;
    bool discoveryReturnHasMore_ = false;
    std::uint64_t aboutCacheBytes_ = 0;
    std::string aboutMessage_;
    std::vector<std::string> detailScreenshots_;
    std::vector<std::string> pendingScreenshotPaths_;
    vitrine::Game detailGame_{};
    vitrine::Game pendingDetailGame_{};
    vitrine::Game backlogPanelGame_{};
    std::string detailGameId_;
    std::string pendingScreenshotGameId_;
    std::string pendingScreenshotError_;
    std::string pendingDetailError_;
    int screenshotIndex_ = 0;
    std::thread screenshotThread_;
    std::atomic<bool> screenshotDone_{false};
    std::thread initialSyncThread_;
    std::atomic<bool> initialSyncDone_{false};
    vitrine::ApiResult pendingInitialSync_{};
    bool initialSyncRunning_ = false;
    bool screenshotLoading_ = false;
    bool screenshotQueued_ = false;
    bool pendingDetailLoaded_ = false;
    SDL_Texture* detailTransitionTexture_ = nullptr;
    std::thread coverWorker_;
    std::mutex coverMutex_;
    std::condition_variable coverCondition_;
    std::deque<CoverRequest> coverQueue_;
    std::unordered_set<std::string> queuedCoverIds_;
    std::unordered_set<std::string> processedCoverIds_;
    std::unordered_set<std::string> favoriteIds_;
    std::unordered_map<std::string, vitrine::BacklogStatus> backlogStatuses_;
    std::string inFlightCoverId_;
    std::string visibleCoverSignature_;
    bool stopCoverWorker_ = false;
};

#ifdef __SWITCH__
Input readSwitchInput(PadState& pad) {
    padUpdate(&pad);
    const u64 down = padGetButtonsDown(&pad);
    Input input;
    input.up = down & HidNpadButton_Up;
    input.down = down & HidNpadButton_Down;
    input.left = down & HidNpadButton_Left;
    input.right = down & HidNpadButton_Right;
    input.accept = down & HidNpadButton_A;
    input.back = down & HidNpadButton_B;
    input.search = down & HidNpadButton_Y;
    input.sort = down & HidNpadButton_X;
    input.previousGenre = down & HidNpadButton_L;
    input.nextGenre = down & HidNpadButton_R;
    input.surprise = down & HidNpadButton_ZL;
    input.backlog = down & HidNpadButton_ZR;
    input.viewMode = down & HidNpadButton_StickR;
    input.favorite = down & HidNpadButton_StickL;
    input.sync = down & HidNpadButton_Minus;
    input.quit = down & HidNpadButton_Plus;
    return input;
}
#endif

}  // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return EXIT_FAILURE;
    if ((IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP) & (IMG_INIT_JPG | IMG_INIT_PNG)) == 0) {
        IMG_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window* window = SDL_CreateWindow("Vitrine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          kWidth, kHeight, SDL_WINDOW_SHOWN);
    if (!window) { IMG_Quit(); SDL_Quit(); return EXIT_FAILURE; }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { SDL_DestroyWindow(window); IMG_Quit(); SDL_Quit(); return EXIT_FAILURE; }
    SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);

    TextRenderer text;
    if (!text.initialize()) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return EXIT_FAILURE;
    }

    bool networkReady = true;
#ifdef __SWITCH__
    romfsInit();
    networkReady = R_SUCCEEDED(socketInitializeDefault());
#endif
    App app(networkReady);
    ImageRenderer images;
    bool running = true;
#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
#endif

    while (running) {
        Input input;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
#ifndef __SWITCH__
            if (event.type == SDL_TEXTINPUT) app.appendSearchText(event.text.text);
            if (event.type == SDL_KEYDOWN) {
                const SDL_Keycode key = event.key.keysym.sym;
                input.up |= key == SDLK_UP;
                input.down |= key == SDLK_DOWN;
                input.left |= key == SDLK_LEFT;
                input.right |= key == SDLK_RIGHT;
                input.accept |= key == SDLK_RETURN;
                input.back |= key == SDLK_ESCAPE;
                input.search |= key == SDLK_y || key == SDLK_SLASH;
                input.sort |= key == SDLK_x;
                input.previousGenre |= key == SDLK_q;
                input.nextGenre |= key == SDLK_e;
                input.surprise |= key == SDLK_s;
                input.backlog |= key == SDLK_l;
                input.viewMode |= key == SDLK_v;
                input.favorite |= key == SDLK_f;
                input.sync |= key == SDLK_MINUS;
                if (key == SDLK_BACKSPACE) app.eraseSearchCharacter();
            }
#endif
        }

#ifdef __SWITCH__
        if (!appletMainLoop()) running = false;
        const Input switchInput = readSwitchInput(pad);
        input = switchInput;
#endif
        if (input.quit) running = false;
        app.handle(input);
        app.render(renderer, text, images);
        SDL_RenderPresent(renderer);
    }

    app.releaseRendererResources();
    images.clear();
    text.shutdown();
#ifdef __SWITCH__
    romfsExit();
    if (networkReady) socketExit();
#endif
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
    return EXIT_SUCCESS;
}
