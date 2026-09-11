#pragma once

#include "catalog.hpp"
#include "draw_utils.hpp"
#include "image_renderer.hpp"
#include "text_renderer.hpp"
#include "ui_constants.hpp"

#include <SDL2/SDL.h>

#include <string>

namespace vitrine {

class LayoutView {
public:
    void renderBackground(SDL_Renderer* renderer);

    void renderHeader(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                      const std::string& searchQuery);

    void renderToolbar(SDL_Renderer* renderer, TextRenderer& text,
                       bool backlogTab, bool favoritesTab,
                       std::size_t gameCount, bool hasMore, bool usingApi,
                       const std::string& genreLabel,
                       const std::string& secondaryFilterLabel,
                       SortMode sortMode);

    void renderDiscoveryRibbon(SDL_Renderer* renderer, TextRenderer& text,
                               bool backlogTab, bool favoritesTab,
                               bool hasSimilar, const std::string& similarGameTitle,
                               int discoveryIndex, bool discoveryFocus, int discoveryCursor);

    void renderFooter(SDL_Renderer* renderer, TextRenderer& text,
                      bool hasSimilar, const std::string& query,
                      bool discoveryFocus, int discoveryIndex,
                      bool backlogTab, bool favoritesTab,
                      const std::string& status);

    void renderTabTransition(SDL_Renderer* renderer, Uint32 tabTransitionStart,
                             bool details, bool detailClosing);

    static void drawFilterChip(SDL_Renderer* renderer, TextRenderer& text,
                               int x, int y, int w,
                               const std::string& title, const std::string& value);

    static const char* discoveryLabel(int index);
};

}  // namespace vitrine
