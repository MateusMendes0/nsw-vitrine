#include "catalog.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace vitrine {
namespace {

bool containsNormalized(const std::string& value, const std::string& needle) {
    return normalizeForSearch(value).find(needle) != std::string::npos;
}

std::vector<Game> demoGames() {
    // Dados ficticios: permitem experimentar a interface sem redistribuir capas
    // ou apresentar scores externos como se fossem dados oficiais.
    std::vector<Game> games = {
        {"astral-trails", "Astral Trails", "Uma jornada alem das nuvens",
         "Explore ilhas flutuantes, restaure constelacoes e encontre atalhos em um mundo compacto feito para ser descoberto no seu ritmo.",
         "Northstar Studio", {"Aventura", "Indie"}, GameType::Game, 2025, 91.0f, 14.5f, 31.0f,
         {53, 83, 186}, {126, 78, 201}},
        {"circuit-bloom", "Circuit Bloom", "Cultive uma cidade que pensa",
         "Um jogo de estrategia acolhedor sobre energia, automacao e jardins urbanos. Cada bairro reage as escolhas do jogador.",
         "Soft Current", {"Estrategia", "Simulacao"}, GameType::Game, 2024, 86.0f, 22.0f, 48.0f,
         {27, 142, 121}, {16, 77, 98}},
        {"emberbound", "Emberbound", "Toda chama guarda uma historia",
         "RPG de acao com combates precisos, companheiros memoraveis e uma campanha que muda conforme os juramentos que voce aceita.",
         "Copper Finch", {"RPG", "Acao"}, GameType::Game, 2026, 94.0f, 37.0f, 72.0f,
         {214, 75, 50}, {91, 27, 65}},
        {"tiny-orbits", "Tiny Orbits", "Pequenos planetas, grandes ideias",
         "Resolva quebra-cabecas gravitacionais em fases curtas. Ideal para sessoes rapidas no modo portatil.",
         "Pocket Comet", {"Puzzle", "Indie"}, GameType::Game, 2025, 83.0f, 7.0f, 12.5f,
         {32, 152, 209}, {20, 50, 111}},
        {"neon-apex", "Neon Apex", "Velocidade depois da meia-noite",
         "Corridas arcade com pistas dinamicas, musica reativa e campeonatos locais para ate quatro pilotos.",
         "Afterlight Games", {"Corrida", "Arcade"}, GameType::Game, 2023, 79.0f, 9.0f, 24.0f,
         {239, 54, 128}, {80, 34, 171}},
        {"moss-and-metal", "Moss & Metal", "A floresta recupera o futuro",
         "Aventura de plataforma sobre um pequeno robo que transforma sucata em ferramentas para recuperar um ecossistema esquecido.",
         "Fern Assembly", {"Plataforma", "Aventura"}, GameType::Game, 2024, 88.0f, 11.0f, 19.0f,
         {80, 159, 91}, {36, 80, 67}},
        {"paper-legends", "Paper Legends", "Dobre o mapa. Mude o destino.",
         "Um RPG tatico em um mundo de papel. Dobre o cenario para criar cobertura, pontes e novas rotas durante as batalhas.",
         "Folded Moon", {"RPG", "Estrategia"}, GameType::Game, 2025, 90.0f, 28.0f, 61.0f,
         {230, 166, 59}, {174, 66, 56}},
        {"deep-signal", "Deep Signal", "Algo respondeu do abismo",
         "Misterio narrativo em uma estacao submarina. Investigue transmissoes, conecte pistas e decida em quem confiar.",
         "Low Frequency", {"Aventura", "Misterio"}, GameType::Game, 2026, 87.0f, 8.5f, 13.0f,
         {24, 96, 139}, {12, 32, 62}},
        {"astral-trails-demo", "Astral Trails: Demo", "Experimente o primeiro arquipelago",
         "Versao demonstrativa com a abertura da campanha e progresso separado da versao completa.",
         "Northstar Studio", {"Aventura", "Indie"}, GameType::Demo, 2025, 0.0f, 1.5f, 2.0f,
         {70, 98, 199}, {146, 91, 207}},
        {"emberbound-ashes", "Emberbound: Echoes of Ash", "Uma nova regiao desperta",
         "Conteudo adicional com uma regiao, dois companheiros e uma linha de missoes para o jogo base Emberbound.",
         "Copper Finch", {"RPG", "Acao"}, GameType::Dlc, 2026, 89.0f, 8.0f, 15.0f,
         {193, 63, 42}, {62, 23, 74}},
    };
    if (games.size() >= 10) {
        games[0].gameModes = {"Single player"};
        games[1].gameModes = {"Single player"};
        games[2].gameModes = {"Single player", "Co-operative"};
        games[2].releaseDate = "2026-10-15";
        games[3].gameModes = {"Single player", "Co-operative"};
        games[4].gameModes = {"Multiplayer", "Split screen", "Co-operative"};
        games[5].gameModes = {"Single player"};
        games[6].gameModes = {"Single player"};
        games[7].gameModes = {"Single player"};
        games[7].releaseDate = "2026-11-20";
        games[8].gameModes = {"Single player"};
        games[9].gameModes = {"Single player", "Co-operative"};
        games[9].releaseDate = "2026-12-05";
    }
    return games;
}

}  // namespace

