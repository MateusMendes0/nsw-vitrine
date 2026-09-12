#include "update_utils.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

namespace vitrine {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

std::uint32_t rotateRight(std::uint32_t value, unsigned int bits) {
    return (value >> bits) | (value << (32u - bits));
}

bool parseVersion(const std::string& value, std::vector<std::uint32_t>& components) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    if (begin < value.size() && (value[begin] == 'v' || value[begin] == 'V')) ++begin;
    std::size_t end = value.find_first_of("-+ \t\r\n", begin);
    if (end == std::string::npos) end = value.size();
    if (begin == end) return false;

    while (begin < end) {
        const std::size_t separator = value.find('.', begin);
        const std::size_t componentEnd = separator == std::string::npos ? end : std::min(separator, end);
        if (componentEnd == begin) return false;
        std::uint64_t number = 0;
        for (std::size_t index = begin; index < componentEnd; ++index) {
            const unsigned char character = static_cast<unsigned char>(value[index]);
            if (!std::isdigit(character)) return false;
            number = number * 10u + static_cast<unsigned int>(character - '0');
            if (number > std::numeric_limits<std::uint32_t>::max()) return false;
        }
        components.push_back(static_cast<std::uint32_t>(number));
        if (separator == std::string::npos || separator >= end) break;
        begin = separator + 1;
    }
    return !components.empty();
}

}  // namespace

Sha256::Sha256()
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u} {}

void Sha256::transform(const std::uint8_t block[64]) {
    std::uint32_t words[64]{};
    for (std::size_t index = 0; index < 16; ++index) {
        const std::size_t offset = index * 4;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24u) |
                       (static_cast<std::uint32_t>(block[offset + 1]) << 16u) |
                       (static_cast<std::uint32_t>(block[offset + 2]) << 8u) |
                       static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (std::size_t index = 16; index < 64; ++index) {
        const std::uint32_t s0 = rotateRight(words[index - 15], 7u) ^
                                 rotateRight(words[index - 15], 18u) ^
                                 (words[index - 15] >> 3u);
        const std::uint32_t s1 = rotateRight(words[index - 2], 17u) ^
                                 rotateRight(words[index - 2], 19u) ^
                                 (words[index - 2] >> 10u);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t index = 0; index < 64; ++index) {
        const std::uint32_t upperSigma1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
        const std::uint32_t choice = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 = h + upperSigma1 + choice + kRoundConstants[index] + words[index];
        const std::uint32_t upperSigma0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = upperSigma0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t size) {
    if (finished_ || !data || size == 0) return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        buffer_[bufferSize_++] = bytes[index];
        if (bufferSize_ == buffer_.size()) {
            transform(buffer_.data());
            transformedBits_ += 512u;
            bufferSize_ = 0;
        }
    }
}

std::string Sha256::finishHex() {
    if (!finished_) {
        const std::uint64_t totalBits = transformedBits_ + static_cast<std::uint64_t>(bufferSize_) * 8u;
        buffer_[bufferSize_++] = 0x80u;
        if (bufferSize_ > 56) {
            while (bufferSize_ < 64) buffer_[bufferSize_++] = 0;
            transform(buffer_.data());
            bufferSize_ = 0;
        }
        while (bufferSize_ < 56) buffer_[bufferSize_++] = 0;
        for (int shift = 7; shift >= 0; --shift) {
            buffer_[bufferSize_++] = static_cast<std::uint8_t>(totalBits >> (shift * 8));
        }
        transform(buffer_.data());
        for (std::size_t index = 0; index < state_.size(); ++index) {
            digest_[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24u);
            digest_[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16u);
            digest_[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8u);
            digest_[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
        }
        finished_ = true;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    std::string output;
    output.reserve(64);
    for (const std::uint8_t byte : digest_) {
        output.push_back(kHex[byte >> 4u]);
        output.push_back(kHex[byte & 0x0fu]);
    }
    return output;
}

int compareSemanticVersions(const std::string& left, const std::string& right) {
    std::vector<std::uint32_t> leftComponents;
    std::vector<std::uint32_t> rightComponents;
    if (!parseVersion(left, leftComponents) || !parseVersion(right, rightComponents)) return 0;
    const std::size_t count = std::max(leftComponents.size(), rightComponents.size());
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint32_t leftValue = index < leftComponents.size() ? leftComponents[index] : 0;
        const std::uint32_t rightValue = index < rightComponents.size() ? rightComponents[index] : 0;
        if (leftValue < rightValue) return -1;
        if (leftValue > rightValue) return 1;
    }
    return 0;
}

std::string sha256Hex(const std::string& value) {
    Sha256 hash;
    hash.update(value.data(), value.size());
    return hash.finishHex();
}

}  // namespace vitrine
