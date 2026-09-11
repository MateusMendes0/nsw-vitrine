#include "api_client.hpp"

#include <curl/curl.h>
#include <jansson.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace vitrine {
namespace {

constexpr const char* kApiBaseUrl = "https://vitrine.mateusmendes.dev";

struct DownloadBuffer {
    std::string data;
    std::size_t limit = 0;
    bool overflow = false;
};

std::size_t writeToBuffer(char* contents, std::size_t size, std::size_t count, void* userData) {
    const std::size_t bytes = size * count;
    DownloadBuffer* buffer = static_cast<DownloadBuffer*>(userData);
    if (buffer->data.size() + bytes > buffer->limit) {
        buffer->overflow = true;
        return 0;
    }
    buffer->data.append(contents, bytes);
    return bytes;
}

bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::string urlEncode(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (const unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_' || character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(kHex[character >> 4]);
            encoded.push_back(kHex[character & 0x0F]);
        }
    }
    return encoded;
}

std::string queryCacheKey(const std::string& query) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char character : query) {
        hash ^= character;
        hash *= 16777619u;
    }
    char output[9] = {};
    std::snprintf(output, sizeof(output), "%08x", hash);
    return output;
}

bool writeFile(const std::string& path, const std::string& contents) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file.good()) return false;
    }
    std::remove(path.c_str());
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

void createDirectory(const std::string& path) {
    if (mkdir(path.c_str(), 0777) != 0 && errno != EEXIST) {
        // The caller will report a useful file-write error if creation failed.
    }
}

bool isDotEntry(const char* name) {
    return std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0;
}