const char* gameTypeLabel(GameType type) {
    switch (type) {
        case GameType::Game: return "Jogo";
        case GameType::Demo: return "Demo";
        case GameType::Dlc: return "DLC";
    }
    return "Jogo";
}

const char* backlogStatusLabel(BacklogStatus status) {
    switch (status) {
        case BacklogStatus::None: return "Sem status";
        case BacklogStatus::WantToPlay: return "Quero jogar";
        case BacklogStatus::Playing: return "Jogando";
        case BacklogStatus::Completed: return "Finalizado";
        case BacklogStatus::Dropped: return "Abandonado";
    }
    return "Sem status";
}

const char* sortModeLabel(SortMode mode) {
    switch (mode) {
        case SortMode::Score: return "Maior score";
        case SortMode::Popular: return "Mais populares";
        case SortMode::Title: return "A-Z";
        case SortMode::Shortest: return "Mais curtos";
        case SortMode::Release: return "Lancamento";
    }
    return "Maior score";
}

SortMode nextSortMode(SortMode mode) {
    switch (mode) {
        case SortMode::Score: return SortMode::Popular;
        case SortMode::Popular: return SortMode::Title;
        case SortMode::Title: return SortMode::Shortest;
        case SortMode::Shortest: return SortMode::Release;
        case SortMode::Release: return SortMode::Score;
    }
    return SortMode::Score;
}

const char* gameModeFilterLabel(GameModeFilter mode) {
    switch (mode) {
        case GameModeFilter::All: return "Todos";
        case GameModeFilter::SinglePlayer: return "Single-player";
        case GameModeFilter::CoOp: return "Co-op Local / 2 Jogadores";
        case GameModeFilter::Multiplayer: return "Multiplayer Online";
    }
    return "Todos";
}

std::string formatExpectedRelease(const std::string& releaseDate, int releaseYear) {
    if (releaseDate.empty() || releaseDate == "----") {
        return releaseYear > 0 ? std::to_string(releaseYear) : "--";
    }

    static const char* ptMonths[] = {
        "", "Jan", "Fev", "Mar", "Abr", "Mai", "Jun",
        "Jul", "Ago", "Set", "Out", "Nov", "Dez"
    };

    if (releaseDate.size() >= 7 && releaseDate[4] == '-') {
        const int year = std::atoi(releaseDate.substr(0, 4).c_str());
        const int month = std::atoi(releaseDate.substr(5, 2).c_str());
        int day = 0;
        if (releaseDate.size() >= 10 && releaseDate[7] == '-') {
            day = std::atoi(releaseDate.substr(8, 2).c_str());
        }
        if (month >= 1 && month <= 12) {
            if (day > 0) {
                return std::to_string(day) + "/" + ptMonths[month];
            }
            return std::string(ptMonths[month]) + " " + std::to_string(year > 0 ? year : releaseYear);
        }
    }

    if (releaseDate.find("Q1") != std::string::npos ||
        releaseDate.find("Q2") != std::string::npos ||
        releaseDate.find("Q3") != std::string::npos ||
        releaseDate.find("Q4") != std::string::npos) {
        return releaseDate;
    }

    static const char* enMonths[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    for (int m = 0; m < 12; ++m) {
        const auto pos = releaseDate.find(enMonths[m]);
        if (pos != std::string::npos) {
            int day = 0;
            std::size_t i = pos + 3;
            while (i < releaseDate.size() && (releaseDate[i] == ' ' || releaseDate[i] == '.')) ++i;
            if (i < releaseDate.size() && std::isdigit(static_cast<unsigned char>(releaseDate[i]))) {
                day = std::atoi(releaseDate.c_str() + i);
            }
            if (day > 0 && day <= 31) {
                return std::to_string(day) + "/" + ptMonths[m + 1];
            }
            int year = releaseYear;
            for (std::size_t yIdx = 0; yIdx + 3 < releaseDate.size(); ++yIdx) {
                if (std::isdigit(static_cast<unsigned char>(releaseDate[yIdx])) &&
                    std::isdigit(static_cast<unsigned char>(releaseDate[yIdx + 1])) &&
                    std::isdigit(static_cast<unsigned char>(releaseDate[yIdx + 2])) &&
                    std::isdigit(static_cast<unsigned char>(releaseDate[yIdx + 3]))) {
                    year = std::atoi(releaseDate.substr(yIdx, 4).c_str());
                    break;
                }
            }
            if (year > 0) {
                return std::string(ptMonths[m + 1]) + " " + std::to_string(year);
            }
            return ptMonths[m + 1];
        }
    }

    if (releaseYear > 0) {
        return std::to_string(releaseYear);
    }
    return releaseDate;
}

std::string normalizeForSearch(const std::string& value) {
    std::string result;
    result.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char current = static_cast<unsigned char>(value[i]);
        if (current == 0xC3 && i + 1 < value.size()) {
            const unsigned char next = static_cast<unsigned char>(value[++i]);
            switch (next) {
                case 0x80: case 0x81: case 0x82: case 0x83: case 0xA0: case 0xA1: case 0xA2: case 0xA3:
                    result += 'a'; break;
                case 0x87: case 0xA7: result += 'c'; break;
                case 0x88: case 0x89: case 0x8A: case 0xA8: case 0xA9: case 0xAA:
                    result += 'e'; break;
                case 0x8C: case 0x8D: case 0x8E: case 0xAC: case 0xAD: case 0xAE:
                    result += 'i'; break;
                case 0x92: case 0x93: case 0x94: case 0x95: case 0xB2: case 0xB3: case 0xB4: case 0xB5:
                    result += 'o'; break;
                case 0x99: case 0x9A: case 0x9B: case 0xB9: case 0xBA: case 0xBB:
                    result += 'u'; break;
                default: break;
            }
        } else if (current < 128) {
            result += static_cast<char>(std::tolower(current));
        }
    }
    return result;
}

