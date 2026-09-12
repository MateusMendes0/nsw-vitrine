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
