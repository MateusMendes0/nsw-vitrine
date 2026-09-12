#include "layout_view.hpp"

#include <algorithm>
#include <cmath>

namespace vitrine {

const char* LayoutView::discoveryLabel(int index) {
    static const char* labels[] = {
        "Todos", "Populares", "Lancamentos", "Bem avaliados", "Indies", "Joias escondidas", "Em breve"
    };
    return labels[std::max(0, std::min(index, 6))];
}

void LayoutView::renderBackground(SDL_Renderer* renderer) {
    for (int y = 0; y < kHeight; ++y) {
        const float t = static_cast<float>(y) / kHeight;
        setColor(renderer, color(static_cast<Uint8>(12 + 6 * t),
                                 static_cast<Uint8>(17 + 5 * t),
                                 static_cast<Uint8>(30 + 10 * t)));
        SDL_RenderDrawLine(renderer, 0, y, kWidth, y);
    }
    setColor(renderer, color(42, 82, 190, 22));
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 160; ++i) {
        SDL_RenderDrawLine(renderer, 760 - i, 0, 1280, 520 - i);
    }
}

void LayoutView::renderHeader(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images,
                              const std::string& searchQuery) {
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
    const std::string searchText = searchQuery.empty() ? "Pesquisar jogos..." : searchQuery;
    text.draw(renderer, searchText, 755, 34, 18,
              searchQuery.empty() ? color(112, 122, 145) : color(231, 235, 245), 315);
    fillRoundedRect(renderer, 1110, 23, 126, 48, 16, color(27, 34, 52));
    text.draw(renderer, "X  Filtros", 1127, 35, 18, color(185, 194, 215));
}

void LayoutView::renderToolbar(SDL_Renderer* renderer, TextRenderer& text,
                              bool backlogTab, bool favoritesTab,
                              std::size_t gameCount, bool hasMore, bool usingApi,
                              const std::string& genreLabel,
                              const std::string& secondaryFilterLabel,
                              SortMode sortMode) {
    fillRoundedRect(renderer, 42, 103, 98, 42, 13,
                    (!backlogTab && !favoritesTab) ? color(66, 82, 190) : color(26, 33, 51));
    fillRoundedRect(renderer, 148, 103, 130, 42, 13,
                    backlogTab ? color(66, 82, 190) : color(26, 33, 51));
    fillRoundedRect(renderer, 286, 103, 112, 42, 13,
                    favoritesTab ? color(66, 82, 190) : color(26, 33, 51));
    text.draw(renderer, "Explorar", 55, 112, 18,
              (!backlogTab && !favoritesTab) ? color(244, 246, 252) : color(154, 166, 191));
    text.draw(renderer, "Minha lista", 163, 112, 18,
              backlogTab ? color(244, 246, 252) : color(154, 166, 191));
    text.draw(renderer, "Favoritos", 298, 112, 18,
              favoritesTab ? color(244, 246, 252) : color(154, 166, 191));
    const std::string count = std::to_string(gameCount) +
                              (hasMore && usingApi && !favoritesTab && !backlogTab ? "+" : "") +
                              (gameCount == 1 ? " item" : " itens");
    text.draw(renderer, count, 408, 116, 18, color(119, 131, 157), 70);
    fillRoundedRect(renderer, 484, 108, 2, 32, 1, color(47, 57, 78));

    drawFilterChip(renderer, text, 496, 103, 220, "Genero", genreLabel);
    drawFilterChip(renderer, text, 724, 103, 220, backlogTab ? "Status" : "Destaque",
                   secondaryFilterLabel);
    drawFilterChip(renderer, text, 952, 103, 286, "Ordenar", sortModeLabel(sortMode));
}

