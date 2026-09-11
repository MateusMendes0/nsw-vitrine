#pragma once

#include "catalog.hpp"
#include "draw_utils.hpp"
#include "text_renderer.hpp"
#include "ui_constants.hpp"

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vitrine {

class PanelsView {
public:
    static std::string formatCacheSize(std::uint64_t bytes);

    void renderFilterPanel(SDL_Renderer* renderer, TextRenderer& text,
                           int filterSection, int filterOption, int currentApplied,
                           bool backlogTab, const std::vector<std::string>& genres);

    void renderBacklogPanel(SDL_Renderer* renderer, TextRenderer& text,
                            const Game& game, int backlogOption,
                            BacklogStatus currentStatus);

    void renderAbout(SDL_Renderer* renderer, TextRenderer& text,
                     int aboutOption, bool networkReady, bool apiInitialized,
                     bool initialSyncRunning, bool usingApi,
                     std::uint64_t aboutCacheBytes, const std::string& aboutMessage,
                     bool aboutConfirmClear);

    static std::string filterOptionLabel(int filterSection, int index,
                                         const std::vector<std::string>& genres);
};

}  // namespace vitrine
