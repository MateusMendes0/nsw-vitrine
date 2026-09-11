#include "app.hpp"

#include <algorithm>

namespace vitrine {

App::App(bool networkReady)
    : favoriteCatalog_(std::vector<Game>{}),
      backlogCatalog_(std::vector<Game>{}),
      networkReady_(networkReady) {
    surpriseSeed_ ^= SDL_GetTicks();
    apiInitialized_ = api_.initialize();
    favoriteCatalog_.replace(api_.loadFavorites());
    for (const Game& game : favoriteCatalog_.all()) {
        favoriteIds_.insert(game.id);
    }
    backlogCatalog_.replace(api_.loadBacklog());
    for (const Game& game : backlogCatalog_.all()) {
        backlogStatuses_[game.id] = game.backlogStatus;
    }
    const ApiResult cached = api_.loadCache();
    if (cached.success) {
        catalog_.replace(cached.games);
        usingApi_ = true;
        currentPage_ = 1;
        hasMore_ = cached.hasMore;
        status_ = cached.message;
    } else {
        status_ = networkReady_ ? "Dados demonstrativos • pressione - para atualizar" :
                                  "Offline • usando dados demonstrativos";
    }
    rebuildGenres();
    refresh();
    startGridReveal();
    coverWorker_ = std::thread([this]() { coverWorkerLoop(); });
    queueVisibleCovers();
    startInitialSync();
}

App::~App() {
    {
        std::lock_guard<std::mutex> lock(coverMutex_);
        stopCoverWorker_ = true;
    }
    coverCondition_.notify_one();
    if (coverWorker_.joinable()) coverWorker_.join();
    if (screenshotThread_.joinable()) screenshotThread_.join();
    if (initialSyncThread_.joinable()) initialSyncThread_.join();
}

void App::releaseRendererResources() {
    detailsView_.releaseTransitionTexture();
}

void App::appendSearchText(const char* value) {
    filter_.query += value;
    refresh();
}

void App::eraseSearchCharacter() {
    if (filter_.query.empty()) return;
    filter_.query.pop_back();
    while (!filter_.query.empty() && (static_cast<unsigned char>(filter_.query.back()) & 0xC0) == 0x80) {
        filter_.query.pop_back();
    }
    refresh();
}

int App::gridColumns() const {
    return classicView_ ? kClassicColumns : kCoverColumns;
}

int App::gridRows() const {
    return classicView_ ? kClassicRows : kCoverRows;
}

int App::visibleGameCount() const {
    return gridColumns() * gridRows();
}

void App::startGridReveal() {
    gridRevealStart_ = SDL_GetTicks();
}

bool App::isFavorite(const std::string& id) const {
    return favoriteIds_.find(id) != favoriteIds_.end();
}

void App::switchMainTab(int direction) {
    int current = backlogTab_ ? 1 : (favoritesTab_ ? 2 : 0);
    current = (current + direction + 3) % 3;
    backlogTab_ = current == 1;
    favoritesTab_ = current == 2;
    discoveryFocus_ = false;
    selected_ = 0;
    previousSelected_ = -1;
    refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
    tabTransitionStart_ = SDL_GetTicks();
    status_ = backlogTab_ ? "Minha lista" :
              (favoritesTab_ ? "Aba Favoritos" :
               (discoveryIndex_ == 0 ? "Catalogo completo" : discoveryLabel(discoveryIndex_)));
}

void App::openAbout() {
    about_ = true;
    aboutOption_ = 0;
    aboutConfirmClear_ = false;
    aboutCacheBytes_ = api_.cacheSizeBytes();
    aboutMessage_.clear();
}

void App::handleAbout(const Input& input) {
    if (aboutConfirmClear_) {
        if (input.back) {
            aboutConfirmClear_ = false;
            aboutMessage_ = "Limpeza cancelada";
            return;
        }
        if (!input.accept) return;
        if (initialSyncRunning_) {
            aboutConfirmClear_ = false;
            aboutMessage_ = "Aguarde a atualizacao inicial terminar";
            return;
        }
        std::string error;
        const bool cleared = api_.clearCache(error);
        aboutCacheBytes_ = api_.cacheSizeBytes();
        aboutConfirmClear_ = false;
        if (!cleared) {
            aboutMessage_ = error.empty() ? "Falha ao limpar o cache" : error;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(coverMutex_);
            coverQueue_.clear();
            queuedCoverIds_.clear();
            processedCoverIds_.clear();
        }
        aboutMessage_ = "Cache limpo; Favoritos e Minha lista foram preservados";
        status_ = "Cache local limpo";
        return;
    }

    if (input.back) {
        about_ = false;
        return;
    }
    if (input.left && aboutOption_ > 0) --aboutOption_;
    if (input.right && aboutOption_ < 2) ++aboutOption_;
    if (!input.accept) return;
    if (aboutOption_ == 0) {
        about_ = false;
        synchronizeCatalog();
    } else if (aboutOption_ == 1) {
        aboutConfirmClear_ = true;
        aboutMessage_.clear();
    } else {
        about_ = false;
    }
}

BacklogStatus App::backlogStatus(const std::string& id) const {
    const auto found = backlogStatuses_.find(id);
    return found == backlogStatuses_.end() ? BacklogStatus::None : found->second;
}

void App::setBacklogStatus(const Game& source, BacklogStatus newStatus) {
    const Game sourceCopy = source;
    std::vector<Game> entries = backlogCatalog_.all();
    const auto found = std::find_if(entries.begin(), entries.end(), [&sourceCopy](const Game& item) {
        return item.id == sourceCopy.id;
    });
    if (newStatus == BacklogStatus::None) {
        if (found != entries.end()) entries.erase(found);
        backlogStatuses_.erase(sourceCopy.id);
    } else {
        Game updated = sourceCopy;
        updated.backlogStatus = newStatus;
        if (found == entries.end()) entries.push_back(std::move(updated));
        else *found = std::move(updated);
        backlogStatuses_[sourceCopy.id] = newStatus;
    }
    backlogCatalog_.replace(std::move(entries));
    if (!api_.saveBacklog(backlogCatalog_.all())) {
        status_ = "Nao foi possivel salvar a Minha lista";
    } else {
        status_ = newStatus == BacklogStatus::None
            ? sourceCopy.title + " removido da Minha lista"
            : sourceCopy.title + " • " + backlogStatusLabel(newStatus);
    }
    if (backlogTab_) refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
}

void App::openBacklogPanel(const Game& game, bool fromDetails) {
    backlogPanelGame_ = game;
    backlogPanelFromDetails_ = fromDetails;
    backlogOption_ = static_cast<int>(backlogStatus(game.id));
    backlogPanel_ = true;
}

void App::handleBacklogPanel(const Input& input) {
    if (input.back || input.backlog) {
        backlogPanel_ = false;
        return;
    }
    if (input.up && backlogOption_ > 0) --backlogOption_;
    if (input.down && backlogOption_ < 4) ++backlogOption_;
    if (input.left && backlogOption_ > 0) --backlogOption_;
    if (input.right && backlogOption_ < 4) ++backlogOption_;
    if (!input.accept) return;
    setBacklogStatus(backlogPanelGame_, static_cast<BacklogStatus>(backlogOption_));
    if (backlogPanelFromDetails_) detailGame_.backlogStatus = static_cast<BacklogStatus>(backlogOption_);
    backlogPanel_ = false;
}

void App::openSelectedDetails() {
    if (games_.empty()) return;
    std::string coverError;
    const bool hasImage = classicView_ ? !games_[selected_]->imageUrl.empty() :
                                         !games_[selected_]->coverImageUrl.empty();
    const bool imageReady = classicView_ ? api_.ensureCover(*games_[selected_], coverError) :
                                           api_.ensurePortraitCover(*games_[selected_], coverError);
    if (hasImage && !imageReady) status_ = coverError;
    detailGameId_ = games_[selected_]->id;
    detailGame_ = *games_[selected_];
    detailGame_.backlogStatus = backlogStatus(detailGame_.id);
    detailScreenshots_.clear();
    screenshotIndex_ = 0;
    screenshotFullscreen_ = false;
    startScreenshotLoad(*games_[selected_]);
    detailTransitionStart_ = SDL_GetTicks();
    detailClosing_ = false;
    details_ = true;
}

void App::surpriseMe() {
    if (games_.empty()) {
        status_ = "Nenhum jogo disponivel para sortear";
        return;
    }
    surpriseSeed_ ^= surpriseSeed_ << 13;
    surpriseSeed_ ^= surpriseSeed_ >> 17;
    surpriseSeed_ ^= surpriseSeed_ << 5;
    int next = static_cast<int>(surpriseSeed_ % static_cast<std::uint32_t>(games_.size()));
    if (games_.size() > 1 && next == selected_) next = (next + 1) % static_cast<int>(games_.size());
    previousSelected_ = selected_;
    selected_ = next;
    selectionAnimationStart_ = SDL_GetTicks();
    queueVisibleCovers();
    status_ = "Surpresa: " + games_[selected_]->title;
    openSelectedDetails();
}

void App::toggleFavorite(const Game& game) {
    const std::string id = game.id;
    const std::string title = game.title;
    std::vector<Game> favorites = favoriteCatalog_.all();
    const auto found = std::find_if(favorites.begin(), favorites.end(), [&id](const Game& item) {
        return item.id == id;
    });
    const bool removing = found != favorites.end();
    if (removing) {
        favorites.erase(found);
        favoriteIds_.erase(id);
    } else {
        favorites.push_back(game);
        favoriteIds_.insert(id);
    }
    favoriteCatalog_.replace(std::move(favorites));
    if (!api_.saveFavorites(favoriteCatalog_.all())) {
        status_ = "Nao foi possivel salvar os favoritos";
    } else {
        status_ = removing ? title + " removido dos favoritos" : title + " adicionado aos favoritos";
    }
    if (favoritesTab_) refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
}

int App::currentFilterOption() const {
    if (filterSection_ == 0) return genreIndex_;
    if (filterSection_ == 1) return highlightIndex_;
    if (filterSection_ == 3) return backlogFilterIndex_;
    return static_cast<int>(filter_.sort);
}

int App::filterOptionCount() const {
    if (filterSection_ == 0) return static_cast<int>(genres_.size());
    if (filterSection_ == 1) return 3;
    if (filterSection_ == 3) return 5;
    return 5;
}

int App::filterColumns() const {
    if (filterSection_ == 0) return 4;
    if (filterSection_ == 1) return 3;
    if (filterSection_ == 3) return 5;
    return 3;
}

void App::openFilterPanel(int section) {
    const int lastSection = backlogTab_ ? 3 : 2;
    filterSection_ = std::max(0, std::min(section, lastSection));
    filterOption_ = currentFilterOption();
    filterPanel_ = true;
}

void App::switchFilterSection(int direction) {
    const int sectionCount = backlogTab_ ? 4 : 3;
    filterSection_ = (filterSection_ + direction + sectionCount) % sectionCount;
    filterOption_ = currentFilterOption();
}

void App::handleFilterPanel(const Input& input) {
    if (input.back) {
        filterPanel_ = false;
        return;
    }
    if (input.previousGenre) switchFilterSection(-1);
    if (input.nextGenre) switchFilterSection(1);

    const int count = filterOptionCount();
    const int columns = filterColumns();
    if (input.left && filterOption_ % columns > 0) --filterOption_;
    if (input.right && filterOption_ % columns < columns - 1 && filterOption_ + 1 < count) ++filterOption_;
    if (input.up && filterOption_ >= columns) filterOption_ -= columns;
    if (input.down && filterOption_ + columns < count) filterOption_ += columns;

    if (!input.accept) return;
    filterPanel_ = false;
    if (filterSection_ == 0) {
        if (genreIndex_ != filterOption_) {
            genreIndex_ = filterOption_;
            if (favoritesTab_ || backlogTab_) refresh(); else loadCurrentFiltersFirstPage();
        }
    } else if (filterSection_ == 1) {
        if (highlightIndex_ != filterOption_) {
            highlightIndex_ = filterOption_;
            filter_.acclaimedOnly = highlightIndex_ == 1;
            filter_.upcomingOnly = highlightIndex_ == 2;
            if (favoritesTab_ || backlogTab_) refresh(); else loadCurrentFiltersFirstPage();
        }
    } else if (filterSection_ == 2) {
        if (static_cast<int>(filter_.sort) != filterOption_) {
            filter_.sort = static_cast<SortMode>(filterOption_);
            updateDiscoverySourceOrdering();
            if (favoritesTab_ || backlogTab_) refresh(); else loadCurrentFiltersFirstPage();
        }
    } else {
        backlogFilterIndex_ = filterOption_;
        refresh();
    }
    visibleCoverSignature_.clear();
    queueVisibleCovers();
}

void App::queueVisibleCovers() {
    if (games_.empty()) return;
    const int columns = gridColumns();
    const int rows = gridRows();
    const int visibleCount = visibleGameCount();
    const int selectedRow = selected_ / columns;
    const int firstRow = std::max(0, selectedRow - (rows - 1));
    const int firstIndex = firstRow * columns;
    std::vector<Game> visible;
    std::string signature = classicView_ ? "classic;" : "covers;";
    if (!classicView_ && selected_ >= 0 && selected_ < static_cast<int>(games_.size())) {
        signature += "selected:" + games_[selected_]->id + ";";
    }
    visible.reserve(visibleCount);
    for (int slot = 0; slot < visibleCount; ++slot) {
        const int index = firstIndex + slot;
        if (index >= static_cast<int>(games_.size())) break;
        const Game& game = *games_[index];
        signature += game.id + ";";
        const std::string& imageUrl = classicView_ ? game.imageUrl : game.coverImageUrl;
        if (!imageUrl.empty()) visible.push_back(game);
    }
    if (signature == visibleCoverSignature_) return;
    visibleCoverSignature_ = signature;

    {
        std::lock_guard<std::mutex> lock(coverMutex_);
        coverQueue_.clear();
        queuedCoverIds_.clear();
        if (selected_ >= firstIndex && selected_ < firstIndex + visibleCount) {
            const Game& selectedGame = *games_[selected_];
            const bool portrait = !classicView_;
            const std::string& imageUrl = portrait ? selectedGame.coverImageUrl : selectedGame.imageUrl;
            const std::string requestId = std::string(portrait ? "poster:" : "backdrop:") + selectedGame.id;
            if (!imageUrl.empty() &&
                processedCoverIds_.find(requestId) == processedCoverIds_.end() &&
                requestId != inFlightCoverId_) {
                coverQueue_.push_back({selectedGame, portrait});
                queuedCoverIds_.insert(requestId);
            }
            if (portrait && !selectedGame.imageUrl.empty()) {
                const std::string backdropId = "backdrop:" + selectedGame.id;
                if (processedCoverIds_.find(backdropId) == processedCoverIds_.end() &&
                    queuedCoverIds_.find(backdropId) == queuedCoverIds_.end() &&
                    backdropId != inFlightCoverId_) {
                    coverQueue_.push_back({selectedGame, false});
                    queuedCoverIds_.insert(backdropId);
                }
            }
        }
        for (const Game& game : visible) {
            const bool portrait = !classicView_;
            const std::string requestId = std::string(portrait ? "poster:" : "backdrop:") + game.id;
            if (processedCoverIds_.find(requestId) != processedCoverIds_.end() ||
                queuedCoverIds_.find(requestId) != queuedCoverIds_.end() ||
                requestId == inFlightCoverId_) {
                continue;
            }
            coverQueue_.push_back({game, portrait});
            queuedCoverIds_.insert(requestId);
        }
    }
    coverCondition_.notify_one();
}

void App::coverWorkerLoop() {
    while (true) {
        CoverRequest request;
        std::string requestId;
        {
            std::unique_lock<std::mutex> lock(coverMutex_);
            coverCondition_.wait(lock, [this]() { return stopCoverWorker_ || !coverQueue_.empty(); });
            if (stopCoverWorker_) return;
            request = coverQueue_.front();
            coverQueue_.pop_front();
            requestId = std::string(request.portrait ? "poster:" : "backdrop:") + request.game.id;
            inFlightCoverId_ = requestId;
        }

        std::string ignoredError;
        const bool loaded = request.portrait ? api_.ensurePortraitCover(request.game, ignoredError) :
                                               api_.ensureCover(request.game, ignoredError);
        {
            std::lock_guard<std::mutex> lock(coverMutex_);
            queuedCoverIds_.erase(requestId);
            if (loaded) processedCoverIds_.insert(requestId);
            if (inFlightCoverId_ == requestId) inFlightCoverId_.clear();
        }
    }
}

void App::rebuildGenres() {
    const std::string selectedGenre = genreIndex_ < static_cast<int>(genres_.size()) ? genres_[genreIndex_] : "Todos";
    if (usingApi_) {
        genres_ = {"Todos", "Acao", "Aventura", "Arcade", "Cartas", "Casual", "Corrida",
                   "Esporte", "Estrategia", "Familia", "Hack and Slash", "Indie", "Luta",
                   "Musica", "Pinball", "Plataforma", "Puzzle", "RPG", "Simulacao",
                   "Tatica", "Tiro", "Visual Novel"};
    } else {
        genres_ = catalog_.genres();
        genres_.insert(genres_.begin(), "Todos");
    }
    const auto found = std::find(genres_.begin(), genres_.end(), selectedGenre);
    genreIndex_ = found == genres_.end() ? 0 : static_cast<int>(found - genres_.begin());
}

std::string App::activeGenreSlug() const {
    if (genreIndex_ <= 0 || genreIndex_ >= static_cast<int>(genres_.size())) return {};
    const std::string& genre = genres_[genreIndex_];
    if (genre == "Acao") return "action";
    if (genre == "Aventura") return "adventure";
    if (genre == "Arcade") return "arcade";
    if (genre == "Cartas") return "card-and-board-game";
    if (genre == "Casual") return "casual";
    if (genre == "Corrida") return "racing";
    if (genre == "Esporte") return "sports";
    if (genre == "Estrategia") return "strategy";
    if (genre == "Familia") return "family";
    if (genre == "Hack and Slash") return "hack-and-slash-beat-em-up";
    if (genre == "Indie") return "indie";
    if (genre == "Luta") return "fighting";
    if (genre == "Musica") return "music";
    if (genre == "Pinball") return "pinball";
    if (genre == "Plataforma") return "platformer";
    if (genre == "Puzzle") return "puzzle";
    if (genre == "RPG") return "role-playing-games-rpg";
    if (genre == "Simulacao") return "simulation";
    if (genre == "Tatica") return "tactical";
    if (genre == "Tiro") return "shooter";
    if (genre == "Visual Novel") return "visual-novel";
    return {};
}

std::string App::activeOrderingSlug() const {
    switch (filter_.sort) {
        case SortMode::Score: return "-metacritic";
        case SortMode::Popular: return "-popular";
        case SortMode::Title: return "name";
        case SortMode::Shortest: return "-metacritic";
        case SortMode::Release: return "-released";
    }
    return "-metacritic";
}

const char* App::discoveryLabel(int index) const {
    return LayoutView::discoveryLabel(index);
}

std::string App::activeDiscoverySlug() const {
    static const char* slugs[] = {
        "", "popular", "releases", "top-rated", "indies", "hidden-gems"
    };
    return slugs[std::max(0, std::min(discoveryIndex_, 5))];
}

void App::updateDiscoverySourceOrdering() {
    filter_.preserveSourceOrder =
        ((discoveryIndex_ == 1 || discoveryIndex_ == 4) && filter_.sort == SortMode::Popular) ||
        (discoveryIndex_ == 2 && filter_.sort == SortMode::Release);
}

void App::captureDiscoveryReturnPoint() {
    discoveryReturnGames_ = catalog_.all();
    discoveryReturnFilter_ = filter_;
    discoveryReturnGenreIndex_ = genreIndex_;
    discoveryReturnHighlightIndex_ = highlightIndex_;
    discoveryReturnSelected_ = selected_;
    discoveryReturnPage_ = currentPage_;
    discoveryReturnHasMore_ = hasMore_;
    hasDiscoveryReturnPoint_ = true;
}

void App::restoreDiscoveryReturnPoint() {
    discoveryIndex_ = 0;
    discoveryCursor_ = 0;
    discoveryFocus_ = false;
    if (hasDiscoveryReturnPoint_) {
        catalog_.replace(std::move(discoveryReturnGames_));
        filter_ = discoveryReturnFilter_;
        genreIndex_ = discoveryReturnGenreIndex_;
        highlightIndex_ = discoveryReturnHighlightIndex_;
        selected_ = discoveryReturnSelected_;
        currentPage_ = discoveryReturnPage_;
        hasMore_ = discoveryReturnHasMore_;
        hasDiscoveryReturnPoint_ = false;
        refresh();
        status_ = "Catalogo completo restaurado";
    } else {
        loadCurrentFiltersFirstPage();
    }
    visibleCoverSignature_.clear();
    queueVisibleCovers();
    tabTransitionStart_ = SDL_GetTicks();
}

void App::applyDiscoverySection(int nextIndex) {
    nextIndex = std::max(0, std::min(nextIndex, 5));
    if (nextIndex == 0) {
        restoreDiscoveryReturnPoint();
        return;
    }
    if (nextIndex == discoveryIndex_) {
        discoveryFocus_ = false;
        return;
    }

    const int previousIndex = discoveryIndex_;
    const SortMode previousSort = filter_.sort;
    const bool previousSourceOrder = filter_.preserveSourceOrder;
    if (discoveryIndex_ == 0) captureDiscoveryReturnPoint();
    discoveryIndex_ = nextIndex;
    discoveryCursor_ = nextIndex;
    filter_.sort = nextIndex == 1 || nextIndex == 4
        ? SortMode::Popular
        : (nextIndex == 2 ? SortMode::Release : SortMode::Score);
    updateDiscoverySourceOrdering();
    status_ = "Carregando " + std::string(discoveryLabel(nextIndex)) + "...";
    const ApiResult result = fetchPage(activeGenreSlug(), 1, filter_.query);
    if (!result.success) {
        discoveryIndex_ = previousIndex;
        discoveryCursor_ = previousIndex;
        filter_.sort = previousSort;
        filter_.preserveSourceOrder = previousSourceOrder;
        if (previousIndex == 0) hasDiscoveryReturnPoint_ = false;
        status_ = result.message.empty() ? "Secao indisponivel sem rede ou cache" : result.message;
        return;
    }
    catalog_.replace(result.games);
    selected_ = 0;
    previousSelected_ = -1;
    currentPage_ = 1;
    hasMore_ = result.hasMore;
    discoveryFocus_ = false;
    refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
    tabTransitionStart_ = SDL_GetTicks();
    status_ = std::string(discoveryLabel(nextIndex)) + " • " +
              std::to_string(games_.size()) + (hasMore_ ? "+ jogos" : " jogos");
}

void App::handleDiscoveryRibbon(const Input& input) {
    if (input.left && discoveryCursor_ > 0) --discoveryCursor_;
    if (input.right && discoveryCursor_ < 5) ++discoveryCursor_;
    if (input.down) {
        discoveryFocus_ = false;
        status_ = discoveryIndex_ == 0 ? "Catalogo completo" : discoveryLabel(discoveryIndex_);
        return;
    }
    if (input.back) {
        if (discoveryIndex_ != 0) applyDiscoverySection(0);
        else discoveryFocus_ = false;
        return;
    }
    if (input.accept) applyDiscoverySection(discoveryCursor_);
}

std::string App::activeStatusParam() const {
    if (highlightIndex_ == 2) return "upcoming";
    return "";
}

int App::activeMinRating() const {
    if (highlightIndex_ == 1) return 80;
    return 0;
}

const char* App::highlightFilterLabel() const {
    static const char* labels[] = {"Todos", "Aclamados (80+)", "Lancamentos"};
    return labels[highlightIndex_];
}

void App::startInitialSync() {
    if (!networkReady_ || !apiInitialized_ || initialSyncRunning_) return;
    initialSyncRunning_ = true;
    initialSyncDone_.store(false, std::memory_order_release);
    status_ = usingApi_ ? "Catalogo salvo • atualizando..." : "Atualizando catalogo automaticamente...";
    initialSyncThread_ = std::thread([this]() {
        pendingInitialSync_ = api_.synchronize("", 1, "", "-metacritic", "", 0);
        initialSyncDone_.store(true, std::memory_order_release);
    });
}

void App::finishInitialSync() {
    if (!initialSyncRunning_ || !initialSyncDone_.load(std::memory_order_acquire)) return;
    if (initialSyncThread_.joinable()) initialSyncThread_.join();
    initialSyncRunning_ = false;

    const bool initialViewStillActive = !favoritesTab_ && !backlogTab_ && currentPage_ == 1 &&
        filter_.query.empty() && genreIndex_ == 0 && highlightIndex_ == 0 && discoveryIndex_ == 0 &&
        filter_.sort == SortMode::Score;
    if (!pendingInitialSync_.success || !initialViewStillActive) {
        if (!pendingInitialSync_.success && !usingApi_) {
            status_ = pendingInitialSync_.message.empty()
                ? "Offline • usando dados demonstrativos"
                : pendingInitialSync_.message + " • usando dados demonstrativos";
        }
        return;
    }

    catalog_.replace(std::move(pendingInitialSync_.games));
    usingApi_ = true;
    currentPage_ = 1;
    hasMore_ = pendingInitialSync_.hasMore;
    rebuildGenres();
    refresh();
    startGridReveal();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
    status_ = "Catalogo atualizado automaticamente";
}

void App::synchronizeCatalog() {
    if (initialSyncRunning_) {
        status_ = "Atualizacao automatica em andamento";
        return;
    }
    if (!networkReady_ || !apiInitialized_) {
        status_ = "Rede indisponivel; o cache continua ativo";
        return;
    }
    status_ = "Atualizando catalogo...";
    const ApiResult result = api_.synchronize(activeGenreSlug(), 1, filter_.query,
                                             activeOrderingSlug(), activeStatusParam(),
                                             activeMinRating(), "", activeDiscoverySlug());
    status_ = result.message;
    if (!result.success) return;
    catalog_.replace(result.games);
    usingApi_ = true;
    selected_ = 0;
    currentPage_ = 1;
    hasMore_ = result.hasMore;
    rebuildGenres();
    refresh();
}

ApiResult App::fetchPage(const std::string& genreSlug, int page, const std::string& query) {
    ApiResult result;
    const std::string ordering = activeOrderingSlug();
    const std::string status = activeStatusParam();
    const int minRating = activeMinRating();
    if (networkReady_ && apiInitialized_) {
        result = api_.synchronize(genreSlug, page, query, ordering, status, minRating,
                                  "", activeDiscoverySlug());
    }
    if (!result.success) {
        const ApiResult cached = api_.loadCache(genreSlug, page, query, ordering, status, minRating,
                                                 "", activeDiscoverySlug());
        if (cached.success) return cached;
    }
    return result;
}

void App::loadCurrentFiltersFirstPage() {
    if (!usingApi_) {
        refresh();
        return;
    }
    const std::string slug = activeGenreSlug();
    const ApiResult result = fetchPage(slug, 1, filter_.query);
    if (!result.success) {
        status_ = result.message.empty() ? "Filtro indisponivel sem rede ou cache" : result.message;
        refresh();
        return;
    }
    catalog_.replace(result.games);
    selected_ = 0;
    currentPage_ = 1;
    hasMore_ = result.hasMore;
    status_ = "Catalogo atualizado • pagina 1";
    refresh();
}

void App::restoreSimilarSourceDetails() {
    if (similarReturnStack_.empty()) return;
    SimilarReturnPoint returnPoint = std::move(similarReturnStack_.back());
    similarReturnStack_.pop_back();

    if (detailGameId_ == returnPoint.detailGameId && detailGame_.id == returnPoint.detailGame.id) {
        returnPoint.detailGame = detailGame_;
        if (!detailScreenshots_.empty()) {
            returnPoint.detailScreenshots = detailScreenshots_;
            returnPoint.screenshotIndex = screenshotIndex_;
        }
    }
    for (Game& item : returnPoint.catalogGames) {
        if (item.id == returnPoint.detailGame.id) {
            item = returnPoint.detailGame;
            break;
        }
    }

    catalog_.replace(std::move(returnPoint.catalogGames));
    filter_ = returnPoint.filter;
    genreIndex_ = returnPoint.genreIndex;
    highlightIndex_ = returnPoint.highlightIndex;
    backlogFilterIndex_ = returnPoint.backlogFilterIndex;
    discoveryIndex_ = returnPoint.discoveryIndex;
    discoveryCursor_ = returnPoint.discoveryCursor;
    discoveryFocus_ = returnPoint.discoveryFocus;
    favoritesTab_ = returnPoint.favoritesTab;
    backlogTab_ = returnPoint.backlogTab;
    classicView_ = returnPoint.classicView;
    usingApi_ = returnPoint.usingApi;
    hasMore_ = returnPoint.hasMore;
    currentPage_ = returnPoint.currentPage;
    selected_ = returnPoint.selected;
    previousSelected_ = returnPoint.previousSelected;
    detailGame_ = std::move(returnPoint.detailGame);
    detailGameId_ = std::move(returnPoint.detailGameId);
    detailScreenshots_ = std::move(returnPoint.detailScreenshots);
    screenshotIndex_ = detailScreenshots_.empty()
        ? 0
        : std::max(0, std::min(returnPoint.screenshotIndex,
                               static_cast<int>(detailScreenshots_.size()) - 1));
    screenshotFullscreen_ = false;
    detailClosing_ = false;
    details_ = true;
    detailTransitionStart_ = SDL_GetTicks();
    refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
    if (detailScreenshots_.empty()) startScreenshotLoad(detailGame_);
    status_ = "Voltando para " + detailGame_.title;
}

void App::loadSimilarGames(const Game& game) {
    if (game.id.rfind("igdb-", 0) != 0) return;
    std::vector<Game> similar;
    std::string error;
    status_ = "Buscando jogos semelhantes...";
    if (!api_.fetchSimilarGames(game, similar, error) || similar.empty()) {
        status_ = error.empty() ? "Nenhum jogo semelhante encontrado" : error;
        return;
    }

    SimilarReturnPoint returnPoint;
    returnPoint.catalogGames = catalog_.all();
    returnPoint.filter = filter_;
    returnPoint.detailGame = detailGame_;
    returnPoint.detailScreenshots = detailScreenshots_;
    returnPoint.detailGameId = detailGameId_;
    returnPoint.genreIndex = genreIndex_;
    returnPoint.highlightIndex = highlightIndex_;
    returnPoint.backlogFilterIndex = backlogFilterIndex_;
    returnPoint.discoveryIndex = discoveryIndex_;
    returnPoint.discoveryCursor = discoveryCursor_;
    returnPoint.selected = selected_;
    returnPoint.previousSelected = previousSelected_;
    returnPoint.currentPage = currentPage_;
    returnPoint.screenshotIndex = screenshotIndex_;
    returnPoint.discoveryFocus = discoveryFocus_;
    returnPoint.favoritesTab = favoritesTab_;
    returnPoint.backlogTab = backlogTab_;
    returnPoint.classicView = classicView_;
    returnPoint.usingApi = usingApi_;
    returnPoint.hasMore = hasMore_;
    similarReturnStack_.push_back(std::move(returnPoint));

    catalog_.replace(std::move(similar));
    usingApi_ = true;
    details_ = false;
    detailClosing_ = false;
    selected_ = 0;
    previousSelected_ = -1;
    currentPage_ = 1;
    hasMore_ = false;
    favoritesTab_ = false;
    backlogTab_ = false;
    discoveryIndex_ = 0;
    discoveryCursor_ = 0;
    discoveryFocus_ = false;
    genreIndex_ = 0;
    highlightIndex_ = 0;
    backlogFilterIndex_ = 0;
    filter_ = CatalogFilter{};
    filter_.preserveSourceOrder = true;
    status_ = "Semelhantes a " + game.title;
    refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
}

void App::loadNextPage() {
    if (!usingApi_ || !hasMore_) return;
    const int nextPage = currentPage_ + 1;
    const ApiResult result = fetchPage(activeGenreSlug(), nextPage, filter_.query);
    if (!result.success) {
        status_ = result.message.empty() ? "Nao foi possivel carregar a proxima pagina" : result.message;
        return;
    }

    std::vector<Game> combined = catalog_.all();
    for (const Game& incoming : result.games) {
        const bool duplicate = std::any_of(combined.begin(), combined.end(), [&incoming](const Game& existing) {
            return existing.id == incoming.id;
        });
        if (!duplicate) combined.push_back(incoming);
    }
    catalog_.replace(std::move(combined));
    currentPage_ = nextPage;
    hasMore_ = result.hasMore;
    status_ = "Pagina " + std::to_string(currentPage_) + " carregada • " +
              std::to_string(catalog_.all().size()) + " jogos";
    refresh();
}

void App::loadSearchFirstPage() {
    if (favoritesTab_ || backlogTab_) {
        refresh();
        status_ = backlogTab_ ? "Busca na Minha lista" :
                  (filter_.query.empty() ? "Aba Favoritos" : "Busca nos favoritos");
        return;
    }

    status_ = filter_.query.empty() ? "Carregando catalogo..." : "Pesquisando na IGDB...";
    const ApiResult result = fetchPage(activeGenreSlug(), 1, filter_.query);
    if (!result.success) {
        status_ = result.message.empty() ? "Busca indisponivel sem rede ou cache" : result.message;
        refresh();
        return;
    }

    catalog_.replace(result.games);
    usingApi_ = true;
    selected_ = 0;
    currentPage_ = 1;
    hasMore_ = result.hasMore;
    status_ = filter_.query.empty()
                  ? "Catalogo completo carregado"
                  : "Busca por " + filter_.query + " • " +
                        std::to_string(result.games.size()) +
                        (result.hasMore ? "+ jogos" : " jogos");
    refresh();
}

void App::clearSearchAndReturn() {
    filter_.query.clear();
    selected_ = 0;
    previousSelected_ = -1;
    if (favoritesTab_ || backlogTab_) {
        refresh();
        status_ = backlogTab_ ? "Minha lista" : "Aba Favoritos";
    } else {
        loadCurrentFiltersFirstPage();
        if (!usingApi_) status_ = "Catalogo completo";
    }
    startGridReveal();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
}

void App::startScreenshotLoad(const Game& game) {
    if (game.id.rfind("igdb-", 0) != 0) return;
    if (screenshotLoading_) {
        screenshotQueued_ = true;
        return;
    }
    if (screenshotThread_.joinable()) screenshotThread_.join();
    screenshotLoading_ = true;
    screenshotQueued_ = false;
    screenshotDone_.store(false, std::memory_order_release);
    pendingScreenshotPaths_.clear();
    pendingScreenshotError_.clear();
    pendingDetailError_.clear();
    pendingDetailLoaded_ = false;
    pendingDetailGame_ = game;
    pendingScreenshotGameId_ = game.id;
    const Game gameCopy = game;
    screenshotThread_ = std::thread([this, gameCopy]() {
        pendingDetailLoaded_ = api_.fetchDetails(gameCopy, pendingDetailGame_, pendingDetailError_);
        api_.ensureScreenshots(gameCopy, pendingScreenshotPaths_, pendingScreenshotError_);
        screenshotDone_.store(true, std::memory_order_release);
    });
}

void App::finishScreenshotLoad() {
    if (!screenshotLoading_ || !screenshotDone_.load(std::memory_order_acquire)) return;
    if (screenshotThread_.joinable()) screenshotThread_.join();
    screenshotLoading_ = false;
    if (pendingScreenshotGameId_ == detailGameId_) {
        detailGame_ = pendingDetailGame_;
        detailScreenshots_ = pendingScreenshotPaths_;
        screenshotIndex_ = 0;
        if (pendingDetailLoaded_) {
            const auto enrich = [this](Game& item) {
                item.mainHours = detailGame_.mainHours;
                item.completionHours = detailGame_.completionHours;
                item.averagePlaytime = detailGame_.averagePlaytime;
                item.description = detailGame_.description;
                item.tagline = detailGame_.tagline;
                item.studio = detailGame_.studio;
                item.publisher = detailGame_.publisher;
                item.releaseDate = detailGame_.releaseDate;
                item.ageRating = detailGame_.ageRating;
                item.website = detailGame_.website;
                item.ratingsCount = detailGame_.ratingsCount;
                item.themes = detailGame_.themes;
                item.gameModes = detailGame_.gameModes;
                item.perspectives = detailGame_.perspectives;
                item.franchise = detailGame_.franchise;
                item.videosCount = detailGame_.videosCount;
            };
            std::vector<Game> all = catalog_.all();
            for (auto& item : all) {
                if (item.id == detailGame_.id) {
                    enrich(item);
                    break;
                }
            }
            catalog_.replace(std::move(all));

            std::vector<Game> favs = favoriteCatalog_.all();
            for (auto& item : favs) {
                if (item.id == detailGame_.id) {
                    enrich(item);
                    break;
                }
            }
            favoriteCatalog_.replace(std::move(favs));

            std::vector<Game> backlog = backlogCatalog_.all();
            for (auto& item : backlog) {
                if (item.id == detailGame_.id) {
                    enrich(item);
                    break;
                }
            }
            backlogCatalog_.replace(std::move(backlog));
            api_.saveBacklog(backlogCatalog_.all());
            refresh();
        }
        if (pendingDetailLoaded_ && !detailScreenshots_.empty()) {
            status_ = "Detalhes e " + std::to_string(detailScreenshots_.size()) +
                      " screenshots carregados";
        } else if (pendingDetailLoaded_) {
            status_ = "Detalhes completos carregados";
        } else if (!pendingDetailError_.empty()) {
            status_ = pendingDetailError_;
        } else if (!pendingScreenshotError_.empty()) {
            status_ = pendingScreenshotError_;
        }
    }
    pendingScreenshotPaths_.clear();
    pendingScreenshotError_.clear();
    pendingDetailError_.clear();
    if (screenshotQueued_ && details_) {
        screenshotQueued_ = false;
        startScreenshotLoad(detailGame_);
    }
}

void App::refresh() {
    filter_.genre = genreIndex_ == 0 ? "" : genres_[genreIndex_];
    const Catalog& source = backlogTab_ ? backlogCatalog_ :
                            (favoritesTab_ ? favoriteCatalog_ : catalog_);
    games_ = source.filtered(filter_);
    if (backlogTab_ && backlogFilterIndex_ > 0) {
        const auto expected = static_cast<BacklogStatus>(backlogFilterIndex_);
        games_.erase(std::remove_if(games_.begin(), games_.end(), [expected](const Game* game) {
            return game->backlogStatus != expected;
        }), games_.end());
    }
    selected_ = std::max(0, std::min(selected_, static_cast<int>(games_.size()) - 1));
}

const char* App::backlogFilterLabel() const {
    static const char* labels[] = {"Todos", "Quero jogar", "Jogando", "Finalizados", "Abandonados"};
    return labels[backlogFilterIndex_];
}

void App::resetFiltersForSearch() {
    genreIndex_ = 0;
    highlightIndex_ = 0;
    backlogFilterIndex_ = 0;
    filter_.genre.clear();
    filter_.acclaimedOnly = false;
    filter_.upcomingOnly = false;
    filter_.sort = SortMode::Score;
    filter_.preserveSourceOrder = false;
    discoveryIndex_ = 0;
    discoveryCursor_ = 0;
    discoveryFocus_ = false;
    hasDiscoveryReturnPoint_ = false;
    discoveryReturnGames_.clear();
}

void App::openSearch() {
#ifdef __SWITCH__
    SwkbdConfig keyboard;
    char output[96] = {};
    if (R_FAILED(swkbdCreate(&keyboard, 0))) return;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "Pesquisar na Vitrine");
    swkbdConfigSetGuideText(&keyboard, "Titulo, estudio ou genero");
    swkbdConfigSetOkButtonText(&keyboard, "Buscar");
    swkbdConfigSetStringLenMax(&keyboard, 80);
    if (!filter_.query.empty()) swkbdConfigSetInitialText(&keyboard, filter_.query.c_str());
    if (R_SUCCEEDED(swkbdShow(&keyboard, output, sizeof(output)))) {
        resetFiltersForSearch();
        filter_.query = output;
        loadSearchFirstPage();
    }
    swkbdClose(&keyboard);
#else
    desktopTyping_ = !desktopTyping_;
    if (desktopTyping_) {
        resetFiltersForSearch();
        refresh();
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
        loadSearchFirstPage();
    }
#endif
}

