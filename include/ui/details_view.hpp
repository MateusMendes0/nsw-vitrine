#pragma once

#include "catalog.hpp"
#include "draw_utils.hpp"
#include "image_renderer.hpp"
#include "text_renderer.hpp"
#include "ui_constants.hpp"

#include <SDL2/SDL.h>

#include <string>
#include <vector>

namespace vitrine {

class DetailsView {
public:
    DetailsView() = default;
    ~DetailsView();

    void releaseTransitionTexture();

    void renderDetails(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                       const Game& game, const std::vector<std::string>& detailScreenshots,
                       int screenshotIndex, bool screenshotLoading,
                       bool isFav, BacklogStatus bStatus);

    void renderDetailsWithTransition(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                                     const Game& game, const std::vector<std::string>& detailScreenshots,
                                     int screenshotIndex, bool screenshotLoading,
                                     bool isFav, BacklogStatus bStatus,
                                     Uint32 detailTransitionStart);

    bool renderDetailsClosing(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                             const Game& game, const std::vector<std::string>& detailScreenshots,
                             int screenshotIndex, bool screenshotLoading,
                             bool isFav, BacklogStatus bStatus,
                             Uint32 detailTransitionStart);

    void renderScreenshotFullscreen(SDL_Renderer* renderer, TextRenderer& text,
                                    ImageRenderer& images,
                                    const std::vector<std::string>& detailScreenshots,
                                    int screenshotIndex);

    static void drawDetailAction(SDL_Renderer* renderer, TextRenderer& text,
                                int x, int y, int width,
                                const std::string& key, const std::string& label,
                                bool primary, bool active);

private:
    SDL_Texture* detailTransitionTexture_ = nullptr;
};

}  // namespace vitrine