std::uint64_t directorySizeBytes(const std::string& path) {
    DIR* directory = opendir(path.c_str());
    if (!directory) return 0;
    std::uint64_t total = 0;
    while (dirent* entry = readdir(directory)) {
        if (isDotEntry(entry->d_name)) continue;
        const std::string child = path + "/" + entry->d_name;
        struct stat info{};
        if (stat(child.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) total += directorySizeBytes(child);
        else if (info.st_size > 0) total += static_cast<std::uint64_t>(info.st_size);
    }
    closedir(directory);
    return total;
}

bool clearDirectoryContents(const std::string& path) {
    DIR* directory = opendir(path.c_str());
    if (!directory) return errno == ENOENT;
    bool success = true;
    while (dirent* entry = readdir(directory)) {
        if (isDotEntry(entry->d_name)) continue;
        const std::string child = path + "/" + entry->d_name;
        struct stat info{};
        if (stat(child.c_str(), &info) != 0) {
            success = false;
            continue;
        }
        if (S_ISDIR(info.st_mode)) {
            if (!clearDirectoryContents(child) || rmdir(child.c_str()) != 0) success = false;
        } else if (std::remove(child.c_str()) != 0) {
            success = false;
        }
    }
    closedir(directory);
    return success;
}

std::string jsonString(json_t* object, const char* key) {
    json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : std::string();
}

int jsonInteger(json_t* object, const char* key) {
    json_t* value = json_object_get(object, key);
    return json_is_integer(value) ? static_cast<int>(json_integer_value(value)) : 0;
}

double jsonNumber(json_t* object, const char* key) {
    json_t* value = json_object_get(object, key);
    if (json_is_real(value)) return json_real_value(value);
    if (json_is_integer(value)) return static_cast<double>(json_integer_value(value));
    return 0.0;
}

std::string translateGenre(const std::string& genre) {
    if (genre == "Action") return "Acao";
    if (genre == "Adventure") return "Aventura";
    if (genre == "Arcade") return "Arcade";
    if (genre == "Board Games") return "Tabuleiro";
    if (genre == "Card") return "Cartas";
    if (genre == "Casual") return "Casual";
    if (genre == "Educational") return "Educativo";
    if (genre == "Family") return "Familia";
    if (genre == "Fighting") return "Luta";
    if (genre == "Hack and slash/Beat 'em up") return "Hack and Slash";
    if (genre == "Indie") return "Indie";
    if (genre == "Massively Multiplayer") return "Multijogador";
    if (genre == "Music") return "Musica";
    if (genre == "Pinball") return "Pinball";
    if (genre == "Platformer") return "Plataforma";
    if (genre == "Platform") return "Plataforma";
    if (genre == "Puzzle") return "Puzzle";
    if (genre == "Racing") return "Corrida";
    if (genre == "RPG") return "RPG";
    if (genre == "Role-playing (RPG)") return "RPG";
    if (genre == "Shooter") return "Tiro";
    if (genre == "Simulation") return "Simulacao";
    if (genre == "Simulator") return "Simulacao";
    if (genre == "Sports") return "Esporte";
    if (genre == "Sport") return "Esporte";
    if (genre == "Strategy") return "Estrategia";
    if (genre == "Tactical") return "Tatica";
    if (genre == "Visual Novel") return "Visual Novel";
    if (genre == "Card & Board Game") return "Cartas";
    return genre;
}

Color colorFromId(const std::string& id, int shift) {
    unsigned int hash = 2166136261u;
    for (unsigned char value : id) {
        hash ^= value;
        hash *= 16777619u;
    }
    hash ^= static_cast<unsigned int>(shift * 0x9e3779b9u);
    return {
        static_cast<std::uint8_t>(45 + ((hash >> 16) & 0x7f)),
        static_cast<std::uint8_t>(45 + ((hash >> 8) & 0x7f)),
        static_cast<std::uint8_t>(55 + (hash & 0x6f)),
    };
}

std::string joinNames(json_t* array) {
    if (!json_is_array(array)) return {};
    std::string result;
    const std::size_t count = json_array_size(array);
    for (std::size_t index = 0; index < count; ++index) {
        const std::string name = jsonString(json_array_get(array, index), "name");
        if (name.empty()) continue;
        if (!result.empty()) result += ", ";
        result += name;
    }
    return result;
}

std::vector<std::string> nameList(json_t* array) {
    std::vector<std::string> result;
    if (!json_is_array(array)) return result;
    const std::size_t count = json_array_size(array);
    for (std::size_t index = 0; index < count; ++index) {
        const std::string name = jsonString(json_array_get(array, index), "name");
        if (!name.empty()) result.push_back(name);
    }
    return result;
}

void readStringArray(json_t* object, const char* key, std::vector<std::string>& destination) {
    json_t* values = json_object_get(object, key);
    if (!json_is_array(values)) return;
    const std::size_t count = json_array_size(values);
    for (std::size_t index = 0; index < count; ++index) {
        json_t* value = json_array_get(values, index);
        if (json_is_string(value)) destination.emplace_back(json_string_value(value));
    }
}

void writeStringArray(json_t* object, const char* key, const std::vector<std::string>& values) {
    json_t* array = json_array();
    for (const std::string& value : values) json_array_append_new(array, json_string(value.c_str()));
    json_object_set_new(object, key, array);
}

bool storedGameFromJson(json_t* item, Game& game) {
    if (!json_is_object(item)) return false;
    game.id = jsonString(item, "id");
    game.title = jsonString(item, "title");
    if (game.id.empty() || game.title.empty()) return false;
    game.tagline = jsonString(item, "tagline");
    game.description = jsonString(item, "description");
    game.studio = jsonString(item, "studio");
    game.type = static_cast<GameType>(std::max(0, std::min(2, jsonInteger(item, "type"))));
    game.releaseYear = jsonInteger(item, "release_year");
    game.score = static_cast<float>(jsonNumber(item, "score"));
    game.mainHours = static_cast<float>(jsonNumber(item, "main_hours"));
    game.completionHours = static_cast<float>(jsonNumber(item, "completion_hours"));
    game.coverTop = {static_cast<std::uint8_t>(jsonInteger(item, "top_r")),
                     static_cast<std::uint8_t>(jsonInteger(item, "top_g")),
                     static_cast<std::uint8_t>(jsonInteger(item, "top_b"))};
    game.coverBottom = {static_cast<std::uint8_t>(jsonInteger(item, "bottom_r")),
                        static_cast<std::uint8_t>(jsonInteger(item, "bottom_g")),
                        static_cast<std::uint8_t>(jsonInteger(item, "bottom_b"))};
    game.imageUrl = jsonString(item, "image_url");
    game.localImagePath = jsonString(item, "local_image_path");
    game.coverImageUrl = jsonString(item, "cover_image_url");
    game.localCoverImagePath = jsonString(item, "local_cover_image_path");
    if (game.coverImageUrl.empty()) game.coverImageUrl = game.imageUrl;
    if (game.localCoverImagePath.empty()) game.localCoverImagePath = game.localImagePath;
    game.sourceName = jsonString(item, "source_name");
    game.sourceUrl = jsonString(item, "source_url");
    game.averagePlaytime = json_is_true(json_object_get(item, "average_playtime"));
    game.publisher = jsonString(item, "publisher");
    game.releaseDate = jsonString(item, "release_date");
    game.ageRating = jsonString(item, "age_rating");
    game.website = jsonString(item, "website");
    game.ratingsCount = jsonInteger(item, "ratings_count");
    game.backlogStatus = static_cast<BacklogStatus>(std::max(0, std::min(4, jsonInteger(item, "backlog_status"))));
    game.franchise = jsonString(item, "franchise");
    game.videosCount = jsonInteger(item, "videos_count");
    readStringArray(item, "genres", game.genres);
    readStringArray(item, "themes", game.themes);
    readStringArray(item, "game_modes", game.gameModes);
    readStringArray(item, "perspectives", game.perspectives);
    if (game.genres.empty()) game.genres.push_back("Outros");
    return true;
}

json_t* storedGameToJson(const Game& game) {
    json_t* item = json_object();
    json_object_set_new(item, "id", json_string(game.id.c_str()));
    json_object_set_new(item, "title", json_string(game.title.c_str()));
    json_object_set_new(item, "tagline", json_string(game.tagline.c_str()));
    json_object_set_new(item, "description", json_string(game.description.c_str()));
    json_object_set_new(item, "studio", json_string(game.studio.c_str()));
    json_object_set_new(item, "type", json_integer(static_cast<int>(game.type)));
    json_object_set_new(item, "release_year", json_integer(game.releaseYear));
    json_object_set_new(item, "score", json_real(game.score));
    json_object_set_new(item, "main_hours", json_real(game.mainHours));
    json_object_set_new(item, "completion_hours", json_real(game.completionHours));
    json_object_set_new(item, "top_r", json_integer(game.coverTop.r));
    json_object_set_new(item, "top_g", json_integer(game.coverTop.g));
    json_object_set_new(item, "top_b", json_integer(game.coverTop.b));
    json_object_set_new(item, "bottom_r", json_integer(game.coverBottom.r));
    json_object_set_new(item, "bottom_g", json_integer(game.coverBottom.g));
    json_object_set_new(item, "bottom_b", json_integer(game.coverBottom.b));
    json_object_set_new(item, "image_url", json_string(game.imageUrl.c_str()));
    json_object_set_new(item, "local_image_path", json_string(game.localImagePath.c_str()));
    json_object_set_new(item, "cover_image_url", json_string(game.coverImageUrl.c_str()));
    json_object_set_new(item, "local_cover_image_path", json_string(game.localCoverImagePath.c_str()));
    json_object_set_new(item, "source_name", json_string(game.sourceName.c_str()));
    json_object_set_new(item, "source_url", json_string(game.sourceUrl.c_str()));
    json_object_set_new(item, "average_playtime", json_boolean(game.averagePlaytime));
    json_object_set_new(item, "publisher", json_string(game.publisher.c_str()));
    json_object_set_new(item, "release_date", json_string(game.releaseDate.c_str()));
    json_object_set_new(item, "age_rating", json_string(game.ageRating.c_str()));
    json_object_set_new(item, "website", json_string(game.website.c_str()));
    json_object_set_new(item, "ratings_count", json_integer(game.ratingsCount));
    json_object_set_new(item, "backlog_status", json_integer(static_cast<int>(game.backlogStatus)));
    json_object_set_new(item, "franchise", json_string(game.franchise.c_str()));
    json_object_set_new(item, "videos_count", json_integer(game.videosCount));
    writeStringArray(item, "genres", game.genres);
    writeStringArray(item, "themes", game.themes);
    writeStringArray(item, "game_modes", game.gameModes);
    writeStringArray(item, "perspectives", game.perspectives);
    return item;
}

std::vector<Game> loadStoredGames(const std::string& path) {
    const std::string payload = readFile(path);
    if (payload.empty()) return {};
    json_error_t error{};
    json_t* root = json_loadb(payload.data(), payload.size(), 0, &error);
    if (!json_is_array(root)) {
        if (root) json_decref(root);
        return {};
    }
    std::vector<Game> games;
    const std::size_t count = json_array_size(root);
    games.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        Game game;
        if (storedGameFromJson(json_array_get(root, index), game)) games.push_back(std::move(game));
    }
    json_decref(root);
    return games;
}

