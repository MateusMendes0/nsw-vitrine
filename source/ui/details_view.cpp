#include "details_view.hpp"

#include <algorithm>
#include <cmath>

namespace vitrine {

DetailsView::~DetailsView() {
    releaseTransitionTexture();
}

void DetailsView::releaseTransitionTexture() {
    if (detailTransitionTexture_) {
        SDL_DestroyTexture(detailTransitionTexture_);
        detailTransitionTexture_ = nullptr;
    }
}

void DetailsView::drawDetailAction(SDL_Renderer* renderer, TextRenderer& text,
                                   int x, int y, int width,
                                   const std::string& key, const std::string& label,
                                   bool primary, bool active) {
    if (primary) {
        fillRoundedRect(renderer, x - 4, y - 4, width + 8, 56, 14, color(28, 158, 255, 72));
        fillRoundedRect(renderer, x - 2, y - 2, width + 4, 52, 13, color(112, 225, 255));
    } else {
        fillRoundedRect(renderer, x - 1, y - 1, width + 2, 50, 12,
                        active ? color(143, 104, 194, 230) : color(58, 79, 113, 225));
    }
    fillRoundedRect(renderer, x, y, width, 48, 11,
                    primary ? color(22, 100, 174, 242) :
                    (active ? color(64, 44, 91, 242) : color(15, 25, 42, 236)));
    drawKeyHint(renderer, text, x + 13, y + 10, key, label, primary);
}

void DetailsView::renderDetailsWithTransition(SDL_Renderer* renderer, TextRenderer& text,
                                              ImageRenderer& images,
                                              const Game& game,
                                              const std::vector<std::string>& detailScreenshots,
                                              int screenshotIndex, bool screenshotLoading,
                                              bool isFav, BacklogStatus bStatus,
                                              Uint32 detailTransitionStart) {
    const float elapsed = static_cast<float>(SDL_GetTicks() - detailTransitionStart);
    const float linear = std::max(0.0f, std::min(1.0f, elapsed / 240.0f));
    const float eased = 1.0f - std::pow(1.0f - linear, 3.0f);
    if (linear >= 1.0f) {
        renderDetails(renderer, text, images, game, detailScreenshots, screenshotIndex,
                      screenshotLoading, isFav, bStatus);
        return;
    }

    if (!detailTransitionTexture_) {
        detailTransitionTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                     SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
        if (detailTransitionTexture_) {
            SDL_SetTextureBlendMode(detailTransitionTexture_, SDL_BLENDMODE_BLEND);
        }
    }
    if (!detailTransitionTexture_) {
        renderDetails(renderer, text, images, game, detailScreenshots, screenshotIndex,
                      screenshotLoading, isFav, bStatus);
        return;
    }

    SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, detailTransitionTexture_) != 0) {
        renderDetails(renderer, text, images, game, detailScreenshots, screenshotIndex,
                      screenshotLoading, isFav, bStatus);
        return;
    }
    setColor(renderer, color(0, 0, 0, 0));
    SDL_RenderClear(renderer);
    renderDetails(renderer, text, images, game, detailScreenshots, screenshotIndex,
                  screenshotLoading, isFav, bStatus);
    SDL_SetRenderTarget(renderer, previousTarget);

    fillRect(renderer, 0, 0, kWidth, kHeight,
             color(3, 6, 12, static_cast<Uint8>(120.0f * eased)));
    SDL_SetTextureAlphaMod(detailTransitionTexture_, static_cast<Uint8>(255.0f * eased));
    const int offset = static_cast<int>(std::round(88.0f * (1.0f - eased)));
    SDL_Rect destination{offset, 0, kWidth, kHeight};
    SDL_RenderCopy(renderer, detailTransitionTexture_, nullptr, &destination);
}

bool DetailsView::renderDetailsClosing(SDL_Renderer* renderer, TextRenderer& text,
                                       ImageRenderer& images,
                                       const Game& game,
                                       const std::vector<std::string>& detailScreenshots,
                                       int screenshotIndex, bool screenshotLoading,
                                       bool isFav, BacklogStatus bStatus,
                                       Uint32 detailTransitionStart) {
    const float elapsed = static_cast<float>(SDL_GetTicks() - detailTransitionStart);
    const float linear = std::max(0.0f, std::min(1.0f, elapsed / 210.0f));
    const float eased = linear * linear * (3.0f - 2.0f * linear);
    if (linear >= 1.0f) {
        return false;  // closing finished
    }
    if (!detailTransitionTexture_) {
        detailTransitionTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                     SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
        if (detailTransitionTexture_) {
            SDL_SetTextureBlendMode(detailTransitionTexture_, SDL_BLENDMODE_BLEND);
        }
    }
    if (!detailTransitionTexture_) {
        return false;
    }
    SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, detailTransitionTexture_) != 0) {
        return false;
    }
    setColor(renderer, color(0, 0, 0, 0));
    SDL_RenderClear(renderer);
    renderDetails(renderer, text, images, game, detailScreenshots, screenshotIndex,
                  screenshotLoading, isFav, bStatus);
    SDL_SetRenderTarget(renderer, previousTarget);

    const float remaining = 1.0f - eased;
    fillRect(renderer, 0, 0, kWidth, kHeight,
             color(3, 6, 12, static_cast<Uint8>(120.0f * remaining)));
    SDL_SetTextureAlphaMod(detailTransitionTexture_, static_cast<Uint8>(255.0f * remaining));
    SDL_Rect destination{static_cast<int>(std::round(88.0f * eased)), 0, kWidth, kHeight};
    SDL_RenderCopy(renderer, detailTransitionTexture_, nullptr, &destination);
    return true;
}

