#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vitrine {

enum class GameType {
    Game,
    Demo,
    Dlc,
};

enum class BacklogStatus {
    None,
    WantToPlay,
    Playing,
    Completed,
    Dropped,
};

struct Color {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

struct Game {
    std::string id;
    std::string title;
    std::string tagline;
    std::string description;
    std::string studio;
    std::vector<std::string> genres;
    GameType type;
    int releaseYear;
    float score;
    float mainHours;
    float completionHours;
    Color coverTop;
    Color coverBottom;
    std::string imageUrl{};
    std::string localImagePath{};
    std::string coverImageUrl{};
    std::string localCoverImagePath{};
    std::string sourceName{};
    std::string sourceUrl{};
    bool averagePlaytime = false;
    std::string publisher{};
    std::string releaseDate{};
    std::string ageRating{};
    std::string website{};
    int ratingsCount = 0;
    BacklogStatus backlogStatus = BacklogStatus::None;
    std::vector<std::string> themes{};
    std::vector<std::string> gameModes{};
    std::vector<std::string> perspectives{};
    std::string franchise{};
    int videosCount = 0;
};

const char* gameTypeLabel(GameType type);
const char* backlogStatusLabel(BacklogStatus status);

}  // namespace vitrine