bool saveStoredGames(const std::string& path, const std::vector<Game>& games) {
    json_t* root = json_array();
    if (!root) return false;
    for (const Game& game : games) json_array_append_new(root, storedGameToJson(game));
    char* serialized = json_dumps(root, JSON_INDENT(2) | JSON_ENSURE_ASCII);
    json_decref(root);
    if (!serialized) return false;
    const std::string payload(serialized);
    std::free(serialized);
    return writeFile(path, payload);
}

}  // namespace

CatalogApiClient::CatalogApiClient()
#ifdef __SWITCH__
    : basePath_("sdmc:/switch/switch-vitrine/")
#else
    : basePath_("runtime/")
#endif
{}

CatalogApiClient::~CatalogApiClient() {
    if (initialized_) curl_global_cleanup();
}

bool CatalogApiClient::initialize() {
    if (initialized_) return true;
#ifdef __SWITCH__
    createDirectory("sdmc:/switch");
    createDirectory("sdmc:/switch/switch-vitrine");
#else
    createDirectory("runtime");
#endif
    createDirectory(basePath_ + "cache");
    createDirectory(basePath_ + "cache/covers");
    createDirectory(basePath_ + "cache/screenshots");
    createDirectory(basePath_ + "cache/details");
    initialized_ = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return initialized_;
}

