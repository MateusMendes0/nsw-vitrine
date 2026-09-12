#pragma once

#include "api_client.hpp"
#include "catalog.hpp"
#include "details_view.hpp"
#include "grid_view.hpp"
#include "input.hpp"
#include "layout_view.hpp"
#include "panels_view.hpp"

#include <SDL2/SDL.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vitrine {

struct CoverRequest {
    Game game;
    bool portrait = true;
};

struct SimilarReturnPoint {
    std::vector<Game> catalogGames;
    CatalogFilter filter{};
    Game detailGame{};
    std::vector<std::string> detailScreenshots;
    std::string detailGameId;
    int genreIndex = 0;
    int highlightIndex = 0;
    int backlogFilterIndex = 0;
    int discoveryIndex = 0;
    int discoveryCursor = 0;
    int selected = 0;
    int previousSelected = -1;
    int currentPage = 1;
    int screenshotIndex = 0;
    bool discoveryFocus = false;
    bool favoritesTab = false;
    bool backlogTab = false;
    bool classicView = false;
    bool usingApi = false;
    bool hasMore = false;
};

class App {
public:
    explicit App(bool networkReady);
    ~App();

    void handle(const Input& input);
    void render(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images);
    void releaseRendererResources();
    void appendSearchText(const char* value);
    void eraseSearchCharacter();
    bool quitRequested() const { return quitRequested_; }

private:
    int gridColumns() const;
    int gridRows() const;
    int visibleGameCount() const;
    bool handleTouch(const Input& touch, Input& mappedInput);
    void updateTouchDrag(const Input& touch);
    int touchGridStride() const;
    int maximumTouchScroll() const;

    void startGridReveal();
    bool isFavorite(const std::string& id) const;
    void switchMainTab(int direction);
    void openAbout();
    void handleAbout(const Input& input);
    BacklogStatus backlogStatus(const std::string& id) const;
    void setBacklogStatus(const Game& source, BacklogStatus newStatus);
    void openBacklogPanel(const Game& game, bool fromDetails);
    void handleBacklogPanel(const Input& input);
    void openSelectedDetails();
    void surpriseMe();
    void toggleFavorite(const Game& game);
    int currentFilterOption() const;
    int filterOptionCount() const;
    int filterColumns() const;
    void openFilterPanel(int section);
    void switchFilterSection(int direction);
    void handleFilterPanel(const Input& input);
    void queueVisibleCovers();
    void coverWorkerLoop();
    void rebuildGenres();
    std::string activeGenreSlug() const;
    std::string activeOrderingSlug() const;
    const char* discoveryLabel(int index) const;
    std::string activeDiscoverySlug() const;
    void updateDiscoverySourceOrdering();
    void captureDiscoveryReturnPoint();
    void restoreDiscoveryReturnPoint();
    void applyDiscoverySection(int nextIndex);
    void handleDiscoveryRibbon(const Input& input);
    std::string activeStatusParam() const;
    int activeMinRating() const;
    const char* highlightFilterLabel() const;
    void startInitialSync();
    void finishInitialSync();
    void synchronizeCatalog();
    ApiResult fetchPage(const std::string& genreSlug, int page, const std::string& query);
    void loadCurrentFiltersFirstPage();
    void sortSearchPage(std::vector<Game>& games) const;
    void restoreSimilarSourceDetails();
    void loadSimilarGames(const Game& game);
    void loadNextPage();
    void finishNextPageLoad();
    void loadSearchFirstPage();
    void clearSearchAndReturn();
    void startScreenshotLoad(const Game& game);
    void finishScreenshotLoad();
    void refresh();
    const char* backlogFilterLabel() const;
    void resetFiltersForSearch();
    void openSearch();

    // Visual Views
    LayoutView layoutView_;
    GridView gridView_;
    DetailsView detailsView_;
    PanelsView panelsView_;

