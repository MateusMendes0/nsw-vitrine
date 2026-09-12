#pragma once

#include "game.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vitrine {

struct ApiResult {
    bool success = false;
    std::string message;
    std::vector<Game> games;
    bool hasMore = false;
};

class CatalogApiClient {
public:
    CatalogApiClient();
    ~CatalogApiClient();

    bool initialize();
    ApiResult loadCache(const std::string& genreSlug = "", int page = 1,
                        const std::string& query = "",
                        const std::string& ordering = "-metacritic",
                        const std::string& status = "", int minRating = 0,
                        const std::string& themeSlug = "",
                        const std::string& discoverySlug = "",
                        const std::string& gameModeSlug = "") const;
    ApiResult synchronize(const std::string& genreSlug = "", int page = 1,
                          const std::string& query = "",
                          const std::string& ordering = "-metacritic",
                          const std::string& status = "", int minRating = 0,
                          const std::string& themeSlug = "",
                          const std::string& discoverySlug = "",
                          const std::string& gameModeSlug = "") const;
    bool ensureCover(const Game& game, std::string& error) const;
    bool ensurePortraitCover(const Game& game, std::string& error) const;
    bool fetchDetails(const Game& game, Game& enriched, std::string& error) const;
    bool fetchSimilarGames(const Game& game, std::vector<Game>& similar,
                           std::string& error) const;
    bool ensureScreenshots(const Game& game, std::vector<std::string>& localPaths,
                           std::string& error) const;
    std::vector<Game> loadFavorites() const;
    bool saveFavorites(const std::vector<Game>& games) const;
    std::vector<Game> loadBacklog() const;
    bool saveBacklog(const std::vector<Game>& games) const;
    std::uint64_t cacheSizeBytes() const;
    bool clearCache(std::string& error) const;

    const std::string& basePath() const;

private:
    ApiResult parseCatalog(const std::string& payload, const std::string& successMessage) const;
    bool request(const std::string& url, std::string& payload, std::string& error,
                 std::size_t maxBytes) const;
    bool ensureImage(const std::string& url, const std::string& path,
                     std::string& error) const;
    std::string cachePath(const std::string& genreSlug, int page,
                          const std::string& query,
                          const std::string& ordering = "-metacritic",
                          const std::string& status = "", int minRating = 0,
                          const std::string& themeSlug = "",
                          const std::string& discoverySlug = "",
                          const std::string& gameModeSlug = "") const;
    bool initialized_ = false;
    std::string basePath_;
};

}  // namespace vitrine