const std::string& CatalogApiClient::basePath() const {
    return basePath_;
}

std::vector<Game> CatalogApiClient::loadFavorites() const {
    return loadStoredGames(basePath_ + "favorites.json");
}

bool CatalogApiClient::saveFavorites(const std::vector<Game>& games) const {
    return saveStoredGames(basePath_ + "favorites.json", games);
}

std::vector<Game> CatalogApiClient::loadBacklog() const {
    return loadStoredGames(basePath_ + "backlog.json");
}

bool CatalogApiClient::saveBacklog(const std::vector<Game>& games) const {
    return saveStoredGames(basePath_ + "backlog.json", games);
}

std::uint64_t CatalogApiClient::cacheSizeBytes() const {
    return directorySizeBytes(basePath_ + "cache");
}

bool CatalogApiClient::clearCache(std::string& error) const {
    const std::string cacheRoot = basePath_ + "cache";
    if (!clearDirectoryContents(cacheRoot)) {
        error = "Nao foi possivel remover todo o cache";
        return false;
    }
    createDirectory(cacheRoot);
    createDirectory(cacheRoot + "/covers");
    createDirectory(cacheRoot + "/screenshots");
    createDirectory(cacheRoot + "/details");
    error.clear();
    return true;
}

std::string CatalogApiClient::cachePath(const std::string& genreSlug, int page,
                                        const std::string& query,
                                        const std::string& ordering,
                                        const std::string& status,
                                        int minRating,
                                        const std::string& themeSlug,
                                        const std::string& discoverySlug) const {
    const std::string genre = genreSlug.empty() ? "all" : genreSlug;
    const std::string theme = themeSlug.empty() ? "all" : themeSlug;
    const std::string search = query.empty() ? "all" : "search-" + queryCacheKey(query);
    const std::string order = ordering.empty() ? "metacritic" : ordering;
    const std::string st = status.empty() ? "all" : status;
    const std::string min = minRating > 0 ? std::to_string(minRating) : "0";
    const std::string discovery = discoverySlug.empty() ? "all" : discoverySlug;
    return basePath_ + "cache/catalog-igdb-v7-" + discovery + "-" + genre + "-" + theme + "-" + search + "-" +
           order + "-" + st + "-" + min + "-" + std::to_string(std::max(1, page)) + ".json";
}

ApiResult CatalogApiClient::loadCache(const std::string& genreSlug, int page,
                                      const std::string& query,
                                      const std::string& ordering,
                                      const std::string& status,
                                      int minRating,
                                      const std::string& themeSlug,
                                      const std::string& discoverySlug) const {
    const std::string payload = readFile(cachePath(genreSlug, page, query, ordering, status, minRating,
                                                   themeSlug, discoverySlug));
    if (payload.empty()) return {false, "Sem cache da API", {}};
    return parseCatalog(payload, "Pagina offline carregada");
}