    Catalog catalog_;
    Catalog favoriteCatalog_;
    Catalog backlogCatalog_;
    CatalogFilter filter_;
    CatalogApiClient api_;
    std::vector<std::string> genres_;
    std::vector<const Game*> games_;
    int genreIndex_ = 0;
    int highlightIndex_ = 0;
    int backlogFilterIndex_ = 0;
    int discoveryIndex_ = 0;
    int discoveryCursor_ = 0;
    int selected_ = 0;
    int previousSelected_ = -1;
    Uint32 selectionAnimationStart_ = 0;
    Uint32 gridRevealStart_ = 0;
    Uint32 detailTransitionStart_ = 0;
    Uint32 tabTransitionStart_ = 0;
    std::uint32_t surpriseSeed_ = 0x9e3779b9u;
    int currentPage_ = 1;
    int filterSection_ = 0;
    int filterOption_ = 0;
    bool details_ = false;
    bool detailClosing_ = false;
    bool screenshotFullscreen_ = false;
    bool classicView_ = false;
    bool backlogTab_ = false;
    bool favoritesTab_ = false;
    bool filterPanel_ = false;
    bool backlogPanel_ = false;
    bool backlogPanelFromDetails_ = false;
    bool about_ = false;
    bool aboutConfirmClear_ = false;
    bool discoveryFocus_ = false;
    bool hasDiscoveryReturnPoint_ = false;
    int backlogOption_ = 0;
    int aboutOption_ = 0;
    bool desktopTyping_ = false;
    bool networkReady_ = false;
    bool apiInitialized_ = false;
    bool usingApi_ = false;
    bool hasMore_ = false;
    std::string status_;
    std::vector<SimilarReturnPoint> similarReturnStack_;
    std::vector<Game> discoveryReturnGames_;
    CatalogFilter discoveryReturnFilter_{};
    int discoveryReturnGenreIndex_ = 0;
    int discoveryReturnHighlightIndex_ = 0;
    int discoveryReturnSelected_ = 0;
    int discoveryReturnPage_ = 1;
    bool discoveryReturnHasMore_ = false;
    std::uint64_t aboutCacheBytes_ = 0;
    std::string aboutMessage_;
    std::vector<std::string> detailScreenshots_;
    std::vector<std::string> pendingScreenshotPaths_;
    Game detailGame_{};
    Game pendingDetailGame_{};
    Game backlogPanelGame_{};
    std::string detailGameId_;
    std::string pendingScreenshotGameId_;
    std::string pendingScreenshotError_;
    std::string pendingDetailError_;
    int screenshotIndex_ = 0;
    std::thread screenshotThread_;
    std::atomic<bool> screenshotDone_{false};
    std::thread initialSyncThread_;
    std::atomic<bool> initialSyncDone_{false};
    ApiResult pendingInitialSync_{};
    bool initialSyncRunning_ = false;
    std::thread nextPageThread_;
    std::atomic<bool> nextPageDone_{false};
    ApiResult pendingNextPage_{};
    bool nextPageLoading_ = false;
    int pendingNextPageNumber_ = 0;
    std::string pendingNextPageGenre_;
    std::string pendingNextPageQuery_;
    std::string pendingNextPageOrdering_;
    std::string pendingNextPageStatus_;
    std::string pendingNextPageDiscovery_;
    int pendingNextPageMinRating_ = 0;
    bool screenshotLoading_ = false;
    bool screenshotQueued_ = false;
    bool pendingDetailLoaded_ = false;
    std::thread coverWorker_;
    std::mutex coverMutex_;
    std::condition_variable coverCondition_;
    std::deque<CoverRequest> coverQueue_;
    std::unordered_set<std::string> queuedCoverIds_;
    std::unordered_set<std::string> processedCoverIds_;
    std::unordered_set<std::string> favoriteIds_;
    std::unordered_map<std::string, BacklogStatus> backlogStatuses_;
    std::string inFlightCoverId_;
    std::string visibleCoverSignature_;
    bool stopCoverWorker_ = false;
    bool quitRequested_ = false;
    bool touchMode_ = false;
    bool touchPreviewActive_ = false;
    bool touchDragTracking_ = false;
    bool touchDragMoved_ = false;
    int touchDragOriginY_ = 0;
    int touchDragStartScroll_ = 0;
    int touchScrollY_ = 0;
};

}  // namespace vitrine