void LayoutView::drawFilterChip(SDL_Renderer* renderer, TextRenderer& text, int x, int y, int w,
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

void LayoutView::renderDiscoveryRibbon(SDL_Renderer* renderer, TextRenderer& text,
                                      bool backlogTab, bool favoritesTab,
                                      bool hasSimilar, const std::string& similarGameTitle,
                                      int discoveryIndex, bool discoveryFocus, int discoveryCursor) {
    if (backlogTab || favoritesTab) return;
    if (hasSimilar) {
        text.draw(renderer, "SEMELHANTES", 42, 158, 18, color(112, 225, 255));
        text.draw(renderer, similarGameTitle, 174, 157, 18, color(205, 214, 232), 700);
        text.draw(renderer, "B  Voltar aos detalhes", 1033, 157, 18,
                  color(151, 167, 255), 205);
        return;
    }
    text.draw(renderer, "DESCOBRIR", 42, 158, 18, color(112, 126, 157));
    static const int widths[] = {76, 112, 132, 158, 82, 164, 110};
    int x = 150;
    for (int index = 0; index < 7; ++index) {
        const bool active = index == discoveryIndex;
        const bool focused = discoveryFocus && index == discoveryCursor;
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
    if (discoveryFocus) {
        text.draw(renderer, "A  Abrir", 1052, 157, 18, color(205, 213, 231));
        text.draw(renderer, "Baixo  Jogos", 1135, 157, 18, color(137, 151, 181));
    } else {
        text.draw(renderer, "Cima  Navegar", 1112, 157, 18, color(137, 151, 181));
    }
}

void LayoutView::renderFooter(SDL_Renderer* renderer, TextRenderer& text,
                             bool hasSimilar, const std::string& query,
                             bool discoveryFocus, int discoveryIndex,
                             bool backlogTab, bool favoritesTab,
                             const std::string& status) {
    fillRect(renderer, 0, 652, kWidth, 68, color(9, 13, 23, 245));
    fillRect(renderer, 0, 652, kWidth, 1, color(38, 111, 190, 190));
    if (hasSimilar) {
        drawKeyHint(renderer, text, 42, 670, "B", "Jogo anterior");
        drawKeyHint(renderer, text, 208, 670, "A", "Detalhes", true);
        drawKeyHint(renderer, text, 332, 670, "L/R", "Abas");
        drawKeyHint(renderer, text, 436, 670, "R3", "Vista");
        drawKeyHint(renderer, text, 544, 670, "ZR", "Minha lista");
        drawKeyHint(renderer, text, 708, 670, "L3", "Favoritar");
        text.draw(renderer, status, 862, 674, 18, color(122, 137, 170), 376);
        return;
    }
    if (!query.empty()) {
        drawKeyHint(renderer, text, 42, 670, "B", "Limpar busca");
        drawKeyHint(renderer, text, 196, 670, "A", "Detalhes", true);
        drawKeyHint(renderer, text, 320, 670, "Y", "Nova busca");
        drawKeyHint(renderer, text, 458, 670, "X", "Filtros");
        drawKeyHint(renderer, text, 576, 670, "R3", "Vista");
        text.draw(renderer, "Busca: " + query, 704, 674, 18,
                  color(151, 181, 224), 534);
        return;
    }
    if (discoveryFocus) {
        text.draw(renderer, "← / →  Secao     ↓  Jogos", 42, 674, 18, color(151, 167, 255));
        drawKeyHint(renderer, text, 286, 670, "A", "Abrir", true);
        drawKeyHint(renderer, text, 390, 670, "B", discoveryIndex == 0 ? "Cancelar" : "Todos");
        drawKeyHint(renderer, text, 524, 670, "-", "Sobre");
        text.draw(renderer, status, 640, 674, 18, color(122, 137, 170), 455);
        drawKeyHint(renderer, text, 1138, 670, "+", "Sair");
        return;
    }
    drawKeyHint(renderer, text, 42, 670, "A", "Detalhes", true);
    if (backlogTab || favoritesTab) {
        drawKeyHint(renderer, text, 166, 670, "B", "Explorar");
        drawKeyHint(renderer, text, 286, 670, "L/R", "Abas");
        drawKeyHint(renderer, text, 390, 670, "X", "Filtros");
        drawKeyHint(renderer, text, 508, 670, "Y", "Buscar");
        drawKeyHint(renderer, text, 616, 670, "R3", "Vista");
        drawKeyHint(renderer, text, 724, 670, "-", "Sobre");
        text.draw(renderer, status, 830, 674, 18, color(122, 137, 170), 265);
    } else {
        drawKeyHint(renderer, text, 166, 670, "L/R", "Abas");
        drawKeyHint(renderer, text, 270, 670, "X", "Filtros");
        drawKeyHint(renderer, text, 388, 670, "Y", "Buscar");
        drawKeyHint(renderer, text, 496, 670, "ZL", "Surpresa");
        drawKeyHint(renderer, text, 634, 670, "R3", "Vista");
        drawKeyHint(renderer, text, 742, 670, "-", "Sobre");
        text.draw(renderer, status, 848, 674, 18, color(122, 137, 170), 247);
    }
    drawKeyHint(renderer, text, 1138, 670, "+", "Sair");
}

void LayoutView::renderTabTransition(SDL_Renderer* renderer, Uint32 tabTransitionStart,
                                     bool details, bool detailClosing) {
    if (tabTransitionStart == 0 || details || detailClosing) return;
    const float elapsed = static_cast<float>(SDL_GetTicks() - tabTransitionStart);
    const float linear = std::max(0.0f, std::min(1.0f, elapsed / 180.0f));
    if (linear >= 1.0f) return;
    const float eased = 1.0f - std::pow(1.0f - linear, 3.0f);
    fillRect(renderer, 0, 92, kWidth, 560,
             color(5, 8, 16, static_cast<Uint8>(82.0f * (1.0f - eased))));
}

}  // namespace vitrine
