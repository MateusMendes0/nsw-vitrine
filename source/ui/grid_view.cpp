#include "grid_view.hpp"

#include <algorithm>
#include <cmath>

namespace vitrine {

float GridView::gridRevealProgress(int slot, Uint32 gridRevealStart) {
    if (gridRevealStart == 0) return 1.0f;
    const float elapsed = static_cast<float>(SDL_GetTicks() - gridRevealStart);
    const float local = elapsed - 130.0f - static_cast<float>(slot) * 72.0f;
    const float linear = std::max(0.0f, std::min(1.0f, local / 260.0f));
    return 1.0f - std::pow(1.0f - linear, 3.0f);
}

float GridView::selectionFocus(int index, int selected, int previousSelected,
                              Uint32 selectionAnimationStart) {
    if (previousSelected < 0 || selectionAnimationStart == 0) {
        return index == selected ? 1.0f : 0.0f;
    }
    const float elapsed = static_cast<float>(SDL_GetTicks() - selectionAnimationStart);
    const float linear = std::max(0.0f, std::min(1.0f, elapsed / 170.0f));
    const float eased = linear * linear * (3.0f - 2.0f * linear);
    if (index == selected) return eased;
    if (index == previousSelected) return 1.0f - eased;
    return 0.0f;
}

void GridView::drawBacklogBadge(SDL_Renderer* renderer, TextRenderer& text,
                               BacklogStatus value, int right, int y) {
    if (value == BacklogStatus::None) return;
    SDL_Color background = color(48, 74, 142, 232);
    std::string label = "QUERO";
    if (value == BacklogStatus::Playing) {
        background = color(132, 93, 34, 232);
        label = "JOGANDO";
    } else if (value == BacklogStatus::Completed) {
        background = color(35, 105, 76, 232);
        label = "FEITO";
    } else if (value == BacklogStatus::Dropped) {
        background = color(79, 68, 91, 232);
        label = "PAROU";
    }
    const int width = text.width(label, 18) + 18;
    fillRoundedRect(renderer, right - width, y, width, 27, 8, background);
    text.draw(renderer, label, right - width + 9, y + 4, 18, color(241, 244, 250));
}

void GridView::drawCoverCard(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                             const Game& game, int x, int y, float focus, float reveal,
                             bool isFav, BacklogStatus bStatus) {
    if (reveal <= 0.01f) return;
    y += static_cast<int>(std::round(28.0f * (1.0f - reveal)));
    const int cardX = x - static_cast<int>(std::round(3.0f * focus));
    const int cardY = y - static_cast<int>(std::round(4.0f * focus));
    const int cardWidth = 220 + static_cast<int>(std::round(6.0f * focus));
    const int cardHeight = 308 + static_cast<int>(std::round(8.0f * focus));
    const int coverX = cardX + 8;
    const int coverY = cardY + 8;
    const int coverWidth = cardWidth - 16;
    const int coverHeight = 236 + static_cast<int>(std::round(6.0f * focus));

    if (focus > 0.01f) {
        fillRoundedRect(renderer, cardX - 10, cardY - 10, cardWidth + 20, cardHeight + 20,
                        21, color(24, 139, 245, static_cast<Uint8>(42.0f * focus)));
        fillRoundedRect(renderer, cardX - 7, cardY - 7, cardWidth + 14, cardHeight + 14,
                        19, color(36, 184, 255, static_cast<Uint8>(92.0f * focus)));
        fillRoundedRect(renderer, cardX - 4, cardY - 4, cardWidth + 8, cardHeight + 8,
                        17, color(112, 225, 255, static_cast<Uint8>(255.0f * focus)));
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

    if (isFav) {
        fillRoundedRect(renderer, coverX + 10, coverY + 10, 32, 32, 16, color(87, 65, 145, 230));
        text.draw(renderer, "♥", coverX + 17, coverY + 14, 18, color(242, 222, 255));
    }
    drawBacklogBadge(renderer, text, bStatus, coverX + coverWidth - 10, coverY + 10);

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
    if (game.score > 0.0f) {
        const std::string year = game.releaseYear > 0 ? std::to_string(game.releaseYear) : "----";
        text.draw(renderer, year, cardX + 10, titleY + 31, 18, color(128, 140, 166));
        const std::string score = scoreText(game.score);
        const int scoreWidth = text.width(score, 18);
        text.draw(renderer, score, cardX + cardWidth - 19 - scoreWidth, titleY + 31, 18, color(116, 235, 181));
    } else {
        const std::string expected = formatExpectedRelease(game.releaseDate, game.releaseYear);
        if (expected != "--") {
            const bool hasYearInBadge = (game.releaseYear > 0 && expected.find(std::to_string(game.releaseYear)) != std::string::npos);
            const std::string leftLabel = hasYearInBadge ? "Estreia" :
                (game.releaseYear > 0 ? std::to_string(game.releaseYear) : "Estreia");
            text.draw(renderer, leftLabel, cardX + 10, titleY + 31, 18, color(128, 140, 166));
            const int badgeWidth = text.width(expected, 18);
            text.draw(renderer, expected, cardX + cardWidth - 19 - badgeWidth, titleY + 31, 18, color(112, 225, 255));
        } else {
            const std::string year = game.releaseYear > 0 ? std::to_string(game.releaseYear) : "----";
            text.draw(renderer, year, cardX + 10, titleY + 31, 18, color(128, 140, 166));
            const int scoreWidth = text.width("--", 18);
            text.draw(renderer, "--", cardX + cardWidth - 19 - scoreWidth, titleY + 31, 18, color(128, 140, 166));
        }
    }
    if (reveal < 0.999f) {
        fillRoundedRect(renderer, cardX - 10, cardY - 10, cardWidth + 20, cardHeight + 20,
                        21, color(7, 10, 18, static_cast<Uint8>(245.0f * (1.0f - reveal))));
    }
    if (focus > 0.01f) {
        fillRoundedRect(renderer, cardX - 4, y, cardWidth + 8, 4, 2,
                        color(112, 225, 255, static_cast<Uint8>(255.0f * focus)));
    }
}

void GridView::drawCard(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                        const Game& game, int x, int y, bool selected, float reveal,
                        bool isFav) {
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
    text.draw(renderer, gameTypeLabel(game.type), x + 31, y + 24, 18, color(230, 234, 244));
    if (isFav) {
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
    } else {
        const std::string expected = formatExpectedRelease(game.releaseDate, game.releaseYear);
        if (expected != "--") {
            const int badgeWidth = text.width(expected, 18) + 16;
            fillRoundedRect(renderer, x + 263 - badgeWidth, y + 154, badgeWidth, 28, 9, color(28, 48, 76));
            text.draw(renderer, expected, x + 263 - badgeWidth + 8, y + 159, 18, color(112, 225, 255));
        }
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
    if (selected) fillRoundedRect(renderer, x - 5, y, 287, 4, 2, color(112, 130, 255));
}

void GridView::drawSelectedSummary(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                                  const Game& game, float reveal,
                                  bool isFav, BacklogStatus bStatus) {
    if (reveal <= 0.01f) return;
    const int summaryY = 516 + static_cast<int>(std::round(12.0f * (1.0f - reveal)));
    constexpr int summaryHeight = 122;
    fillRoundedRect(renderer, 42, summaryY, 1196, summaryHeight, 15, color(17, 23, 38));
    fillRoundedRect(renderer, 42, summaryY, 5, summaryHeight, 3, color(112, 225, 255));

    gradientRect(renderer, 54, summaryY + 10, 176, 102, game.coverTop, game.coverBottom);
    if (images.drawCover(renderer, game.localImagePath, 54, summaryY + 10, 176, 102)) {
        fillRect(renderer, 54, summaryY + 10, 176, 102, color(5, 10, 20, 42));
    }
    fillRoundedRect(renderer, 241, summaryY + 12, 2, summaryHeight - 24, 1, color(47, 61, 85));

    std::string genres;
    for (std::size_t index = 0; index < game.genres.size() && index < 2; ++index) {
        if (!genres.empty()) genres += "  •  ";
        genres += game.genres[index];
    }
    if (genres.empty()) genres = "Genero nao informado";
    const std::string playtime = game.mainHours > 0.0f
        ? (game.averagePlaytime ? "Tempo medio  " : "Historia  ") + hoursText(game.mainHours)
        : "Tempo medio  --";
    const std::string year = game.releaseYear > 0 ? std::to_string(game.releaseYear) : "Data indefinida";
    const std::string metadata = genres + "  •  " + year + "  •  " + playtime;
    text.draw(renderer, game.title, 258, summaryY + 5, 28, color(245, 248, 253), 790);
    text.draw(renderer, metadata, 258, summaryY + 39, 18, color(151, 184, 224), 790);
    const std::string description = game.description.empty() ? game.tagline : game.description;
    text.draw(renderer, description, 258, summaryY + 65, 18, color(179, 190, 211), 790);

    const std::string libraryAction = bStatus == BacklogStatus::None
        ? "Minha lista"
        : backlogStatusLabel(bStatus);
    drawKeyHint(renderer, text, 258, summaryY + 91, "A", "Detalhes", true);
    drawKeyHint(renderer, text, 386, summaryY + 91, "ZR", libraryAction);
    drawKeyHint(renderer, text, 554, summaryY + 91, "L3",
                isFav ? "Remover favorito" : "Favoritar");

    fillRoundedRect(renderer, 1080, summaryY + 13, 2, summaryHeight - 26, 1, color(47, 61, 85));
    if (game.score > 0.0f) {
        text.draw(renderer, "NOTA", 1121, summaryY + 25, 18, color(126, 145, 176));
        const std::string score = scoreText(game.score);
        const int scoreWidth = text.width(score, 28);
        text.draw(renderer, score, 1158 - scoreWidth / 2, summaryY + 54, 28, color(116, 235, 181));
    } else {
        const std::string expected = formatExpectedRelease(game.releaseDate, game.releaseYear);
        text.draw(renderer, "ESTREIA", 1107, summaryY + 25, 18, color(126, 145, 176));
        const int dateWidth = text.width(expected, 20);
        text.draw(renderer, expected, 1158 - dateWidth / 2, summaryY + 56, 20,
                  expected == "--" ? color(150, 162, 186) : color(112, 225, 255));
    }
    if (reveal < 0.999f) {
        fillRoundedRect(renderer, 42, summaryY, 1196, summaryHeight, 15,
                        color(7, 10, 18, static_cast<Uint8>(235.0f * (1.0f - reveal))));
    }
}

void GridView::render(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                     const std::vector<const Game*>& games,
                     int selected, int previousSelected,
                     Uint32 selectionAnimationStart, Uint32 gridRevealStart,
                     bool classicView, bool backlogTab, bool favoritesTab,
                     bool showSummary, bool touchBrowse, int touchScrollY,
                     const std::function<bool(const std::string&)>& isFavorite,
                     const std::function<BacklogStatus(const std::string&)>& getBacklogStatus) {
    if (games.empty()) {
        text.draw(renderer, backlogTab ? "Sua lista esta vazia" :
                  (favoritesTab ? "Nenhum jogo favorito" : "Nenhum item encontrado"),
                  450, 300, 28, color(231, 235, 245));
        text.draw(renderer, backlogTab ? "Na aba Explorar, use ZR para adicionar jogos." :
                  (favoritesTab ? "Na aba Explorar, use L3 para adicionar jogos." :
                                   "Tente outro termo ou altere os filtros."),
                  443, 344, 18, color(126, 137, 160));
        return;
    }

    const int columns = classicView ? kClassicColumns : kCoverColumns;
    const bool discoveryVisible = !backlogTab && !favoritesTab;
    const int classicRowStride = discoveryVisible ? 236 : 244;
    const int rowStride = classicView ? classicRowStride : 372;
    const int rows = touchBrowse ? 3 :
                     (classicView ? kClassicRows : kCoverRows);
    const int visibleCount = columns * rows;
    const int selectedRow = selected / columns;
    const int firstRow = touchBrowse ? touchScrollY / rowStride :
                         std::max(0, selectedRow - (rows - 1));
    const int firstIndex = firstRow * columns;
    const int gridY = discoveryVisible ? 190 : 160;
    const int scrollRemainder = touchBrowse ? touchScrollY % rowStride : 0;
    int renderedGames = 0;
    for (int slot = 0; slot < visibleCount; ++slot) {
        const int index = firstIndex + slot;
        if (index >= static_cast<int>(games.size())) break;
        const int column = slot % columns;
        const int row = slot / columns;
        const float reveal = gridRevealProgress(slot, gridRevealStart);
        const bool isFav = isFavorite(games[index]->id);
        const BacklogStatus bStatus = getBacklogStatus(games[index]->id);
        if (classicView) {
            drawCard(renderer, text, images, *games[index],
                     42 + column * 307, gridY - scrollRemainder + row * classicRowStride,
                     !touchBrowse && index == selected, reveal, isFav);
        } else {
            const float focus = touchBrowse ? 0.0f :
                selectionFocus(index, selected, previousSelected, selectionAnimationStart);
            drawCoverCard(renderer, text, images, *games[index],
                          42 + column * 244, gridY - scrollRemainder + row * 372, focus, reveal,
                          isFav, bStatus);
        }
        ++renderedGames;
    }
    if (!classicView && showSummary && selected >= 0 && selected < static_cast<int>(games.size())) {
        const bool isFav = isFavorite(games[selected]->id);
        const BacklogStatus bStatus = getBacklogStatus(games[selected]->id);
        drawSelectedSummary(renderer, text, images, *games[selected],
                            gridRevealProgress(renderedGames, gridRevealStart),
                            isFav, bStatus);
    }
}

}  // namespace vitrine