ApiResult CatalogApiClient::synchronize(const std::string& genreSlug, int page,
                                        const std::string& query,
                                        const std::string& ordering,
                                        const std::string& status,
                                        int minRating,
                                        const std::string& themeSlug,
                                        const std::string& discoverySlug) const {
    if (!initialized_) return {false, "Rede nao inicializada", {}};
    std::string url = std::string(kApiBaseUrl) +
                      (discoverySlug.empty() ? "/v1/games" : "/v1/discovery/" + discoverySlug) +
                      "?page_size=40&page=" +
                      std::to_string(std::max(1, page));
    if (!ordering.empty()) url += "&ordering=" + ordering;
    if (!genreSlug.empty()) url += "&genres=" + genreSlug;
    if (!themeSlug.empty()) url += "&themes=" + themeSlug;
    if (!status.empty()) url += "&status=" + status;
    if (minRating > 0) url += "&min_rating=" + std::to_string(minRating);
    if (!query.empty()) url += "&search=" + urlEncode(query.substr(0, 80));

    std::string payload;
    std::string error;
    if (!request(url, payload, error, 6 * 1024 * 1024)) return {false, error, {}};

    ApiResult result = parseCatalog(payload, "Pagina IGDB carregada");
    if (!result.success) return result;
    if (!writeFile(cachePath(genreSlug, page, query, ordering, status, minRating,
                             themeSlug, discoverySlug), payload)) {
        result.message = "Atualizado, mas nao foi possivel salvar o cache";
    }
    return result;
}

bool CatalogApiClient::fetchSimilarGames(const Game& game, std::vector<Game>& similar,
                                         std::string& error) const {
    if (game.id.rfind("igdb-", 0) != 0) return false;
    const std::string numericId = game.id.substr(5);
    const std::string path = basePath_ + "cache/details/similar-" + numericId + ".json";
    std::string payload = readFile(path);
    if (payload.empty()) {
        if (!initialized_) {
            error = "Rede nao inicializada";
            return false;
        }
        const std::string url = std::string(kApiBaseUrl) + "/v1/games/" + numericId + "/similar";
        if (!request(url, payload, error, 4 * 1024 * 1024)) return false;
        writeFile(path, payload);
    }
    ApiResult parsed = parseCatalog(payload, "Jogos semelhantes");
    if (!parsed.success) {
        error = parsed.message;
        return false;
    }
    similar = std::move(parsed.games);
    return true;
}

bool CatalogApiClient::ensureCover(const Game& game, std::string& error) const {
    return ensureImage(game.imageUrl, game.localImagePath, error);
}

bool CatalogApiClient::ensurePortraitCover(const Game& game, std::string& error) const {
    return ensureImage(game.coverImageUrl, game.localCoverImagePath, error);
}

bool CatalogApiClient::ensureImage(const std::string& url, const std::string& path,
                                   std::string& error) const {
    if (url.empty() || path.empty()) return false;
    if (fileExists(path)) return true;
    std::string payload;
    if (!request(url, payload, error, 12 * 1024 * 1024)) return false;
    if (payload.size() < 64) {
        error = "Imagem recebida e invalida";
        return false;
    }
    if (!writeFile(path, payload)) {
        error = "Nao foi possivel salvar a capa";
        return false;
    }
    return true;
}

