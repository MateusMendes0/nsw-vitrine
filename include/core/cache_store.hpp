#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vitrine {

class CacheStore {
public:
    static constexpr std::uint64_t kDefaultMaximumBytes = 100ull * 1024ull * 1024ull;
    static constexpr std::uint64_t kDefaultTrimTargetBytes = 80ull * 1024ull * 1024ull;

    explicit CacheStore(std::string rootPath,
                        std::uint64_t maximumBytes = kDefaultMaximumBytes,
                        std::uint64_t trimTargetBytes = kDefaultTrimTargetBytes);

    std::uint64_t maximumBytes() const;
    std::uint64_t sizeBytes() const;
    bool clear() const;
    bool contains(const std::string& path) const;
    std::string read(const std::string& path) const;
    bool write(const std::string& path, const std::string& contents) const;
    void enforceLimit() const;

private:
    void measureLocked() const;
    void markUsedLocked(const std::string& path) const;
    bool pruneLocked(const std::string& protectedPath) const;

    std::string rootPath_;
    std::uint64_t maximumBytes_;
    std::uint64_t trimTargetBytes_;
    mutable std::uint64_t currentBytes_ = 0;
    mutable std::uint64_t usageCounter_ = 0;
    mutable bool sizeKnown_ = false;
    mutable std::unordered_map<std::string, std::uint64_t> usageOrder_;
    mutable std::mutex mutex_;
};

}  // namespace vitrine
