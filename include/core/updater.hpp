#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace vitrine {

enum class UpdateDialogState {
    Hidden,
    Available,
    Installing,
    Installed,
    Error,
};

struct UpdateInfo {
    bool available = false;
    std::string version;
    std::string downloadUrl;
    std::string sha256;
    std::string notes;
    std::uint64_t size = 0;
};

struct UpdateInstallResult {
    bool success = false;
    bool cancelled = false;
    std::string message;
};

class AppUpdater {
public:
    explicit AppUpdater(std::string executablePath);

    bool canInstall() const;
    bool check(UpdateInfo& info, std::string& error) const;
    UpdateInstallResult install(const UpdateInfo& info,
                                std::atomic<std::uint64_t>& downloadedBytes,
                                std::atomic<bool>& cancelRequested) const;

    const std::string& executablePath() const { return executablePath_; }

private:
    std::string executablePath_;
};

}  // namespace vitrine