bool CatalogApiClient::fetchDetails(const Game& game, Game& enriched, std::string& error) const {
    enriched = game;
    if (game.id.rfind("igdb-", 0) != 0) return false;
    const std::string igdbId = game.id.substr(5);
    if (igdbId.empty()) return false;

    const std::string path = basePath_ + "cache/details/v3-" + game.id + ".json";
    std::string payload = readFile(path);
    if (payload.empty()) {
        const std::string url = std::string(kApiBaseUrl) + "/v1/games/" + igdbId;
        if (!request(url, payload, error, 6 * 1024 * 1024)) return false;
        writeFile(path, payload);
    }

    json_error_t jsonError{};
    json_t* root = json_loadb(payload.data(), payload.size(), 0, &jsonError);
    if (!json_is_object(root)) {
        if (root) json_decref(root);
        error = "Resposta de detalhes invalida";
        return false;
    }

    const std::string title = jsonString(root, "name");
    const std::string description = jsonString(root, "description_raw");
    const std::string released = jsonString(root, "released");
    const std::string developers = joinNames(json_object_get(root, "developers"));
    const std::string publishers = joinNames(json_object_get(root, "publishers"));
    const std::string imageUrl = jsonString(root, "background_image");
    const std::string coverImageUrl = jsonString(root, "cover_image");
    const std::string slug = jsonString(root, "slug");
    json_t* age = json_object_get(root, "esrb_rating");

    if (!title.empty()) enriched.title = title;
    if (!description.empty()) enriched.description = description;
    if (!developers.empty()) enriched.studio = developers;
    enriched.publisher = publishers;
    enriched.releaseDate = released;
    enriched.ageRating = json_is_object(age) ? jsonString(age, "name") : std::string();
    enriched.website = jsonString(root, "website");
    enriched.ratingsCount = jsonInteger(root, "ratings_count");
    enriched.themes = nameList(json_object_get(root, "themes"));
    enriched.gameModes = nameList(json_object_get(root, "game_modes"));
    enriched.perspectives = nameList(json_object_get(root, "player_perspectives"));
    enriched.franchise = jsonString(root, "franchise");
    enriched.videosCount = jsonInteger(root, "videos_count");
    if (released.size() >= 4) enriched.releaseYear = std::atoi(released.substr(0, 4).c_str());
    const int metacritic = jsonInteger(root, "metacritic");
    if (metacritic > 0) enriched.score = static_cast<float>(metacritic);
    else {
        const double rating = jsonNumber(root, "rating");
        if (rating > 0.0) enriched.score = static_cast<float>(rating * 20.0);
    }
    const int playtime = jsonInteger(root, "playtime");
    if (playtime > 0) enriched.mainHours = static_cast<float>(playtime);
    if (!imageUrl.empty()) enriched.imageUrl = imageUrl;
    if (!coverImageUrl.empty()) enriched.coverImageUrl = coverImageUrl;
    if (!slug.empty()) enriched.sourceUrl = "https://www.igdb.com/games/" + slug;

    json_t* timeToBeat = json_object_get(root, "time_to_beat");
    if (json_is_object(timeToBeat)) {
        const float hastily = static_cast<float>(jsonNumber(timeToBeat, "hastily_hours"));
        const float normally = static_cast<float>(jsonNumber(timeToBeat, "normally_hours"));
        const float completely = static_cast<float>(jsonNumber(timeToBeat, "completely_hours"));
        if (hastily > 0.0f || normally > 0.0f) {
            enriched.mainHours = hastily > 0.0f ? hastily : normally;
            enriched.averagePlaytime = false;
        }
        if (completely > 0.0f) enriched.completionHours = completely;
    }

    json_t* genres = json_object_get(root, "genres");
    if (json_is_array(genres)) {
        enriched.genres.clear();
        const std::size_t count = json_array_size(genres);
        for (std::size_t index = 0; index < count; ++index) {
            const std::string name = jsonString(json_array_get(genres, index), "name");
            if (!name.empty()) enriched.genres.push_back(translateGenre(name));
        }
    }
    json_decref(root);
    return true;
}

bool CatalogApiClient::ensureScreenshots(const Game& game, std::vector<std::string>& localPaths,
                                   std::string& error) const {
    localPaths.clear();
    if (game.id.rfind("igdb-", 0) != 0) return false;
    const std::string igdbId = game.id.substr(5);
    if (igdbId.empty()) return false;

    std::string metadata;
    const std::string url = std::string(kApiBaseUrl) + "/v1/games/" + igdbId + "/screenshots";
    if (!request(url, metadata, error, 1024 * 1024)) return false;

    json_error_t jsonError{};
    json_t* root = json_loadb(metadata.data(), metadata.size(), 0, &jsonError);
    if (!root) {
        error = "Resposta de screenshots invalida";
        return false;
    }
    json_t* results = json_object_get(root, "results");
    if (!json_is_array(results)) {
        json_decref(root);
        error = "API nao retornou screenshots";
        return false;
    }

    const std::size_t count = std::min<std::size_t>(6, json_array_size(results));
    for (std::size_t index = 0; index < count; ++index) {
        json_t* screenshot = json_array_get(results, index);
        const std::string imageUrl = jsonString(screenshot, "image");
        if (imageUrl.empty()) continue;
        const std::string path = basePath_ + "cache/screenshots/" + game.id + "-" +
                                 std::to_string(index) + ".img";
        if (!fileExists(path)) {
            std::string image;
            std::string imageError;
            if (!request(imageUrl, image, imageError, 12 * 1024 * 1024) || image.size() < 64 ||
                !writeFile(path, image)) {
                continue;
            }
        }
        localPaths.push_back(path);
    }
    json_decref(root);
    if (localPaths.empty()) {
        error = "Nenhuma screenshot disponivel";
        return false;
    }
    return true;
}

