#pragma once

#include "catalog.hpp"
#include "draw_utils.hpp"
#include "image_renderer.hpp"
#include "text_renderer.hpp"
#include "ui_constants.hpp"

#include <SDL2/SDL.h>

#include <functional>
#include <string>
#include <vector>

namespace vitrine {

class GridView {
public:
    void render(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                const std::vector<const Game*>& games,
                int selected, int previousSelected,
                Uint32 selectionAnimationStart, Uint32 gridRevealStart,
                bool classicView, bool backlogTab, bool favoritesTab,
                bool showSummary, bool touchBrowse, int touchScrollY,
                const std::function<bool(const std::string&)>& isFavorite,
                const std::function<BacklogStatus(const std::string&)>& getBacklogStatus);

    static float selectionFocus(int index, int selected, int previousSelected,
                                Uint32 selectionAnimationStart);

    static float gridRevealProgress(int slot, Uint32 gridRevealStart);

    static void drawBacklogBadge(SDL_Renderer* renderer, TextRenderer& text,
                                 BacklogStatus status, int right, int y);

    static void drawCoverCard(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                              const Game& game, int x, int y, float focus, float reveal,
                              bool isFav, BacklogStatus bStatus);

    static void drawCard(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                         const Game& game, int x, int y, bool selected, float reveal,
                         bool isFav);

    static void drawSelectedSummary(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                                    const Game& game, float reveal,
                                    bool isFav, BacklogStatus bStatus);
};

}  // namespace vitrine
