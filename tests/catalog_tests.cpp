#include "catalog.hpp"
#include "input.hpp"
#include "update_utils.hpp"

#include <cassert>
#include <iostream>

int main() {
    vitrine::Catalog catalog;
    assert(catalog.all().size() == 10);

    vitrine::CatalogFilter filter;
    filter.query = "rpg";
    auto games = catalog.filtered(filter);
    assert(games.size() == 3);

    filter.query = "estrategia";
    games = catalog.filtered(filter);
    assert(games.size() == 2);

    filter.query.clear();
    filter.genre = "Aventura";
    games = catalog.filtered(filter);
    assert(games.size() == 4);

    filter.genre.clear();
    filter.sort = vitrine::SortMode::Shortest;
    games = catalog.filtered(filter);
    assert(games.front()->id == "astral-trails-demo");

    filter.sort = vitrine::SortMode::Score;
    filter.acclaimedOnly = true;
    games = catalog.filtered(filter);
    assert(games.size() == 8);
    for (const auto* g : games) {
        assert(g->score >= 80.0f);
    }

    filter.acclaimedOnly = false;
    filter.upcomingOnly = true;
    games = catalog.filtered(filter);
    assert(games.size() == 3);
    for (const auto* g : games) {
        assert(g->releaseYear > 2025);
    }

    filter.upcomingOnly = false;
    filter.sort = vitrine::SortMode::Release;
    games = catalog.filtered(filter);
    assert(games.front()->releaseYear == 2026);

    filter.preserveSourceOrder = true;
    games = catalog.filtered(filter);
    assert(games.front()->id == catalog.all().front().id);

    filter.preserveSourceOrder = false;
    filter.gameMode = vitrine::GameModeFilter::SinglePlayer;
    games = catalog.filtered(filter);
    assert(!games.empty());
    for (const auto* g : games) {
        bool hasSingle = false;
        for (const auto& m : g->gameModes) {
            if (m.find("Single") != std::string::npos) hasSingle = true;
        }
        assert(hasSingle);
    }

    filter.gameMode = vitrine::GameModeFilter::CoOp;
    games = catalog.filtered(filter);
    assert(!games.empty());

    filter.gameMode = vitrine::GameModeFilter::Multiplayer;
    games = catalog.filtered(filter);
    assert(games.size() == 1);
    assert(games.front()->id == "neon-apex");

    filter.gameMode = vitrine::GameModeFilter::All;

    assert(std::string(vitrine::gameModeFilterLabel(vitrine::GameModeFilter::All)) == "Todos");
    assert(std::string(vitrine::gameModeFilterLabel(vitrine::GameModeFilter::SinglePlayer)) == "Single-player");
    assert(std::string(vitrine::gameModeFilterLabel(vitrine::GameModeFilter::CoOp)) == "Co-op Local / 2 Jogadores");
    assert(std::string(vitrine::gameModeFilterLabel(vitrine::GameModeFilter::Multiplayer)) == "Multiplayer Online");

    assert(vitrine::formatExpectedRelease("2026-10-15", 2026) == "15/Out");
    assert(vitrine::formatExpectedRelease("2026-11-00", 2026) == "Nov 2026");
    assert(vitrine::formatExpectedRelease("2026-11", 2026) == "Nov 2026");
    assert(vitrine::formatExpectedRelease("Oct 15, 2026", 2026) == "15/Out");
    assert(vitrine::formatExpectedRelease("November 2026", 2026) == "Nov 2026");
    assert(vitrine::formatExpectedRelease("Q1 2027", 2027) == "Q1 2027");
    assert(vitrine::formatExpectedRelease("", 2027) == "2027");
    assert(vitrine::formatExpectedRelease("", 0) == "--");

    assert(std::string(vitrine::sortModeLabel(vitrine::SortMode::Popular)) == "Mais populares");
    assert(std::string(vitrine::sortModeLabel(vitrine::SortMode::Release)) == "Lancamento");
    assert(std::string(vitrine::backlogStatusLabel(vitrine::BacklogStatus::WantToPlay)) == "Quero jogar");
    assert(std::string(vitrine::backlogStatusLabel(vitrine::BacklogStatus::Completed)) == "Finalizado");

    assert(vitrine::normalizeForSearch("Ação e Simulação") == "acao e simulacao");
    assert(vitrine::compareSemanticVersions("1.0.0", "v1.0.1") < 0);
    assert(vitrine::compareSemanticVersions("v2.0", "1.9.9") > 0);
    assert(vitrine::compareSemanticVersions("1.0.0+build", "1.0") == 0);
    assert(vitrine::sha256Hex("") ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(vitrine::sha256Hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    vitrine::Input touch;
    touch.touchReleased = true;
    touch.touchStartX = 500;
    touch.touchStartY = 300;
    touch.touchX = 390;
    touch.touchY = 310;
    assert(vitrine::touchGestureDirection(touch) == vitrine::TouchGestureDirection::Left);
    touch.touchX = 515;
    touch.touchY = 220;
    assert(vitrine::touchGestureDirection(touch) == vitrine::TouchGestureDirection::Up);
    touch.touchX = 530;
    touch.touchY = 320;
    assert(vitrine::touchGestureDirection(touch) == vitrine::TouchGestureDirection::None);
    std::cout << "catalog_tests: OK\n";
    return 0;
}
