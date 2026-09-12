#include "panels_view.hpp"

#include <algorithm>
#include <cstdio>

namespace vitrine {

std::string PanelsView::formatCacheSize(std::uint64_t bytes) {
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

std::string PanelsView::filterOptionLabel(int filterSection, int index,
                                          const std::vector<std::string>& genres) {
    if (filterSection == 0) {
        if (index >= 0 && index < static_cast<int>(genres.size())) {
            return genres[index];
        }
        return "";
    }
    if (filterSection == 1) {
        static const char* highlights[] = {"Todos", "Aclamados (80+)", "Lancamentos"};
        return highlights[index];
    }
    if (filterSection == 2) {
        static const char* modes[] = {
            "Todos",
            "Single-player",
            "Co-op Local / 2 Jogadores",
            "Multiplayer Online"
        };
        return modes[index];
    }
    if (filterSection == 3) {
        static const char* sorts[] = {"Maior score", "Mais populares", "A-Z", "Mais curtos", "Lancamento"};
        return sorts[index];
    }
    if (filterSection == 4) {
        static const char* statuses[] = {"Todos", "Quero jogar", "Jogando", "Finalizados", "Abandonados"};
        return statuses[index];
    }
    return "";
}

void PanelsView::renderFilterPanel(SDL_Renderer* renderer, TextRenderer& text,
                                   int filterSection, int filterOption, int currentApplied,
                                   bool backlogTab, const std::vector<std::string>& genres) {
    fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 6, 13, 218));
    fillRoundedRect(renderer, 82, 60, 1116, 600, 24, color(16, 22, 37));
    text.draw(renderer, "Filtros e ordenacao", 122, 89, 28, color(244, 246, 252));
    text.draw(renderer, "Escolha diretamente; a API so atualiza ao aplicar.", 122, 126, 18, color(132, 145, 174));

    static const char* tabs[] = {"Genero", "Destaque", "Modo", "Ordenar", "Status"};
    const int tabCount = backlogTab ? 5 : 4;
    for (int tab = 0; tab < tabCount; ++tab) {
        const int tabWidth = backlogTab ? 194 : 247;
        const int gap = 16;
        const int x = 122 + tab * (tabWidth + gap);
        const bool active = tab == filterSection;
        fillRoundedRect(renderer, x, 164, tabWidth, 52, 14, active ? color(75, 91, 205) : color(25, 33, 51));
        const int labelWidth = text.width(tabs[tab], 22);
        text.draw(renderer, tabs[tab], x + (tabWidth - labelWidth) / 2, 178, 22,
                  active ? color(247, 248, 253) : color(145, 156, 181));
    }

    int count = 5;
    int columns = 3;
    if (filterSection == 0) {
        count = static_cast<int>(genres.size());
        columns = 4;
    } else if (filterSection == 1) {
        count = 3;
        columns = 3;
    } else if (filterSection == 2) {
        count = 4;
        columns = 2;
    } else if (filterSection == 3) {
        count = 5;
        columns = 3;
    } else if (filterSection == 4) {
        count = 5;
        columns = 5;
    }

    const int gap = 16;
    const int areaWidth = 1036;
    const int optionWidth = (areaWidth - gap * (columns - 1)) / columns;
    const int optionHeight = filterSection == 0 ? 52 : 72;
    const int startY = filterSection == 0 ? 232 : 282;

    for (int index = 0; index < count; ++index) {
        const int column = index % columns;
        const int row = index / columns;
        const int x = 122 + column * (optionWidth + gap);
        const int y = startY + row * (optionHeight + (filterSection == 0 ? 10 : 16));
        const bool focused = index == filterOption;
        const bool applied = index == currentApplied;
        if (focused) fillRoundedRect(renderer, x - 4, y - 4, optionWidth + 8, optionHeight + 8, 15, color(116, 133, 255));
        fillRoundedRect(renderer, x, y, optionWidth, optionHeight, 12,
                        focused ? color(38, 48, 76) : color(23, 30, 47));
        if (applied) fillRoundedRect(renderer, x + 14, y + optionHeight / 2 - 5, 10, 10, 5, color(112, 224, 172));
        const std::string label = filterOptionLabel(filterSection, index, genres);
        text.draw(renderer, label, x + (applied ? 34 : 18), y + optionHeight / 2 - 12,
                  filterSection == 0 ? 18 : 22,
                  focused ? color(246, 248, 253) : color(190, 199, 218), optionWidth - 48);
    }

    text.draw(renderer, "L / R  Categoria", 122, 620, 18, color(151, 167, 255));
    text.draw(renderer, "A  Aplicar", 958, 620, 18, color(214, 220, 234));
    text.draw(renderer, "B  Voltar", 1082, 620, 18, color(160, 172, 196));
}