bool CatalogApiClient::request(const std::string& url, std::string& payload, std::string& error,
                         std::size_t maxBytes) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "Falha ao iniciar HTTPS";
        return false;
    }
    DownloadBuffer buffer;
    buffer.limit = maxBytes;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Switch-Vitrine/0.2 (Nintendo Switch homebrew)");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        error = buffer.overflow ? "Resposta excedeu o limite de memoria" :
                                 std::string("Falha de rede: ") + curl_easy_strerror(code);
        return false;
    }
    if (status < 200 || status >= 300) {
        error = "API respondeu HTTP " + std::to_string(status);
        return false;
    }
    payload = std::move(buffer.data);
    return true;
}

ApiResult CatalogApiClient::parseCatalog(const std::string& payload, const std::string& successMessage) const {
    json_error_t jsonError{};
    json_t* root = json_loadb(payload.data(), payload.size(), 0, &jsonError);
    if (!root) return {false, "JSON invalido: linha " + std::to_string(jsonError.line), {}};
    json_t* results = json_object_get(root, "results");
    if (!json_is_array(results)) {
        json_decref(root);
        return {false, "Resposta da API sem lista de jogos", {}};
    }

    std::vector<Game> games;
    const std::size_t count = json_array_size(results);
    games.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        json_t* item = json_array_get(results, index);
        if (!json_is_object(item)) continue;
        const int igdbId = jsonInteger(item, "id");
        const std::string title = jsonString(item, "name");
        const std::string slug = jsonString(item, "slug");
        if (igdbId <= 0 || title.empty()) continue;

        Game game;
        game.id = "igdb-" + std::to_string(igdbId);
        game.title = title;
        game.tagline = jsonString(item, "short_description");
        game.description = game.tagline;
        game.studio = joinNames(json_object_get(item, "developers"));
        if (game.studio.empty()) game.studio = "Desenvolvedora nao informada";
        game.type = GameType::Game;
        const std::string released = jsonString(item, "released");
        game.releaseYear = released.size() >= 4 ? std::atoi(released.substr(0, 4).c_str()) : 0;
        game.score = static_cast<float>(jsonInteger(item, "metacritic"));
        if (game.score <= 0.0f) game.score = static_cast<float>(jsonNumber(item, "rating") * 20.0);
        game.mainHours = static_cast<float>(jsonInteger(item, "playtime"));
        game.completionHours = 0.0f;
        game.coverTop = colorFromId(game.id, 1);
        game.coverBottom = colorFromId(game.id, 2);
        game.imageUrl = jsonString(item, "background_image");
        game.localImagePath = basePath_ + "cache/covers/" + game.id + ".img";
        game.coverImageUrl = jsonString(item, "cover_image");
        if (game.coverImageUrl.empty()) game.coverImageUrl = game.imageUrl;
        // O prefixo poster-v1 impede que screenshots horizontais mantidas pelo
        // cache de versoes anteriores sejam reutilizadas na nova vitrine.
        game.localCoverImagePath = basePath_ + "cache/covers/poster-v1-" + game.id + ".img";
        game.sourceName = "IGDB";
        game.sourceUrl = slug.empty() ? "https://www.igdb.com" : "https://www.igdb.com/games/" + slug;
        game.averagePlaytime = true;

        json_t* genres = json_object_get(item, "genres");
        if (json_is_array(genres)) {
            const std::size_t genreCount = json_array_size(genres);
            for (std::size_t genreIndex = 0; genreIndex < genreCount; ++genreIndex) {
                json_t* genre = json_array_get(genres, genreIndex);
                const std::string name = jsonString(genre, "name");
                if (!name.empty()) game.genres.push_back(translateGenre(name));
            }
        }
        if (game.genres.empty()) game.genres.push_back("Outros");
        if (game.tagline.empty()) {
            game.tagline = game.genres.front();
            if (game.studio != "Desenvolvedora nao informada") game.tagline += " da " + game.studio;
            if (game.releaseYear > 0) game.tagline += "  •  Lancado em " + std::to_string(game.releaseYear);
            game.description = game.tagline;
        }
        games.push_back(std::move(game));
    }
    const json_t* next = json_object_get(root, "next");
    const bool hasMore = json_is_string(next) && std::strlen(json_string_value(next)) > 0;
    json_decref(root);
    if (games.empty()) return {false, "A API nao retornou jogos validos", {}};
    return {true, successMessage + " (" + std::to_string(games.size()) + ")", std::move(games), hasMore};
}

}  // namespace vitrine
