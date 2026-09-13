#include "app.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace vitrine {

namespace {

bool contains(int x, int y, int left, int top, int width, int height) {
    return x >= left && x < left + width && y >= top && y < top + height;
}

}  // namespace

App::App(bool networkReady, std::string executablePath)
    : favoriteCatalog_(std::vector<Game>{}),
      backlogCatalog_(std::vector<Game>{}),
      updater_(std::move(executablePath)),
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
    startUpdateCheck(false);
}

App::~App() {
    updateCancelRequested_.store(true);
    {
        std::lock_guard<std::mutex> lock(coverMutex_);
        stopCoverWorker_ = true;
    }
    coverCondition_.notify_one();
    if (coverWorker_.joinable()) coverWorker_.join();
    if (screenshotThread_.joinable()) screenshotThread_.join();
    if (initialSyncThread_.joinable()) initialSyncThread_.join();
    if (nextPageThread_.joinable()) nextPageThread_.join();
    if (updateCheckThread_.joinable()) updateCheckThread_.join();
    if (updateInstallThread_.joinable()) updateInstallThread_.join();
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

int App::touchGridStride() const {
    if (!classicView_) return 372;
    return (!backlogTab_ && !favoritesTab_) ? 236 : 244;
}

int App::maximumTouchScroll() const {
    if (games_.empty()) return 0;
    const int columns = gridColumns();
    const int rowCount = (static_cast<int>(games_.size()) + columns - 1) / columns;
    const int gridY = (!backlogTab_ && !favoritesTab_) ? 190 : 160;
    const int cardHeight = classicView_ ? 216 : 316;
    const int contentHeight = (rowCount - 1) * touchGridStride() + cardHeight;
    return std::max(0, contentHeight - (652 - gridY));
}

void App::updateTouchDrag(const Input& touch) {
    if (details_ || screenshotFullscreen_ || filterPanel_ || backlogPanel_ || about_ || detailClosing_) {
        touchDragTracking_ = false;
        touchDragMoved_ = false;
        return;
    }

    const int gridY = (!backlogTab_ && !favoritesTab_) ? 190 : 160;
    if (touch.touchBegan) {
        touchDragTracking_ = touch.touchY >= gridY && touch.touchY < 648;
        touchDragMoved_ = false;
        touchDragOriginY_ = touch.touchY;
        touchDragStartScroll_ = touchPreviewActive_
            ? (selected_ / gridColumns()) * touchGridStride()
            : touchScrollY_;
    }
    if (!touchDragTracking_) return;

    const int deltaX = touch.touchX - touch.touchStartX;
    const int deltaY = touch.touchY - touchDragOriginY_;
    if (!touchDragMoved_ && std::abs(deltaY) > 10 && std::abs(deltaY) > std::abs(deltaX)) {
        touchDragMoved_ = true;
        touchPreviewActive_ = false;
        visibleCoverSignature_.clear();
    }
    if (touchDragMoved_) {
        touchScrollY_ = std::max(0, std::min(maximumTouchScroll(), touchDragStartScroll_ - deltaY));
        queueVisibleCovers();
    }
}

bool App::handleTouch(const Input& touch, Input& mappedInput) {
    const int x = touch.touchX;
    const int y = touch.touchY;
    const TouchGestureDirection gesture = touchGestureDirection(touch);

    if (touchDragTracking_) {
        touchDragTracking_ = false;
        if (touchDragMoved_) {
            touchDragMoved_ = false;
            const bool reachedEnd = touchScrollY_ >= maximumTouchScroll() - 4;
            if (reachedEnd && !backlogTab_ && !favoritesTab_ && usingApi_ && hasMore_) {
                loadNextPage();
                visibleCoverSignature_.clear();
                queueVisibleCovers();
            }
            return true;
        }
    }

    if (about_) {
        Input action;
        if (aboutConfirmClear_) {
            if (contains(x, y, 330, 430, 300, 66)) action.accept = true;
            else action.back = true;
            handleAbout(action);
            return true;
        }
        if (contains(x, y, 174, 390, 932, 120)) {
            aboutOption_ = std::min(3, std::max(0, (x - 178) / 232));
            action.accept = true;
        } else if (!contains(x, y, 130, 48, 1020, 624) || contains(x, y, 960, 604, 160, 58)) {
            action.back = true;
        }
        handleAbout(action);
        return true;
    }

    if (backlogPanel_) {
        Input action;
        for (int option = 0; option < 5; ++option) {
            if (contains(x, y, 344, 246 + option * 62, 592, 64)) {
                backlogOption_ = option;
                action.accept = true;
                break;
            }
        }
        if (!action.accept && (!contains(x, y, 320, 112, 640, 496) ||
                               contains(x, y, 730, 548, 210, 54))) {
            action.back = true;
        }
        handleBacklogPanel(action);
        return true;
    }

    if (screenshotFullscreen_) {
        if (gesture == TouchGestureDirection::Left) mappedInput.right = true;
        else if (gesture == TouchGestureDirection::Right) mappedInput.left = true;
        else if (x < 300) mappedInput.left = true;
        else if (x >= kWidth - 300) mappedInput.right = true;
        else mappedInput.back = true;
        return false;
    }

    if (filterPanel_) {
        Input action;
        const int tabCount = backlogTab_ ? 5 : 4;
        const int tabWidth = backlogTab_ ? 194 : 247;
        for (int tab = 0; tab < tabCount; ++tab) {
            if (contains(x, y, 122 + tab * (tabWidth + 16), 158, tabWidth, 64)) {
                filterSection_ = tab;
                filterOption_ = currentFilterOption();
                return true;
            }
        }

        const int count = filterOptionCount();
        const int columns = filterColumns();
        const int gap = 16;
        const int optionWidth = (1036 - gap * (columns - 1)) / columns;
        const int optionHeight = filterSection_ == 0 ? 52 : 72;
        const int startY = filterSection_ == 0 ? 232 : 282;
        const int rowGap = filterSection_ == 0 ? 10 : 16;
        for (int option = 0; option < count; ++option) {
            const int optionX = 122 + (option % columns) * (optionWidth + gap);
            const int optionY = startY + (option / columns) * (optionHeight + rowGap);
            if (contains(x, y, optionX - 6, optionY - 6, optionWidth + 12, optionHeight + 12)) {
                filterOption_ = option;
                action.accept = true;
                handleFilterPanel(action);
                touchPreviewActive_ = false;
                touchScrollY_ = 0;
                return true;
            }
        }
        if (!contains(x, y, 82, 60, 1116, 600) || contains(x, y, 1060, 594, 130, 58)) {
            action.back = true;
            handleFilterPanel(action);
        }
        return true;
    }

    if (details_) {
        if (gesture == TouchGestureDirection::Left) {
            mappedInput.right = true;
            return false;
        }
        if (gesture == TouchGestureDirection::Right) {
            mappedInput.left = true;
            return false;
        }
        if (!detailScreenshots_.empty() && contains(x, y, 34, 326, 184, 64)) {
            mappedInput.accept = true;
            return false;
        }
        if (contains(x, y, 218, 326, 232, 64)) {
            mappedInput.backlog = true;
            return false;
        }
        if (contains(x, y, 450, 326, 224, 64)) {
            mappedInput.favorite = true;
            return false;
        }
        if (contains(x, y, 674, 326, 226, 64)) {
            mappedInput.search = true;
            return false;
        }
        const std::size_t thumbnailCount = std::min<std::size_t>(6, detailScreenshots_.size());
        for (std::size_t index = 0; index < thumbnailCount; ++index) {
            if (contains(x, y, 36 + static_cast<int>(index) * 200, 440, 200, 130)) {
                screenshotIndex_ = static_cast<int>(index);
                screenshotFullscreen_ = true;
                return true;
            }
        }
        if (contains(x, y, 20, 648, 150, 72)) {
            mappedInput.back = true;
            return false;
        }
        return true;
    }

    if (gesture != TouchGestureDirection::None) {
        mappedInput.left = gesture == TouchGestureDirection::Right;
        mappedInput.right = gesture == TouchGestureDirection::Left;
        mappedInput.up = gesture == TouchGestureDirection::Down;
        mappedInput.down = gesture == TouchGestureDirection::Up;
        return false;
    }

    if (contains(x, y, 692, 16, 408, 64)) {
        touchPreviewActive_ = false;
        touchScrollY_ = 0;
        mappedInput.search = true;
        return false;
    }
    if (contains(x, y, 28, 16, 310, 64)) {
        openAbout();
        return true;
    }
    if (contains(x, y, 1100, 16, 146, 64)) {
        openFilterPanel(0);
        return true;
    }

    static const int tabX[] = {42, 148, 286};
    static const int tabWidths[] = {98, 130, 112};
    for (int tab = 0; tab < 3; ++tab) {
        if (contains(x, y, tabX[tab], 96, tabWidths[tab], 56)) {
            const int current = backlogTab_ ? 1 : (favoritesTab_ ? 2 : 0);
            if (tab != current) switchMainTab(tab - current);
            return true;
        }
    }
    if (contains(x, y, 490, 96, 232, 56)) {
        openFilterPanel(0);
        return true;
    }
    if (contains(x, y, 718, 96, 232, 56)) {
        openFilterPanel(backlogTab_ ? 3 : 1);
        return true;
    }
    if (contains(x, y, 946, 96, 298, 56)) {
        openFilterPanel(2);
        return true;
    }

    if (!backlogTab_ && !favoritesTab_) {
        if (!similarReturnStack_.empty()) {
            if (contains(x, y, 1000, 145, 250, 48)) {
                restoreSimilarSourceDetails();
                return true;
            }
        } else {
            static const int widths[] = {76, 112, 132, 158, 82, 164, 110};
            int chipX = 150;
            for (int index = 0; index < 7; ++index) {
                if (contains(x, y, chipX - 4, 147, widths[index] + 8, 42)) {
                    discoveryCursor_ = index;
                    touchPreviewActive_ = false;
                    touchScrollY_ = 0;
                    applyDiscoverySection(index);
                    return true;
                }
                chipX += widths[index] + 8;
            }
        }
    }

    if (y >= 648) {
        if (x >= 1110) {
            quitRequested_ = true;
            return true;
        }
        if (!similarReturnStack_.empty()) {
            if (x < 205) restoreSimilarSourceDetails();
            else if (x < 335 && !games_.empty()) openSelectedDetails();
            else if (x >= 420 && x < 560) {
                classicView_ = !classicView_;
                visibleCoverSignature_.clear();
                queueVisibleCovers();
            } else if (x >= 540 && x < 720 && !games_.empty()) {
                openBacklogPanel(*games_[selected_], false);
            } else if (x >= 700 && x < 850 && !games_.empty()) {
                toggleFavorite(*games_[selected_]);
            }
            return true;
        }
        if (!filter_.query.empty()) {
            if (x < 190) clearSearchAndReturn();
            else if (x < 315 && !games_.empty()) openSelectedDetails();
            else if (x < 455) openSearch();
            else if (x < 575) openFilterPanel(0);
            else if (x < 700) {
                classicView_ = !classicView_;
                visibleCoverSignature_.clear();
                queueVisibleCovers();
            }
            return true;
        }
        if (x < 160 && !games_.empty()) {
            openSelectedDetails();
        } else if ((backlogTab_ || favoritesTab_) && x < 285) {
            const int current = backlogTab_ ? 1 : 2;
            switchMainTab(-current);
        } else if ((!backlogTab_ && !favoritesTab_ && x >= 260 && x < 385) ||
                   ((backlogTab_ || favoritesTab_) && x >= 380 && x < 510)) {
            openFilterPanel(0);
        } else if ((!backlogTab_ && !favoritesTab_ && x >= 380 && x < 495) ||
                   ((backlogTab_ || favoritesTab_) && x >= 500 && x < 620)) {
            openSearch();
        } else if (!backlogTab_ && !favoritesTab_ && x >= 490 && x < 635) {
            surpriseMe();
        } else if ((!backlogTab_ && !favoritesTab_ && x >= 625 && x < 745) ||
                   ((backlogTab_ || favoritesTab_) && x >= 610 && x < 735)) {
            classicView_ = !classicView_;
            visibleCoverSignature_.clear();
            queueVisibleCovers();
        } else if ((!backlogTab_ && !favoritesTab_ && x >= 735 && x < 850) ||
                   ((backlogTab_ || favoritesTab_) && x >= 715 && x < 835)) {
            openAbout();
        }
        return true;
    }

    if (games_.empty()) return true;

    if (!classicView_ && touchPreviewActive_ && contains(x, y, 240, 580, 840, 65)) {
        if (x < 380) openSelectedDetails();
        else if (x < 550) openBacklogPanel(*games_[selected_], false);
        else if (x < 790) toggleFavorite(*games_[selected_]);
        return true;
    }
    const int columns = gridColumns();
    const bool touchBrowse = touchMode_ && !touchPreviewActive_;
    const int rows = touchBrowse ? 3 : gridRows();
    const int selectedRow = selected_ / columns;
    const int firstRow = touchBrowse ? touchScrollY_ / touchGridStride() :
                         std::max(0, selectedRow - (rows - 1));
    const int firstIndex = firstRow * columns;
    const bool discoveryVisible = !backlogTab_ && !favoritesTab_;
    const int gridY = discoveryVisible ? 190 : 160;
    const int rowStride = discoveryVisible ? 236 : 244;
    const int scrollRemainder = touchBrowse ? touchScrollY_ % touchGridStride() : 0;
    for (int slot = 0; slot < columns * rows; ++slot) {
        const int index = firstIndex + slot;
        if (index >= static_cast<int>(games_.size())) break;
        const int column = slot % columns;
        const int row = slot / columns;
        const int cardX = 42 + column * (classicView_ ? 307 : 244);
        const int cardY = gridY - scrollRemainder + row * (classicView_ ? rowStride : 372);
        const int cardWidth = classicView_ ? 277 : 226;
        const int cardHeight = classicView_ ? 216 : 316;
        if (contains(x, y, cardX - 8, cardY - 8, cardWidth + 16, cardHeight + 16)) {
            const bool openDetails = touchPreviewActive_ && index == selected_;
            if (index != selected_) {
                previousSelected_ = selected_;
                selected_ = index;
                selectionAnimationStart_ = SDL_GetTicks();
                queueVisibleCovers();
            }
            if (openDetails) {
                openSelectedDetails();
            } else {
                touchPreviewActive_ = true;
                touchScrollY_ = (selected_ / columns) * touchGridStride();
                visibleCoverSignature_.clear();
                queueVisibleCovers();
            }
            return true;
        }
    }
    return true;
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
    touchPreviewActive_ = false;
    touchScrollY_ = 0;
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

std::string App::aboutUpdateSubtitle() const {
    if (!updater_.canInstall()) return "Indisponivel neste modo";
    if (updateCheckRunning_) return "Verificando nova versao";
    if (updateInfo_.available) return "v" + updateInfo_.version + " disponivel";
    return "Buscar nova versao";
}

void App::startUpdateCheck(bool manual) {
    if (updateCheckRunning_ || updateInstallRunning_) return;
    if (!networkReady_ || !apiInitialized_) {
        if (manual) aboutMessage_ = "Sem conexao para verificar atualizacoes";
        return;
    }
    if (!updater_.canInstall()) {
        if (manual) aboutMessage_ = "Atualizacao disponivel apenas no Nintendo Switch";
        return;
    }
    if (updateCheckThread_.joinable()) updateCheckThread_.join();
    updateCheckManual_ = manual;
    updateCheckRunning_ = true;
    updateCheckDone_.store(false);
    pendingUpdateInfo_ = {};
    pendingUpdateError_.clear();
    pendingUpdateCheckSuccess_ = false;
    if (manual) aboutMessage_ = "Verificando atualizacoes no GitHub...";
    updateCheckThread_ = std::thread([this]() {
        pendingUpdateCheckSuccess_ = updater_.check(pendingUpdateInfo_, pendingUpdateError_);
        updateCheckDone_.store(true);
    });
}

void App::finishUpdateCheck() {
    if (!updateCheckRunning_ || !updateCheckDone_.load()) return;
    if (updateCheckThread_.joinable()) updateCheckThread_.join();
    updateCheckRunning_ = false;
    const bool manual = updateCheckManual_;
    updateCheckManual_ = false;
    if (!pendingUpdateCheckSuccess_) {
        if (manual) aboutMessage_ = pendingUpdateError_.empty()
            ? "Nao foi possivel verificar atualizacoes" : pendingUpdateError_;
        return;
    }
    updateInfo_ = pendingUpdateInfo_;
    if (!updateInfo_.available) {
        if (manual) aboutMessage_ = "Voce ja esta usando a versao mais recente";
        return;
    }
    aboutMessage_ = "Nova versao disponivel: v" + updateInfo_.version;
    if (manual || !updateIgnored_) openUpdateDialog();
}

void App::openUpdateDialog() {
    if (!updateInfo_.available || updateInstallRunning_) return;
    updateDialogOption_ = 0;
    updateDialogMessage_.clear();
    updateDialogState_ = UpdateDialogState::Available;
}

void App::startUpdateInstall() {
    if (updateInstallRunning_ || !updateInfo_.available) return;
    if (updateInstallThread_.joinable()) updateInstallThread_.join();
    updateInstallRunning_ = true;
    updateInstallDone_.store(false);
    updateCancelRequested_.store(false);
    updateDownloadedBytes_.store(0);
    pendingUpdateInstallResult_ = {};
    updateDialogMessage_ = "Baixando e validando a nova versao...";
    updateDialogState_ = UpdateDialogState::Installing;
    updateInstallThread_ = std::thread([this]() {
        pendingUpdateInstallResult_ = updater_.install(
            updateInfo_, updateDownloadedBytes_, updateCancelRequested_);
        updateInstallDone_.store(true);
    });
}

void App::finishUpdateInstall() {
    if (!updateInstallRunning_ || !updateInstallDone_.load()) return;
    if (updateInstallThread_.joinable()) updateInstallThread_.join();
    updateInstallRunning_ = false;
    updateDialogMessage_ = pendingUpdateInstallResult_.message;
    if (pendingUpdateInstallResult_.success) {
        updateDialogState_ = UpdateDialogState::Installed;
        status_ = "Vitrine v" + updateInfo_.version + " instalada";
    } else if (pendingUpdateInstallResult_.cancelled) {
        updateDialogState_ = UpdateDialogState::Available;
        aboutMessage_ = pendingUpdateInstallResult_.message;
    } else {
        updateDialogState_ = UpdateDialogState::Error;
        aboutMessage_ = pendingUpdateInstallResult_.message;
    }
}

void App::handleUpdateDialog(const Input& input) {
    if (updateDialogState_ == UpdateDialogState::Available) {
        if (input.left) updateDialogOption_ = 0;
        if (input.right) updateDialogOption_ = 1;
        if (input.back) updateDialogOption_ = 1;
        if (!input.accept && !input.back) return;
        if (updateDialogOption_ == 0 && input.accept) {
            startUpdateInstall();
        } else {
            updateIgnored_ = true;
            updateDialogState_ = UpdateDialogState::Hidden;
            aboutMessage_ = "Atualizacao ignorada por enquanto";
        }
        return;
    }
    if (updateDialogState_ == UpdateDialogState::Installing) {
        if (input.back) {
            updateCancelRequested_.store(true);
            updateDialogMessage_ = "Cancelando; a versao atual sera preservada...";
        }
        return;
    }
    if (updateDialogState_ == UpdateDialogState::Installed) {
        if (input.accept || input.back) quitRequested_ = true;
        return;
    }
    if (updateDialogState_ == UpdateDialogState::Error && (input.accept || input.back)) {
        updateDialogState_ = UpdateDialogState::Hidden;
    }
}

void App::handleAbout(const Input& input) {
    if (aboutConfirmClear_) {
        if (input.back) {
            aboutConfirmClear_ = false;
            aboutMessage_ = "Limpeza cancelada";
            return;
        }
        if (!input.accept) return;
        if (initialSyncRunning_ || nextPageLoading_) {
            aboutConfirmClear_ = false;
            aboutMessage_ = "Aguarde a atualizacao em andamento terminar";
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
    if (input.right && aboutOption_ < 3) ++aboutOption_;
    if (!input.accept) return;
    if (aboutOption_ == 0) {
        about_ = false;
        synchronizeCatalog();
    } else if (aboutOption_ == 1) {
        if (updateInfo_.available) openUpdateDialog();
        else startUpdateCheck(true);
    } else if (aboutOption_ == 2) {
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
    if (filterSection_ == 2) return gameModeIndex_;
    if (filterSection_ == 3) return static_cast<int>(filter_.sort);
    if (filterSection_ == 4) return backlogFilterIndex_;
    return 0;
}

int App::filterOptionCount() const {
    if (filterSection_ == 0) return static_cast<int>(genres_.size());
    if (filterSection_ == 1) return 3;
    if (filterSection_ == 2) return 4;
    if (filterSection_ == 3) return 5;
    if (filterSection_ == 4) return 5;
    return 5;
}

int App::filterColumns() const {
    if (filterSection_ == 0) return 4;
    if (filterSection_ == 1) return 3;
    if (filterSection_ == 2) return 2;
    if (filterSection_ == 3) return 3;
    if (filterSection_ == 4) return 5;
    return 3;
}

void App::openFilterPanel(int section) {
    const int lastSection = backlogTab_ ? 4 : 3;
    filterSection_ = std::max(0, std::min(section, lastSection));
    filterOption_ = currentFilterOption();
    filterPanel_ = true;
}

void App::switchFilterSection(int direction) {
    const int sectionCount = backlogTab_ ? 5 : 4;
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
        if (gameModeIndex_ != filterOption_) {
            gameModeIndex_ = filterOption_;
            filter_.gameMode = static_cast<GameModeFilter>(gameModeIndex_);
            if (favoritesTab_ || backlogTab_) refresh(); else loadCurrentFiltersFirstPage();
        }
    } else if (filterSection_ == 3) {
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
    const bool touchBrowse = touchMode_ && !touchPreviewActive_;
    const int rows = touchBrowse ? 3 : gridRows();
    const int visibleCount = columns * rows;
    const int selectedRow = selected_ / columns;
    const int firstRow = touchBrowse ? touchScrollY_ / touchGridStride() :
                         std::max(0, selectedRow - (rows - 1));
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
        "", "popular", "releases", "top-rated", "indies", "hidden-gems", "upcoming"
    };
    return slugs[std::max(0, std::min(discoveryIndex_, 6))];
}

void App::updateDiscoverySourceOrdering() {
    filter_.preserveSourceOrder =
        ((discoveryIndex_ == 1 || discoveryIndex_ == 4) && filter_.sort == SortMode::Popular) ||
        ((discoveryIndex_ == 2 || discoveryIndex_ == 6) && filter_.sort == SortMode::Release);
}

void App::captureDiscoveryReturnPoint() {
    discoveryReturnGames_ = catalog_.all();
    discoveryReturnFilter_ = filter_;
    discoveryReturnGenreIndex_ = genreIndex_;
    discoveryReturnHighlightIndex_ = highlightIndex_;
    discoveryReturnGameModeIndex_ = gameModeIndex_;
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
        gameModeIndex_ = discoveryReturnGameModeIndex_;
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
    nextIndex = std::max(0, std::min(nextIndex, 6));
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
        : ((nextIndex == 2 || nextIndex == 6) ? SortMode::Release : SortMode::Score);
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
    if (input.right && discoveryCursor_ < 6) ++discoveryCursor_;
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

std::string App::activeGameModeSlug() const {
    switch (filter_.gameMode) {
        case GameModeFilter::SinglePlayer: return "single-player";
        case GameModeFilter::CoOp: return "co-op";
        case GameModeFilter::Multiplayer: return "multiplayer";
        case GameModeFilter::All: default: return "";
    }
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
        filter_.query.empty() && genreIndex_ == 0 && highlightIndex_ == 0 && gameModeIndex_ == 0 &&
        discoveryIndex_ == 0 && filter_.sort == SortMode::Score;
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
    if (initialSyncRunning_ || nextPageLoading_) {
        status_ = "Atualizacao em andamento";
        return;
    }
    if (!networkReady_ || !apiInitialized_) {
        status_ = "Rede indisponivel; o cache continua ativo";
        return;
    }
    status_ = "Atualizando catalogo...";
    const ApiResult result = api_.synchronize(activeGenreSlug(), 1, filter_.query,
                                             activeOrderingSlug(), activeStatusParam(),
                                             activeMinRating(), "", activeDiscoverySlug(),
                                             activeGameModeSlug());
    status_ = result.message;
    if (!result.success) return;
    std::vector<Game> synchronizedGames = result.games;
    sortSearchPage(synchronizedGames);
    catalog_.replace(std::move(synchronizedGames));
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
    const std::string gameMode = activeGameModeSlug();
    if (networkReady_ && apiInitialized_) {
        result = api_.synchronize(genreSlug, page, query, ordering, status, minRating,
                                  "", activeDiscoverySlug(), gameMode);
    }
    if (!result.success) {
        const ApiResult cached = api_.loadCache(genreSlug, page, query, ordering, status, minRating,
                                                 "", activeDiscoverySlug(), gameMode);
        if (cached.success) return cached;
    }
    return result;
}

void App::sortSearchPage(std::vector<Game>& games) const {
    if (filter_.query.empty() || games.size() < 2) return;

    Catalog pageCatalog(std::move(games));
    CatalogFilter sortFilter;
    sortFilter.sort = filter_.sort;
    const std::vector<const Game*> ordered = pageCatalog.filtered(sortFilter);
    games.clear();
    games.reserve(ordered.size());
    for (const Game* game : ordered) games.push_back(*game);
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
    std::vector<Game> filteredGames = result.games;
    sortSearchPage(filteredGames);
    catalog_.replace(std::move(filteredGames));
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
    gameModeIndex_ = returnPoint.gameModeIndex;
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
    returnPoint.gameModeIndex = gameModeIndex_;
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
    gameModeIndex_ = 0;
    backlogFilterIndex_ = 0;
    filter_ = CatalogFilter{};
    filter_.preserveSourceOrder = true;
    status_ = "Semelhantes a " + game.title;
    refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
}

void App::loadNextPage() {
    if (!usingApi_ || !hasMore_ || nextPageLoading_ || initialSyncRunning_) return;
    if (nextPageThread_.joinable()) nextPageThread_.join();

    pendingNextPageNumber_ = currentPage_ + 1;
    pendingNextPageGenre_ = activeGenreSlug();
    pendingNextPageQuery_ = filter_.query;
    pendingNextPageOrdering_ = activeOrderingSlug();
    pendingNextPageStatus_ = activeStatusParam();
    pendingNextPageMinRating_ = activeMinRating();
    pendingNextPageDiscovery_ = activeDiscoverySlug();
    pendingNextPageGameMode_ = activeGameModeSlug();
    pendingNextPage_ = ApiResult{};
    nextPageLoading_ = true;
    nextPageDone_.store(false, std::memory_order_release);
    status_ = "Carregando mais jogos...";

    const int page = pendingNextPageNumber_;
    const std::string genre = pendingNextPageGenre_;
    const std::string query = pendingNextPageQuery_;
    const std::string ordering = pendingNextPageOrdering_;
    const std::string pageStatus = pendingNextPageStatus_;
    const int minRating = pendingNextPageMinRating_;
    const std::string discovery = pendingNextPageDiscovery_;
    const std::string gameMode = pendingNextPageGameMode_;
    nextPageThread_ = std::thread([this, page, genre, query, ordering, pageStatus, minRating, discovery, gameMode]() {
        ApiResult result;
        if (networkReady_ && apiInitialized_) {
            result = api_.synchronize(genre, page, query, ordering, pageStatus, minRating, "", discovery, gameMode);
        }
        if (!result.success) {
            const ApiResult cached = api_.loadCache(
                genre, page, query, ordering, pageStatus, minRating, "", discovery, gameMode);
            if (cached.success) result = cached;
        }
        pendingNextPage_ = std::move(result);
        nextPageDone_.store(true, std::memory_order_release);
    });
}

void App::finishNextPageLoad() {
    if (!nextPageLoading_ || !nextPageDone_.load(std::memory_order_acquire)) return;
    if (nextPageThread_.joinable()) nextPageThread_.join();
    nextPageLoading_ = false;

    const bool sameCatalog = !favoritesTab_ && !backlogTab_ &&
        currentPage_ + 1 == pendingNextPageNumber_ &&
        activeGenreSlug() == pendingNextPageGenre_ &&
        activeGameModeSlug() == pendingNextPageGameMode_ &&
        filter_.query == pendingNextPageQuery_ &&
        activeOrderingSlug() == pendingNextPageOrdering_ &&
        activeStatusParam() == pendingNextPageStatus_ &&
        activeMinRating() == pendingNextPageMinRating_ &&
        activeDiscoverySlug() == pendingNextPageDiscovery_;
    if (!sameCatalog) return;
    if (!pendingNextPage_.success) {
        status_ = pendingNextPage_.message.empty()
            ? "Nao foi possivel carregar a proxima pagina"
            : pendingNextPage_.message;
        return;
    }

    sortSearchPage(pendingNextPage_.games);
    std::vector<Game> combined = catalog_.all();
    for (const Game& incoming : pendingNextPage_.games) {
        const bool duplicate = std::any_of(combined.begin(), combined.end(), [&incoming](const Game& existing) {
            return existing.id == incoming.id;
        });
        if (!duplicate) combined.push_back(incoming);
    }
    catalog_.replace(std::move(combined));
    currentPage_ = pendingNextPageNumber_;
    hasMore_ = pendingNextPage_.hasMore;
    status_ = "Pagina " + std::to_string(currentPage_) + " carregada • " +
              std::to_string(catalog_.all().size()) + " jogos";
    refresh();
    visibleCoverSignature_.clear();
    queueVisibleCovers();
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

    std::vector<Game> searchGames = result.games;
    sortSearchPage(searchGames);
    catalog_.replace(std::move(searchGames));
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
    CatalogFilter appliedFilter = filter_;
    if (!favoritesTab_ && !backlogTab_ && usingApi_) {
        appliedFilter.preserveSourceOrder = true;
    }
    games_ = source.filtered(appliedFilter);
    if (backlogTab_ && backlogFilterIndex_ > 0) {
        const auto expected = static_cast<BacklogStatus>(backlogFilterIndex_);
        games_.erase(std::remove_if(games_.begin(), games_.end(), [expected](const Game* game) {
            return game->backlogStatus != expected;
        }), games_.end());
    }
    selected_ = std::max(0, std::min(selected_, static_cast<int>(games_.size()) - 1));
    touchScrollY_ = std::max(0, std::min(touchScrollY_, maximumTouchScroll()));
}

const char* App::backlogFilterLabel() const {
    static const char* labels[] = {"Todos", "Quero jogar", "Jogando", "Finalizados", "Abandonados"};
    return labels[backlogFilterIndex_];
}

void App::resetFiltersForSearch() {
    genreIndex_ = 0;
    highlightIndex_ = 0;
    gameModeIndex_ = 0;
    backlogFilterIndex_ = 0;
    filter_.genre.clear();
    filter_.acclaimedOnly = false;
    filter_.upcomingOnly = false;
    filter_.gameMode = GameModeFilter::All;
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
    finishNextPageLoad();
    finishScreenshotLoad();
    finishUpdateCheck();
    finishUpdateInstall();
    if (updateDialogState_ != UpdateDialogState::Hidden) {
        Input dialogInput = input;
        if (input.touchReleased) {
            dialogInput = {};
            if (updateDialogState_ == UpdateDialogState::Available) {
                if (contains(input.touchX, input.touchY, 300, 500, 286, 70)) {
                    updateDialogOption_ = 0;
                    dialogInput.accept = true;
                } else if (contains(input.touchX, input.touchY, 694, 500, 286, 70)) {
                    updateDialogOption_ = 1;
                    dialogInput.accept = true;
                }
            } else if (updateDialogState_ == UpdateDialogState::Installing) {
                if (contains(input.touchX, input.touchY, 760, 548, 220, 56)) dialogInput.back = true;
            } else {
                dialogInput.accept = true;
            }
        }
        handleUpdateDialog(dialogInput);
        return;
    }
    Input resolvedInput = input;
    const bool controllerInput = input.up || input.down || input.left || input.right ||
        input.accept || input.back || input.search || input.sort || input.previousGenre ||
        input.nextGenre || input.viewMode || input.favorite || input.backlog ||
        input.surprise || input.sync;
    if (controllerInput && !input.touchActive && !input.touchReleased) {
        if (touchMode_ && !touchPreviewActive_ && !games_.empty()) {
            const int touchSelection = selectionForTouchScroll(
                touchScrollY_, touchGridStride(), gridColumns(), gridRows(),
                selected_, static_cast<int>(games_.size()));
            if (touchSelection != selected_) {
                previousSelected_ = selected_;
                selected_ = touchSelection;
                selectionAnimationStart_ = SDL_GetTicks();
            }
        }
        touchMode_ = false;
        touchPreviewActive_ = false;
        touchDragTracking_ = false;
    }
    if (input.touchBegan || input.touchActive || input.touchReleased) {
        if (!touchMode_) {
            touchScrollY_ = touchScrollForSelection(
                selected_, touchGridStride(), gridColumns(), gridRows(), maximumTouchScroll());
        }
        touchMode_ = true;
        updateTouchDrag(input);
    }
    if (input.touchReleased && handleTouch(input, resolvedInput)) return;
    const Input& currentInput = resolvedInput;
    if (about_) {
        handleAbout(currentInput);
        return;
    }
    if (backlogPanel_) {
        handleBacklogPanel(currentInput);
        return;
    }
    if (detailClosing_) return;
    if (screenshotFullscreen_) {
        if (!detailScreenshots_.empty()) {
            if (currentInput.left) {
                screenshotIndex_ = (screenshotIndex_ - 1 + static_cast<int>(detailScreenshots_.size())) %
                                   static_cast<int>(detailScreenshots_.size());
            }
            if (currentInput.right) {
                screenshotIndex_ = (screenshotIndex_ + 1) % static_cast<int>(detailScreenshots_.size());
            }
        }
        if (currentInput.back || currentInput.accept) screenshotFullscreen_ = false;
        return;
    }
    if (filterPanel_) {
        handleFilterPanel(currentInput);
        return;
    }
    if (details_) {
        if (!detailScreenshots_.empty()) {
            if (currentInput.left) {
                screenshotIndex_ = (screenshotIndex_ - 1 + static_cast<int>(detailScreenshots_.size())) %
                                   static_cast<int>(detailScreenshots_.size());
            }
            if (currentInput.right) {
                screenshotIndex_ = (screenshotIndex_ + 1) % static_cast<int>(detailScreenshots_.size());
            }
        }
        if (currentInput.favorite) toggleFavorite(detailGame_);
        if (currentInput.backlog) {
            openBacklogPanel(detailGame_, true);
            return;
        }
        if (currentInput.search) {
            loadSimilarGames(detailGame_);
            return;
        }
        if (currentInput.back) {
            details_ = false;
            detailClosing_ = true;
            detailTransitionStart_ = SDL_GetTicks();
        }
        if (currentInput.accept && !detailScreenshots_.empty()) screenshotFullscreen_ = true;
        return;
    }
    if (currentInput.sync) {
        openAbout();
        return;
    }
    if (discoveryFocus_) {
        handleDiscoveryRibbon(currentInput);
        return;
    }
    if (currentInput.sort) {
        openFilterPanel(0);
        return;
    }
    if (currentInput.previousGenre) {
        switchMainTab(-1);
        return;
    }
    if (currentInput.nextGenre) {
        switchMainTab(1);
        return;
    }
    if (currentInput.viewMode) {
        classicView_ = !classicView_;
        visibleCoverSignature_.clear();
        queueVisibleCovers();
        return;
    }
    if (currentInput.back) {
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
    if (currentInput.search) openSearch();
    if (currentInput.surprise) {
        surpriseMe();
        return;
    }
    if (currentInput.backlog && !games_.empty()) {
        openBacklogPanel(*games_[selected_], false);
        return;
    }
    if (currentInput.favorite && !games_.empty()) {
        toggleFavorite(*games_[selected_]);
        return;
    }
    const int columns = gridColumns();
    if (currentInput.up && similarReturnStack_.empty() && !favoritesTab_ && !backlogTab_ &&
        (games_.empty() || selected_ < columns)) {
        discoveryFocus_ = true;
        discoveryCursor_ = discoveryIndex_;
        status_ = "Escolha uma secao de descoberta";
        return;
    }
    if (currentInput.down && !favoritesTab_ && !backlogTab_ && usingApi_ && hasMore_ &&
        (games_.empty() || selected_ + columns >= static_cast<int>(games_.size()))) {
        loadNextPage();
    }
    if (games_.empty()) return;

    int next = selected_;
    if (currentInput.left && selected_ % columns > 0) --next;
    if (currentInput.right && selected_ % columns < columns - 1 && selected_ + 1 < static_cast<int>(games_.size())) ++next;
    if (currentInput.up && selected_ >= columns) next -= columns;
    if (currentInput.down && selected_ + columns < static_cast<int>(games_.size())) next += columns;
    next = std::max(0, std::min(next, static_cast<int>(games_.size()) - 1));
    if (next != selected_) {
        previousSelected_ = selected_;
        selected_ = next;
        selectionAnimationStart_ = SDL_GetTicks();
    }
    queueVisibleCovers();
    if (currentInput.accept) openSelectedDetails();
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

    const int gridTop = (!backlogTab_ && !favoritesTab_) ? 190 : 160;
    const SDL_Rect gridClip{0, gridTop, kWidth, 652 - gridTop};
    SDL_RenderSetClipRect(renderer, &gridClip);
    gridView_.render(
        renderer, text, images, games_, selected_, previousSelected_,
        selectionAnimationStart_, gridRevealStart_, classicView_,
        backlogTab_, favoritesTab_, !touchMode_ || touchPreviewActive_,
        touchMode_ && !touchPreviewActive_, touchScrollY_,
        [this](const std::string& id) { return isFavorite(id); },
        [this](const std::string& id) { return backlogStatus(id); });
    SDL_RenderSetClipRect(renderer, nullptr);
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
                                aboutCacheBytes_, api_.cacheLimitBytes(), aboutMessage_, aboutConfirmClear_,
                                aboutUpdateSubtitle());
    }
    layoutView_.renderTabTransition(renderer, tabTransitionStart_, details_, detailClosing_);
    if (updateDialogState_ != UpdateDialogState::Hidden) {
        panelsView_.renderUpdateDialog(renderer, text, updateDialogState_, updateInfo_,
                                       updateDialogOption_, updateDownloadedBytes_.load(),
                                       updateDialogMessage_);
    }
}

}  // namespace vitrine