void DetailsView::renderDetails(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                                const Game& game,
                                const std::vector<std::string>& detailScreenshots,
                                int screenshotIndex, bool screenshotLoading,
                                bool isFav, BacklogStatus bStatus) {
    gradientRect(renderer, 0, 0, kWidth, kHeight, game.coverTop, game.coverBottom);
    bool hasHero = false;
    if (!detailScreenshots.empty() && screenshotIndex >= 0 &&
        screenshotIndex < static_cast<int>(detailScreenshots.size())) {
        hasHero = images.drawCover(renderer, detailScreenshots[screenshotIndex], 0, 0, kWidth, kHeight);
    }
    if (!hasHero) hasHero = images.drawCover(renderer, game.localImagePath, 0, 0, kWidth, kHeight);
    if (!hasHero) images.drawCover(renderer, game.localCoverImagePath, 0, 0, kWidth, kHeight);

    fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 7, 16, 62));
    horizontalGradientRect(renderer, 0, 0, 920, 652,
                           color(3, 8, 18, 248), color(3, 8, 18, 8));
    fillRect(renderer, 0, 402, kWidth, 250, color(3, 8, 18, 145));

    std::string genres;
    for (std::size_t i = 0; i < game.genres.size() && i < 3; ++i) {
        if (i) genres += "  •  ";
        genres += game.genres[i];
    }
    if (genres.empty()) genres = "Genero nao informado";

    const std::string release = !game.releaseDate.empty() ? game.releaseDate :
        (game.releaseYear > 0 ? std::to_string(game.releaseYear) : "Data indefinida");
    std::string metadata = release;
    if (!game.studio.empty()) metadata += "  •  " + game.studio;
    metadata += "  •  " + genres;

    std::string facts;
    if (!game.gameModes.empty()) facts = game.gameModes.front();
    if (!game.perspectives.empty()) {
        if (!facts.empty()) facts += "  •  ";
        facts += game.perspectives.front();
    }
    if (!game.ageRating.empty()) {
        if (!facts.empty()) facts += "  •  ";
        facts += game.ageRating;
    }

    text.draw(renderer, "NINTENDO SWITCH   |   " + std::string(gameTypeLabel(game.type)),
              42, 34, 18, color(155, 186, 231));
    text.draw(renderer, game.title, 42, 70, 42, color(247, 249, 253), 760);
    text.draw(renderer, metadata, 42, 126, 18, color(184, 199, 223), 790);

    const std::string score = game.score > 0.0f ? scoreText(game.score) : "--";
    fillRoundedRect(renderer, 42, 164, 72, 48, 10,
                    game.score > 0.0f ? color(34, 162, 91, 242) : color(46, 57, 76, 235));
    const int scoreWidth = text.width(score, 28);
    text.draw(renderer, score, 78 - scoreWidth / 2, 173, 28, color(247, 252, 249));
    text.draw(renderer, game.sourceName.empty() ? "NOTA" : game.sourceName,
              126, 177, 18, color(220, 228, 240));

    fillRoundedRect(renderer, 194, 166, 2, 44, 1, color(76, 92, 117, 190));
    text.draw(renderer, game.averagePlaytime ? "TEMPO MEDIO" : "HISTORIA",
              216, 160, 18, color(132, 151, 183));
    text.draw(renderer, game.mainHours > 0.0f ? hoursText(game.mainHours) : "--",
              216, 184, 22, color(241, 245, 251));
    fillRoundedRect(renderer, 354, 166, 2, 44, 1, color(76, 92, 117, 190));
    text.draw(renderer, "COMPLETAR", 376, 160, 18, color(132, 151, 183));
    text.draw(renderer, game.completionHours > 0.0f ? hoursText(game.completionHours) : "--",
              376, 184, 22, color(241, 245, 251));
    if (!facts.empty()) text.draw(renderer, facts, 516, 177, 18, color(184, 199, 223), 360);

    const std::string editorialDescription = game.description.empty() ? game.tagline : game.description;
    fillRoundedRect(renderer, 42, 237, 5, 78, 2, color(112, 225, 255));
    text.drawWrapped(renderer, editorialDescription, 62, 229, 22,
                     color(218, 225, 238), 760, 3);

    if (!detailScreenshots.empty()) {
        drawDetailAction(renderer, text, 42, 334, 168, "A", "Ver imagem", true, false);
    }
    drawDetailAction(renderer, text, 226, 334, 216, "ZR",
                     bStatus == BacklogStatus::None
                         ? "Minha lista"
                         : backlogStatusLabel(bStatus),
                     false, bStatus != BacklogStatus::None);
    drawDetailAction(renderer, text, 458, 334, 208, "L3",
                     isFav ? "Remover" : "Favoritar", false, isFav);
    drawDetailAction(renderer, text, 682, 334, 210, "Y", "Semelhantes", false, false);

    const std::string galleryTitle = "IMAGENS";
    text.draw(renderer, galleryTitle, 42, 414, 18, color(151, 181, 224));
    if (!detailScreenshots.empty()) {
        const std::string galleryHint = std::to_string(screenshotIndex + 1) + " / " +
            std::to_string(detailScreenshots.size()) + "     ← / →  Selecionar     A  Tela cheia";
        text.draw(renderer, galleryHint, 853, 414, 18, color(184, 204, 235), 385);
        const std::size_t thumbnailCount = std::min<std::size_t>(6, detailScreenshots.size());
        for (std::size_t index = 0; index < thumbnailCount; ++index) {
            const int x = 42 + static_cast<int>(index) * 200;
            const bool selected = static_cast<int>(index) == screenshotIndex;
            if (selected) {
                fillRoundedRect(renderer, x - 6, 443, 200, 126, 12, color(34, 181, 255, 85));
                fillRoundedRect(renderer, x - 3, 446, 194, 120, 10, color(112, 225, 255));
            }
            fillRoundedRect(renderer, x, 449, 188, 114, 8, color(19, 28, 45));
            images.drawCover(renderer, detailScreenshots[index], x + 3, 452, 182, 108);
        }
    } else {
        fillRoundedRect(renderer, 42, 449, 1196, 114, 12, color(12, 20, 34, 224));
        text.draw(renderer, screenshotLoading ? "Carregando detalhes e imagens..." :
                                                "Imagens indisponiveis para este jogo.",
                  72, 491, 22, color(182, 195, 218));
    }

    text.draw(renderer, "Somente informacao • o Vitrine nao baixa nem executa jogos.",
              42, 608, 18, color(143, 158, 184));
    if (!game.sourceName.empty()) {
        text.draw(renderer, "Dados: " + game.sourceName, 1100, 608, 18,
                  color(151, 181, 224), 138);
    }

    fillRect(renderer, 0, 652, kWidth, 68, color(5, 10, 20, 248));
    fillRect(renderer, 0, 652, kWidth, 1, color(38, 111, 190, 190));
    drawKeyHint(renderer, text, 42, 670, "B", "Voltar");
    if (!detailScreenshots.empty()) {
        text.draw(renderer, "← / →  Imagens", 174, 674, 18, color(166, 190, 225));
        drawKeyHint(renderer, text, 344, 670, "A", "Tela cheia", true);
    }
    drawKeyHint(renderer, text, 508, 670, "ZR", "Minha lista");
    drawKeyHint(renderer, text, 672, 670, "Y", "Semelhantes");
    drawKeyHint(renderer, text, 842, 670, "L3", isFav ? "Remover" : "Favoritar");
}

void DetailsView::renderScreenshotFullscreen(SDL_Renderer* renderer, TextRenderer& text,
                                             ImageRenderer& images,
                                             const std::vector<std::string>& detailScreenshots,
                                             int screenshotIndex) {
    fillRect(renderer, 0, 0, kWidth, kHeight, color(2, 3, 7));
    if (!detailScreenshots.empty() && screenshotIndex >= 0 &&
        screenshotIndex < static_cast<int>(detailScreenshots.size())) {
        images.drawContain(renderer, detailScreenshots[screenshotIndex], 18, 18, 1244, 630);
    }
    fillRect(renderer, 0, 648, kWidth, 72, color(5, 8, 15, 245));
    const std::string counter = "Screenshot " + std::to_string(screenshotIndex + 1) + " de " +
                                std::to_string(detailScreenshots.size());
    text.draw(renderer, counter, 42, 672, 18, color(224, 229, 241));
    text.draw(renderer, "← / →  Alternar", 503, 672, 18, color(151, 167, 255));
    text.draw(renderer, "A / B  Voltar", 1086, 672, 18, color(203, 211, 227));
}

}  // namespace vitrine
