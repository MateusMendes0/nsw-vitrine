#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace vitrine {

class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);
    std::string finishHex();

private:
    void transform(const std::uint8_t block[64]);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::array<std::uint8_t, 32> digest_{};
    std::uint64_t transformedBits_ = 0;
    std::size_t bufferSize_ = 0;
    bool finished_ = false;
};

// Returns -1 when left is older, 0 when equal and 1 when newer.
// A leading "v" and SemVer prerelease/build suffixes are accepted.
int compareSemanticVersions(const std::string& left, const std::string& right);

std::string sha256Hex(const std::string& value);

}  // namespace vitrine