void App::handle(const Input& input) {
    finishInitialSync();
    finishScreenshotLoad();
    if (about_) {
        handleAbout(input);
        return;
    }
    if (backlogPanel_) {
        handleBacklogPanel(input);
        return;
    }
    if (detailClosing_) return;
    if (screenshotFullscreen_) {
        if (!detailScreenshots_.empty()) {
            if (input.left) {
                screenshotIndex_ = (screenshotIndex_ - 1 + static_cast<int>(detailScreenshots_.size())) %
                                   static_cast<int>(detailScreenshots_.size());
            }
            if (input.right) {
                screenshotIndex_ = (screenshotIndex_ + 1) % static_cast<int>(detailScreenshots_.size());
            }
        }
        if (input.back || input.accept) screenshotFullscreen_ = false;
        return;
    }
    if (filterPanel_) {
        handleFilterPanel(input);
        return;
    }
    if (details_) {
        if (!detailScreenshots_.empty()) {
            if (input.left) {
                screenshotIndex_ = (screenshotIndex_ - 1 + static_cast<int>(detailScreenshots_.size())) %
                                   static_cast<int>(detailScreenshots_.size());
            }
            if (input.right) {
                screenshotIndex_ = (screenshotIndex_ + 1) % static_cast<int>(detailScreenshots_.size());
            }
        }
        if (input.favorite) toggleFavorite(detailGame_);
        if (input.backlog) {
            openBacklogPanel(detailGame_, true);
            return;
        }
        if (input.search) {
            loadSimilarGames(detailGame_);
            return;
        }
        if (input.back) {
            details_ = false;
            detailClosing_ = true;
            detailTransitionStart_ = SDL_GetTicks();
        }
        if (input.accept && !detailScreenshots_.empty()) screenshotFullscreen_ = true;
        return;
    }
    if (input.sync) {
        openAbout();
        return;
    }
    if (discoveryFocus_) {
        handleDiscoveryRibbon(input);
        return;
    }
    if (input.sort) {
        openFilterPanel(0);
        return;
    }
    if (input.previousGenre) {
        switchMainTab(-1);
        return;
    }
    if (input.nextGenre) {
        switchMainTab(1);
        return;
    }
    if (input.viewMode) {
        classicView_ = !classicView_;
        visibleCoverSignature_.clear();
        queueVisibleCovers();
        return;
    }
    if (input.back) {
        if (!similarReturnStack_.empty()) {
            restoreSimilarSourceDetails();
            return;
        }
        if (!filter_.query.empty()) {
            clearSearchAndReturn();
            return;
        }
        if (!backlogTab_ && !favoritesTab_ && discoveryIndex_ != 0) {
            applyDiscoverySection(0);
            return;
        }
        if (backlogTab_ || favoritesTab_) {
            backlogTab_ = false;
            favoritesTab_ = false;
            selected_ = 0;
            discoveryFocus_ = false;
            refresh();
            visibleCoverSignature_.clear();
            queueVisibleCovers();
            tabTransitionStart_ = SDL_GetTicks();
            status_ = discoveryIndex_ == 0 ? "Catalogo completo" : discoveryLabel(discoveryIndex_);
        }
        return;
    }
    if (input.search) openSearch();
    if (input.surprise) {
        surpriseMe();
        return;
    }
    if (input.backlog && !games_.empty()) {
        openBacklogPanel(*games_[selected_], false);
        return;
    }
    if (input.favorite && !games_.empty()) {
        toggleFavorite(*games_[selected_]);
        return;
    }
    const int columns = gridColumns();
    if (input.up && similarReturnStack_.empty() && !favoritesTab_ && !backlogTab_ &&
        (games_.empty() || selected_ < columns)) {
        discoveryFocus_ = true;
        discoveryCursor_ = discoveryIndex_;
        status_ = "Escolha uma secao de descoberta";
        return;
    }
    if (input.down && !favoritesTab_ && !backlogTab_ && usingApi_ && hasMore_ &&
        (games_.empty() || selected_ + columns >= static_cast<int>(games_.size()))) {
        loadNextPage();
    }
    if (games_.empty()) return;

    int next = selected_;
    if (input.left && selected_ % columns > 0) --next;
    if (input.right && selected_ % columns < columns - 1 && selected_ + 1 < static_cast<int>(games_.size())) ++next;
    if (input.up && selected_ >= columns) next -= columns;
    if (input.down && selected_ + columns < static_cast<int>(games_.size())) next += columns;
    next = std::max(0, std::min(next, static_cast<int>(games_.size()) - 1));
    if (next != selected_) {
        previousSelected_ = selected_;
        selected_ = next;
        selectionAnimationStart_ = SDL_GetTicks();
    }
    queueVisibleCovers();
    if (input.accept) openSelectedDetails();
}