Catalog::Catalog() : games_(demoGames()) {}

Catalog::Catalog(std::vector<Game> games) : games_(std::move(games)) {}

const std::vector<Game>& Catalog::all() const {
    return games_;
}

void Catalog::replace(std::vector<Game> games) {
    games_ = std::move(games);
}

std::vector<const Game*> Catalog::filtered(const CatalogFilter& filter) const {
    std::vector<const Game*> result;
    const std::string query = normalizeForSearch(filter.query);

    for (const Game& game : games_) {
        if (!filter.genre.empty() &&
            std::find(game.genres.begin(), game.genres.end(), filter.genre) == game.genres.end()) {
            continue;
        }
        if (filter.acclaimedOnly && game.score < 80.0f) {
            continue;
        }
        if (filter.upcomingOnly && game.releaseYear <= 2025) {
            continue;
        }
        if (filter.gameMode != GameModeFilter::All) {
            bool matches = false;
            for (const std::string& mode : game.gameModes) {
                const std::string normalized = normalizeForSearch(mode);
                if (filter.gameMode == GameModeFilter::SinglePlayer) {
                    if (normalized.find("single") != std::string::npos) {
                        matches = true;
                        break;
                    }
                } else if (filter.gameMode == GameModeFilter::CoOp) {
                    if (normalized.find("co-op") != std::string::npos ||
                        normalized.find("coop") != std::string::npos ||
                        normalized.find("co-operative") != std::string::npos ||
                        normalized.find("cooperative") != std::string::npos ||
                        normalized.find("split") != std::string::npos) {
                        matches = true;
                        break;
                    }
                } else if (filter.gameMode == GameModeFilter::Multiplayer) {
                    if (normalized.find("multiplayer") != std::string::npos ||
                        normalized.find("online") != std::string::npos ||
                        normalized.find("battle royale") != std::string::npos ||
                        normalized.find("mmo") != std::string::npos) {
                        matches = true;
                        break;
                    }
                }
            }
            if (!matches) continue;
        }
        if (!query.empty()) {
            bool match = containsNormalized(game.title, query) ||
                         containsNormalized(game.studio, query) ||
                         containsNormalized(game.tagline, query);
            for (const std::string& genre : game.genres) {
                match = match || containsNormalized(genre, query);
            }
            if (!match) continue;
        }
        result.push_back(&game);
    }

    if (filter.preserveSourceOrder) return result;

    std::stable_sort(result.begin(), result.end(), [&filter](const Game* left, const Game* right) {
        switch (filter.sort) {
            case SortMode::Score:
                if (left->score != right->score) return left->score > right->score;
                return left->title < right->title;
            case SortMode::Popular:
                if (left->ratingsCount != right->ratingsCount) return left->ratingsCount > right->ratingsCount;
                return left->title < right->title;
            case SortMode::Title:
                return normalizeForSearch(left->title) < normalizeForSearch(right->title);
            case SortMode::Shortest: {
                const bool leftHas = left->mainHours > 0.0f;
                const bool rightHas = right->mainHours > 0.0f;
                if (leftHas != rightHas) return leftHas;
                if (left->mainHours != right->mainHours) return left->mainHours < right->mainHours;
                return left->title < right->title;
            }
            case SortMode::Release:
                if (left->releaseYear != right->releaseYear) return left->releaseYear > right->releaseYear;
                return left->title < right->title;
        }
        return false;
    });
    return result;
}

std::vector<std::string> Catalog::genres() const {
    std::set<std::string> unique;
    for (const Game& game : games_) {
        unique.insert(game.genres.begin(), game.genres.end());
    }
    return {unique.begin(), unique.end()};
}

}  // namespace vitrine
