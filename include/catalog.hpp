#pragma once

#include "game.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace vitrine {

enum class SortMode {
    Score,
    Popular,
    Title,
    Shortest,
    Release,
};

struct CatalogFilter {
    std::string query;
    std::string genre;
    SortMode sort = SortMode::Score;
    bool acclaimedOnly = false;
    bool upcomingOnly = false;
    bool preserveSourceOrder = false;
};

class Catalog {
public:
    Catalog();
    explicit Catalog(std::vector<Game> games);

    const std::vector<Game>& all() const;
    void replace(std::vector<Game> games);
    std::vector<const Game*> filtered(const CatalogFilter& filter) const;
    std::vector<std::string> genres() const;

private:
    std::vector<Game> games_;
};

const char* sortModeLabel(SortMode mode);
SortMode nextSortMode(SortMode mode);
std::string normalizeForSearch(const std::string& value);

}  // namespace vitrine