void App::render(SDL_Renderer* renderer, TextRenderer& text, ImageRenderer& images) {
    layoutView_.renderBackground(renderer);
    layoutView_.renderHeader(renderer, text, images, filter_.query);
    layoutView_.renderToolbar(renderer, text, backlogTab_, favoritesTab_,
                              games_.size(), hasMore_, usingApi_,
                              genres_[genreIndex_],
                              backlogTab_ ? backlogFilterLabel() : highlightFilterLabel(),
                              filter_.sort);
    layoutView_.renderDiscoveryRibbon(
        renderer, text, backlogTab_, favoritesTab_,
        !similarReturnStack_.empty(),
        similarReturnStack_.empty() ? "" : similarReturnStack_.back().detailGame.title,
        discoveryIndex_, discoveryFocus_, discoveryCursor_);
    gridView_.render(
        renderer, text, images, games_, selected_, previousSelected_,
        selectionAnimationStart_, gridRevealStart_, classicView_,
        backlogTab_, favoritesTab_,
        [this](const std::string& id) { return isFavorite(id); },
        [this](const std::string& id) { return backlogStatus(id); });
    layoutView_.renderFooter(
        renderer, text, !similarReturnStack_.empty(), filter_.query,
        discoveryFocus_, discoveryIndex_, backlogTab_, favoritesTab_, status_);

    if (details_) {
        detailsView_.renderDetailsWithTransition(
            renderer, text, images, detailGame_, detailScreenshots_,
            screenshotIndex_, screenshotLoading_, isFavorite(detailGame_.id),
            backlogStatus(detailGame_.id), detailTransitionStart_);
    } else if (detailClosing_) {
        detailClosing_ = detailsView_.renderDetailsClosing(
            renderer, text, images, detailGame_, detailScreenshots_,
            screenshotIndex_, screenshotLoading_, isFavorite(detailGame_.id),
            backlogStatus(detailGame_.id), detailTransitionStart_);
    }

    if (screenshotFullscreen_) {
        detailsView_.renderScreenshotFullscreen(renderer, text, images,
                                                detailScreenshots_, screenshotIndex_);
    }
    if (filterPanel_) {
        panelsView_.renderFilterPanel(renderer, text, filterSection_,
                                      filterOption_, currentFilterOption(),
                                      backlogTab_, genres_);
    }
    if (backlogPanel_) {
        panelsView_.renderBacklogPanel(renderer, text, backlogPanelGame_,
                                       backlogOption_, backlogStatus(backlogPanelGame_.id));
    }
    if (about_) {
        panelsView_.renderAbout(renderer, text, aboutOption_, networkReady_,
                                apiInitialized_, initialSyncRunning_, usingApi_,
                                aboutCacheBytes_, aboutMessage_, aboutConfirmClear_);
    }
    layoutView_.renderTabTransition(renderer, tabTransitionStart_, details_, detailClosing_);
}

}  // namespace vitrine