void PanelsView::renderBacklogPanel(SDL_Renderer* renderer, TextRenderer& text,
                                    const Game& game, int backlogOption,
                                    BacklogStatus currentStatus) {
    fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 6, 13, 205));
    fillRoundedRect(renderer, 320, 112, 640, 496, 24, color(16, 22, 37));
    text.draw(renderer, "Minha lista", 360, 145, 28, color(244, 246, 252));
    text.draw(renderer, game.title, 360, 184, 18, color(151, 167, 255), 550);
    text.draw(renderer, "Escolha o estado deste jogo", 360, 217, 18, color(132, 145, 174));

    for (int option = 0; option < 5; ++option) {
        const int y = 254 + option * 62;
        const bool focused = option == backlogOption;
        if (focused) fillRoundedRect(renderer, 352, y - 4, 576, 56, 14, color(112, 130, 255));
        fillRoundedRect(renderer, 356, y, 568, 48, 11,
                        focused ? color(38, 48, 76) : color(24, 31, 49));
        const auto value = static_cast<BacklogStatus>(option);
        const char* optionLabel = option == 0 && currentStatus != BacklogStatus::None
            ? "Remover da lista"
            : backlogStatusLabel(value);
        text.draw(renderer, optionLabel, 380, y + 11, 18,
                  focused ? color(246, 248, 253) : color(187, 197, 217));
        if (currentStatus == value) {
            fillRoundedRect(renderer, 885, y + 17, 10, 10, 5, color(112, 224, 172));
        }
    }
    text.draw(renderer, "A  Aplicar", 360, 570, 18, color(224, 230, 246));
    text.draw(renderer, "B / ZR  Voltar", 754, 570, 18, color(160, 172, 196));
}

