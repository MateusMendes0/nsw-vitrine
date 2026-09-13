#include "cache_store.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace vitrine {
namespace {

struct CacheFile {
    std::string path;
    std::uint64_t size = 0;
    std::time_t modifiedAt = 0;
    std::uint64_t sessionUsage = 0;
};

std::string normalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

bool isDotEntry(const char* name) {
    return std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0;
}

void collectFiles(const std::string& path, std::vector<CacheFile>& files,
                  std::uint64_t& totalBytes) {
    DIR* directory = opendir(path.c_str());
    if (!directory) return;
    while (dirent* entry = readdir(directory)) {
        if (isDotEntry(entry->d_name)) continue;
        const std::string child = path + "/" + entry->d_name;
        struct stat info{};
        if (stat(child.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) {
            collectFiles(child, files, totalBytes);
        } else if (info.st_size >= 0) {
            const auto size = static_cast<std::uint64_t>(info.st_size);
            files.push_back({child, size, info.st_mtime});
            totalBytes += size;
        }
    }
    closedir(directory);
}

std::uint64_t directorySizeBytes(const std::string& path) {
    std::vector<CacheFile> files;
    std::uint64_t totalBytes = 0;
    collectFiles(path, files, totalBytes);
    return totalBytes;
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
            if (!clearDirectoryContents(child)) success = false;
        } else if (std::remove(child.c_str()) != 0) {
            success = false;
        }
    }
    closedir(directory);
    return success;
}

bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

std::uint64_t fileSizeBytes(const std::string& path) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || info.st_size < 0 || S_ISDIR(info.st_mode)) return 0;
    return static_cast<std::uint64_t>(info.st_size);
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
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

}  // namespace

CacheStore::CacheStore(std::string rootPath, std::uint64_t maximumBytes,
                       std::uint64_t trimTargetBytes)
    : rootPath_(normalizePath(std::move(rootPath))),
      maximumBytes_(std::max<std::uint64_t>(1, maximumBytes)),
      trimTargetBytes_(std::min(trimTargetBytes, maximumBytes_)) {}

std::uint64_t CacheStore::maximumBytes() const {
    return maximumBytes_;
}

std::uint64_t CacheStore::sizeBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sizeKnown_) measureLocked();
    return currentBytes_;
}

bool CacheStore::clear() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool cleared = clearDirectoryContents(rootPath_);
    if (cleared) {
        currentBytes_ = 0;
        sizeKnown_ = true;
        usageOrder_.clear();
    } else {
        sizeKnown_ = false;
    }
    return cleared;
}

bool CacheStore::contains(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string normalized = normalizePath(path);
    if (!fileExists(normalized)) return false;
    markUsedLocked(normalized);
    return true;
}

std::string CacheStore::read(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string normalized = normalizePath(path);
    std::string contents = readFile(normalized);
    if (!contents.empty()) markUsedLocked(normalized);
    return contents;
}

bool CacheStore::write(const std::string& path, const std::string& contents) const {
    if (contents.size() > maximumBytes_) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string normalized = normalizePath(path);
    if (!sizeKnown_) measureLocked();
    const std::uint64_t previousBytes = fileSizeBytes(normalized);
    if (!writeFile(normalized, contents)) return false;
    currentBytes_ -= std::min(currentBytes_, previousBytes);
    currentBytes_ += static_cast<std::uint64_t>(contents.size());
    markUsedLocked(normalized);
    if (currentBytes_ <= maximumBytes_) return true;
    if (pruneLocked(normalized)) return true;

    // Keep the maximum strict if older files could not be removed.
    if (std::remove(normalized.c_str()) == 0) usageOrder_.erase(normalized);
    sizeKnown_ = false;
    pruneLocked({});
    return false;
}

void CacheStore::enforceLimit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    pruneLocked({});
}

void CacheStore::measureLocked() const {
    currentBytes_ = directorySizeBytes(rootPath_);
    sizeKnown_ = true;
}

void CacheStore::markUsedLocked(const std::string& path) const {
    usageOrder_[path] = ++usageCounter_;
}

bool CacheStore::pruneLocked(const std::string& protectedPath) const {
    std::vector<CacheFile> files;
    std::uint64_t totalBytes = 0;
    collectFiles(rootPath_, files, totalBytes);
    currentBytes_ = totalBytes;
    sizeKnown_ = true;
    if (totalBytes <= maximumBytes_) return true;

    for (CacheFile& file : files) {
        const auto usage = usageOrder_.find(file.path);
        if (usage != usageOrder_.end()) file.sessionUsage = usage->second;
    }

    std::sort(files.begin(), files.end(), [](const CacheFile& left, const CacheFile& right) {
        const bool leftUsed = left.sessionUsage != 0;
        const bool rightUsed = right.sessionUsage != 0;
        if (leftUsed != rightUsed) return !leftUsed;
        if (leftUsed && left.sessionUsage != right.sessionUsage) {
            return left.sessionUsage < right.sessionUsage;
        }
        if (left.modifiedAt != right.modifiedAt) return left.modifiedAt < right.modifiedAt;
        return left.path < right.path;
    });
    for (const CacheFile& file : files) {
        if (totalBytes <= trimTargetBytes_) break;
        if (file.path == protectedPath) continue;
        if (std::remove(file.path.c_str()) == 0) {
            totalBytes -= file.size;
            currentBytes_ = totalBytes;
            usageOrder_.erase(file.path);
        }
    }
    return totalBytes <= maximumBytes_;
}

}  // namespace vitrine