void PanelsView::renderAbout(SDL_Renderer* renderer, TextRenderer& text,
                             int aboutOption, bool networkReady, bool apiInitialized,
                             bool initialSyncRunning, bool usingApi,
                             std::uint64_t aboutCacheBytes, const std::string& aboutMessage,
                             bool aboutConfirmClear, const std::string& updateSubtitle) {
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
    const std::string connection = networkReady && apiInitialized
        ? (initialSyncRunning ? "Rede disponivel • atualizando" : "Rede disponivel")
        : "Offline • usando dados locais";
    text.draw(renderer, connection, 670, 218, 22,
              networkReady && apiInitialized ? color(116, 235, 181) : color(216, 178, 118), 400);

    fillRoundedRect(renderer, 178, 266, 452, 72, 15, color(23, 30, 48));
    text.draw(renderer, "FONTE DOS DADOS", 198, 278, 18, color(113, 129, 166));
    text.draw(renderer, usingApi ? "IGDB via Cloudflare Worker" : "Catalogo demonstrativo",
              198, 306, 22, color(225, 230, 244), 400);
    fillRoundedRect(renderer, 650, 266, 448, 72, 15, color(23, 30, 48));
    text.draw(renderer, "CACHE LOCAL", 670, 278, 18, color(113, 129, 166));
    text.draw(renderer, formatCacheSize(aboutCacheBytes), 670, 306, 22, color(225, 230, 244));

    text.draw(renderer, "ACOES", 178, 365, 18, color(113, 129, 166));
    static const char* titles[] = {"Atualizar dados", "Atualizar app", "Limpar cache", "Fechar"};
    const std::string subtitles[] = {
        "Consulta o catalogo", updateSubtitle, "Capas e detalhes", "Voltar para a vitrine"
    };
    for (int option = 0; option < 4; ++option) {
        const int x = 178 + option * 232;
        const bool focused = option == aboutOption;
        if (focused) fillRoundedRect(renderer, x - 4, 394, 228, 108, 18, color(112, 130, 255));
        fillRoundedRect(renderer, x, 398, 220, 100, 14,
                        focused ? color(38, 48, 76) : color(23, 30, 48));
        text.draw(renderer, titles[option], x + 20, 418, 22,
                  focused ? color(246, 248, 253) : color(205, 213, 230), 180);
        text.draw(renderer, subtitles[option], x + 20, 454, 18, color(126, 140, 171), 180);
    }

    text.draw(renderer, aboutMessage, 178, 528, 18, color(151, 167, 255), 920);
    text.draw(renderer, "Favoritos e Minha lista nao fazem parte do cache e nunca sao apagados.",
              178, 563, 18, color(130, 143, 170), 920);
    text.draw(renderer, "Esquerda / Direita  Escolher", 178, 626, 18, color(151, 167, 255));
    text.draw(renderer, "A  Confirmar", 828, 626, 18, color(224, 230, 246));
    text.draw(renderer, "B  Voltar", 990, 626, 18, color(178, 189, 211));

    if (!aboutConfirmClear) return;
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

void PanelsView::renderUpdateDialog(SDL_Renderer* renderer, TextRenderer& text,
                                    UpdateDialogState state, const UpdateInfo& info,
                                    int option, std::uint64_t downloadedBytes,
                                    const std::string& message) {
    fillRect(renderer, 0, 0, kWidth, kHeight, color(3, 6, 13, 226));
    fillRoundedRect(renderer, 240, 90, 800, 540, 26, color(16, 22, 37));
    fillRoundedRect(renderer, 240, 90, 7, 540, 3,
                    state == UpdateDialogState::Error ? color(218, 112, 112) : color(112, 130, 255));

    if (state == UpdateDialogState::Available) {
        text.draw(renderer, "Atualizacao disponivel", 290, 132, 32, color(246, 248, 252));
        text.draw(renderer, "Vitrine v" + std::string(kAppVersion) + "  →  v" + info.version,
                  290, 181, 22, color(151, 167, 255));
        text.draw(renderer, "NOVIDADES", 290, 230, 17, color(113, 129, 166));
        text.drawWrapped(renderer, info.notes.empty() ? "Nova versao publicada no GitHub." : info.notes,
                         290, 260, 20, color(205, 213, 230), 700, 4);
        fillRoundedRect(renderer, 290, 388, 700, 76, 14, color(23, 34, 46));
        text.drawWrapped(renderer,
                         "O update altera somente o NRO do Vitrine no cartao SD. "
                         "Sphaira, Atmosphere, saves e NAND nao sao modificados.",
                         312, 405, 18, color(116, 235, 181), 656, 2);
        const int buttonX[] = {300, 694};
        const char* labels[] = {"Atualizar", "Ignorar por enquanto"};
        for (int index = 0; index < 2; ++index) {
            const bool focused = option == index;
            if (focused) fillRoundedRect(renderer, buttonX[index] - 4, 496, 294, 78, 18, color(112, 130, 255));
            fillRoundedRect(renderer, buttonX[index], 500, 286, 70, 14,
                            focused ? color(38, 48, 76) : color(24, 31, 49));
            const int labelWidth = text.width(labels[index], 20);
            text.draw(renderer, labels[index], buttonX[index] + (286 - labelWidth) / 2,
                      523, 20, focused ? color(246, 248, 253) : color(185, 196, 217));
        }
        text.draw(renderer, "A  Confirmar", 290, 594, 18, color(224, 230, 246));
        text.draw(renderer, "B  Ignorar", 840, 594, 18, color(160, 172, 196));
        return;
    }

    if (state == UpdateDialogState::Installing) {
        text.draw(renderer, "Atualizando o Vitrine", 290, 142, 32, color(246, 248, 252));
        text.draw(renderer, "Baixando v" + info.version + " por HTTPS", 290, 194, 20, color(151, 167, 255));
        const double ratio = info.size > 0
            ? std::min(1.0, static_cast<double>(downloadedBytes) / static_cast<double>(info.size)) : 0.0;
        fillRoundedRect(renderer, 290, 270, 700, 34, 17, color(28, 36, 55));
        fillRoundedRect(renderer, 290, 270, static_cast<int>(700.0 * ratio), 34, 17, color(112, 130, 255));
        char progress[96]{};
        std::snprintf(progress, sizeof(progress), "%.0f%%  •  %.1f / %.1f MB", ratio * 100.0,
                      static_cast<double>(downloadedBytes) / (1024.0 * 1024.0),
                      static_cast<double>(info.size) / (1024.0 * 1024.0));
        text.draw(renderer, progress, 290, 322, 20, color(205, 213, 230));
        text.drawWrapped(renderer, message, 290, 378, 20, color(188, 198, 219), 700, 3);
        text.drawWrapped(renderer,
                         "O NRO atual permanece intacto durante o download. "
                         "A troca so acontece depois das validacoes de tamanho, SHA-256 e formato NRO.",
                         290, 458, 18, color(116, 235, 181), 700, 3);
        text.draw(renderer, "B  Cancelar download", 775, 580, 18, color(183, 193, 214));
        return;
    }

    const bool installed = state == UpdateDialogState::Installed;
    text.draw(renderer, installed ? "Atualizacao concluida" : "Atualizacao nao instalada",
              290, 145, 32, installed ? color(116, 235, 181) : color(235, 151, 151));
    text.drawWrapped(renderer, message, 290, 215, 22, color(215, 222, 237), 700, 4);
    if (installed) {
        text.drawWrapped(renderer,
                         "O NRO anterior foi mantido como arquivo .bak para recuperacao. "
                         "Volte ao Sphaira e abra o Vitrine novamente.",
                         290, 350, 19, color(151, 167, 255), 700, 3);
        text.draw(renderer, "A / B  Voltar ao Sphaira", 705, 570, 18, color(224, 230, 246));
    } else {
        text.drawWrapped(renderer,
                         "Quando a validacao falha, o arquivo baixado e descartado. "
                         "O Sphaira e os dados do usuario nao sao alterados.",
                         290, 350, 19, color(151, 167, 255), 700, 3);
        text.draw(renderer, "A / B  Fechar", 820, 570, 18, color(224, 230, 246));
    }
}

}  // namespace vitrine
