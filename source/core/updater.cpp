#include "updater.hpp"

#include "ui_constants.hpp"
#include "update_utils.hpp"

#include <curl/curl.h>
#include <jansson.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace vitrine {
namespace {

constexpr const char* kLatestReleaseUrl =
    "https://api.github.com/repos/MateusMendes0/nsw-vitrine/releases/latest";
constexpr const char* kReleaseDownloadPrefix =
    "https://github.com/MateusMendes0/nsw-vitrine/releases/download/";
constexpr const char* kNroAssetName = "switch-vitrine.nro";
constexpr std::uint64_t kMinimumNroSize = 128u * 1024u;
constexpr std::uint64_t kMaximumNroSize = 64u * 1024u * 1024u;

struct MemoryDownload {
    std::string data;
    std::size_t limit = 0;
    bool overflow = false;
};

struct FileDownload {
    std::FILE* file = nullptr;
    std::uint64_t expectedSize = 0;
    std::uint64_t written = 0;
    bool overflow = false;
    bool writeFailed = false;
    Sha256 hash;
    std::atomic<std::uint64_t>* progress = nullptr;
    std::atomic<bool>* cancel = nullptr;
};

#ifdef __SWITCH__
bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

bool endsWith(const std::string& value, const char* suffix) {
    const std::size_t suffixLength = std::strlen(suffix);
    return value.size() >= suffixLength &&
           value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
}
#endif

std::string normalizeExecutablePath(std::string path) {
#ifdef __SWITCH__
    if (path.rfind("/switch/", 0) == 0) path = "sdmc:" + path;
    if (path.rfind("sdmc:/", 0) == 0 && endsWith(path, ".nro")) return path;
    const std::string fallback = "sdmc:/switch/switch-vitrine/switch-vitrine.nro";
    return fileExists(fallback) ? fallback : std::string();
#else
    (void)path;
    return {};
#endif
}

std::size_t writeToMemory(char* contents, std::size_t size, std::size_t count, void* userData) {
    const std::size_t bytes = size * count;
    auto* output = static_cast<MemoryDownload*>(userData);
    if (output->data.size() + bytes > output->limit) {
        output->overflow = true;
        return 0;
    }
    output->data.append(contents, bytes);
    return bytes;
}

std::size_t writeToFile(char* contents, std::size_t size, std::size_t count, void* userData) {
    const std::size_t bytes = size * count;
    auto* output = static_cast<FileDownload*>(userData);
    if (output->cancel->load()) return 0;
    if (output->written + bytes > output->expectedSize || output->written + bytes > kMaximumNroSize) {
        output->overflow = true;
        return 0;
    }
    const std::size_t written = std::fwrite(contents, 1, bytes, output->file);
    if (written != bytes) {
        output->writeFailed = true;
        return 0;
    }
    output->hash.update(contents, bytes);
    output->written += bytes;
    output->progress->store(output->written);
    return bytes;
}

int updateTransferProgress(void* userData, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* output = static_cast<const FileDownload*>(userData);
    return output->cancel->load() ? 1 : 0;
}

bool requestReleaseJson(std::string& payload, long& status, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "Falha ao iniciar HTTPS";
        return false;
    }
    MemoryDownload output;
    output.limit = 512u * 1024u;
    curl_slist* headers = nullptr;
    const std::string userAgent = "Switch-Vitrine/" + std::string(kAppVersion) +
                                  " (Nintendo Switch homebrew)";
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(curl, CURLOPT_URL, kLatestReleaseUrl);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToMemory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) {
        error = output.overflow ? "Resposta do GitHub excedeu o limite" :
                                 std::string("Falha ao consultar updates: ") + curl_easy_strerror(code);
        return false;
    }
    payload = std::move(output.data);
    return true;
}

std::string jsonString(json_t* object, const char* key) {
    json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : std::string();
}

bool validSha256(const std::string& digest) {
    if (digest.size() != 71 || digest.compare(0, 7, "sha256:") != 0) return false;
    return std::all_of(digest.begin() + 7, digest.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

std::string displayVersion(std::string version) {
    if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) version.erase(0, 1);
    return version;
}

std::string compactNotes(const std::string& notes) {
    std::string output;
    output.reserve(std::min<std::size_t>(notes.size(), 300));
    bool pendingSpace = false;
    for (const unsigned char character : notes) {
        if (std::isspace(character)) {
            pendingSpace = !output.empty();
            continue;
        }
        if (pendingSpace && output.size() < 280) output.push_back(' ');
        pendingSpace = false;
        if (output.size() >= 280) break;
        output.push_back(static_cast<char>(character));
    }
    if (notes.size() > output.size() && output.size() >= 277) output += "...";
    return output;
}

bool validNro(const std::string& path, std::uint64_t actualSize) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::uint8_t header[32]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) return false;
    if (std::memcmp(header + 0x10, "NRO0", 4) != 0) return false;
    const std::uint32_t declaredSize = static_cast<std::uint32_t>(header[0x18]) |
        (static_cast<std::uint32_t>(header[0x19]) << 8u) |
        (static_cast<std::uint32_t>(header[0x1a]) << 16u) |
        (static_cast<std::uint32_t>(header[0x1b]) << 24u);
    return declaredSize >= 0x80u && declaredSize <= actualSize;
}

struct FileFingerprint {
    std::uint64_t size = 0;
    std::string sha256;
};

std::string fileOperationError(const char* action, int errorNumber) {
#ifdef __SWITCH__
    const Result nativeResult = fsdevGetLastResult();
#endif
    std::ostringstream message;
    message << action;
    if (errorNumber != 0) message << ": " << std::strerror(errorNumber);
#ifdef __SWITCH__
    if (R_FAILED(nativeResult)) {
        message << " (FS 0x" << std::hex << std::uppercase << R_VALUE(nativeResult) << ')';
    }
#endif
    return message.str();
}

bool removeFileIfPresent(const std::string& path, std::string& error) {
    errno = 0;
    if (std::remove(path.c_str()) == 0) return true;
    const int savedErrno = errno;
    if (savedErrno == ENOENT) return true;
    error = fileOperationError("Falha ao remover arquivo antigo", savedErrno);
    return false;
}

bool fingerprintFile(const std::string& path, FileFingerprint& fingerprint, std::string& error) {
    errno = 0;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        error = fileOperationError("Falha ao abrir arquivo para verificacao", errno);
        return false;
    }

    std::vector<std::uint8_t> buffer(128u * 1024u);
    Sha256 hash;
    std::uint64_t total = 0;
    bool succeeded = true;
    while (true) {
        const std::size_t bytes = std::fread(buffer.data(), 1, buffer.size(), file);
        if (bytes > 0) {
            if (total + bytes > kMaximumNroSize) {
                error = "Arquivo excede o limite seguro de tamanho";
                succeeded = false;
                break;
            }
            hash.update(buffer.data(), bytes);
            total += bytes;
        }
        if (bytes < buffer.size()) {
            if (std::ferror(file)) {
                error = fileOperationError("Falha ao ler arquivo para verificacao", errno);
                succeeded = false;
            }
            break;
        }
    }
    if (std::fclose(file) != 0 && succeeded) {
        error = fileOperationError("Falha ao fechar arquivo verificado", errno);
        succeeded = false;
    }
    if (!succeeded) return false;

    fingerprint.size = total;
    fingerprint.sha256 = hash.finishHex();
    return true;
}

bool copyFileVerified(const std::string& sourcePath,
                      const std::string& destinationPath,
                      FileFingerprint& fingerprint,
                      std::string& error) {
    errno = 0;
    std::FILE* source = std::fopen(sourcePath.c_str(), "rb");
    if (!source) {
        error = fileOperationError("Falha ao abrir arquivo de origem", errno);
        return false;
    }
    errno = 0;
    std::FILE* destination = std::fopen(destinationPath.c_str(), "wb");
    if (!destination) {
        const int savedErrno = errno;
        std::fclose(source);
        error = fileOperationError("Falha ao abrir arquivo de destino", savedErrno);
        return false;
    }

    std::vector<std::uint8_t> buffer(128u * 1024u);
    Sha256 sourceHash;
    std::uint64_t total = 0;
    bool succeeded = true;
    while (true) {
        const std::size_t bytes = std::fread(buffer.data(), 1, buffer.size(), source);
        if (bytes > 0) {
            if (total + bytes > kMaximumNroSize) {
                error = "Arquivo de origem excede o limite seguro de tamanho";
                succeeded = false;
                break;
            }
            const std::size_t written = std::fwrite(buffer.data(), 1, bytes, destination);
            if (written != bytes) {
                error = fileOperationError("Falha ao gravar copia no SD", errno);
                succeeded = false;
                break;
            }
            sourceHash.update(buffer.data(), bytes);
            total += bytes;
        }
        if (bytes < buffer.size()) {
            if (std::ferror(source)) {
                error = fileOperationError("Falha ao ler arquivo de origem", errno);
                succeeded = false;
            }
            break;
        }
    }

    if (std::fclose(source) != 0 && succeeded) {
        error = fileOperationError("Falha ao fechar arquivo de origem", errno);
        succeeded = false;
    }
    if (std::fclose(destination) != 0 && succeeded) {
        error = fileOperationError("Falha ao concluir copia no SD", errno);
        succeeded = false;
    }
    if (!succeeded) {
        std::remove(destinationPath.c_str());
        return false;
    }

    fingerprint.size = total;
    fingerprint.sha256 = sourceHash.finishHex();
    FileFingerprint writtenFingerprint;
    if (!fingerprintFile(destinationPath, writtenFingerprint, error) ||
        writtenFingerprint.size != fingerprint.size ||
        writtenFingerprint.sha256 != fingerprint.sha256) {
        if (error.empty()) error = "A copia gravada no SD nao confere com a origem";
        std::remove(destinationPath.c_str());
        return false;
    }
    return true;
}

bool commitSdCard() {
#ifdef __SWITCH__
    return R_SUCCEEDED(fsdevCommitDevice("sdmc"));
#else
    return true;
#endif
}

}  // namespace

AppUpdater::AppUpdater(std::string executablePath)
    : executablePath_(normalizeExecutablePath(std::move(executablePath))) {}

bool AppUpdater::canInstall() const {
#ifdef __SWITCH__
    return !executablePath_.empty() && fileExists(executablePath_);
#else
    return false;
#endif
}

bool AppUpdater::check(UpdateInfo& info, std::string& error) const {
    info = {};
    std::string payload;
    long status = 0;
    if (!requestReleaseJson(payload, status, error)) return false;
    if (status == 404) return true;
    if (status < 200 || status >= 300) {
        error = "GitHub respondeu HTTP " + std::to_string(status);
        return false;
    }

    json_error_t jsonError{};
    json_t* root = json_loadb(payload.data(), payload.size(), 0, &jsonError);
    if (!root) {
        error = "Resposta de update invalida";
        return false;
    }
    const std::string tag = jsonString(root, "tag_name");
    const std::string version = displayVersion(tag);
    if (tag.empty() || compareSemanticVersions(kAppVersion, version) >= 0) {
        json_decref(root);
        return true;
    }

    json_t* assets = json_object_get(root, "assets");
    json_t* selected = nullptr;
    if (json_is_array(assets)) {
        const std::size_t count = json_array_size(assets);
        for (std::size_t index = 0; index < count; ++index) {
            json_t* asset = json_array_get(assets, index);
            if (json_is_object(asset) && jsonString(asset, "name") == kNroAssetName) {
                selected = asset;
                break;
            }
        }
    }
    if (!selected) {
        json_decref(root);
        error = "A release nao contem switch-vitrine.nro";
        return false;
    }

    const std::string url = jsonString(selected, "browser_download_url");
    const std::string digest = jsonString(selected, "digest");
    json_t* sizeValue = json_object_get(selected, "size");
    const json_int_t size = json_is_integer(sizeValue) ? json_integer_value(sizeValue) : 0;
    if (url.rfind(kReleaseDownloadPrefix, 0) != 0 || !validSha256(digest) ||
        size < static_cast<json_int_t>(kMinimumNroSize) ||
        size > static_cast<json_int_t>(kMaximumNroSize)) {
        json_decref(root);
        error = "A release nao possui metadados seguros para atualizar";
        return false;
    }

    info.available = true;
    info.version = version;
    info.downloadUrl = url;
    info.sha256 = digest.substr(7);
    std::transform(info.sha256.begin(), info.sha256.end(), info.sha256.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    info.notes = compactNotes(jsonString(root, "body"));
    info.size = static_cast<std::uint64_t>(size);
    json_decref(root);
    return true;
}

UpdateInstallResult AppUpdater::install(const UpdateInfo& info,
                                        std::atomic<std::uint64_t>& downloadedBytes,
                                        std::atomic<bool>& cancelRequested) const {
    UpdateInstallResult result;
    if (!canInstall()) {
        result.message = "Caminho do NRO nao e seguro para atualizacao";
        return result;
    }
    if (!info.available || info.downloadUrl.rfind(kReleaseDownloadPrefix, 0) != 0 ||
        !validSha256("sha256:" + info.sha256) || info.size < kMinimumNroSize ||
        info.size > kMaximumNroSize) {
        result.message = "Metadados da atualizacao foram rejeitados";
        return result;
    }

    const std::string temporaryPath = executablePath_ + ".new";
    const std::string backupPath = executablePath_ + ".bak";
    std::remove(temporaryPath.c_str());
    std::FILE* outputFile = std::fopen(temporaryPath.c_str(), "wb");
    if (!outputFile) {
        result.message = "Nao foi possivel criar o arquivo temporario no SD";
        return result;
    }

    downloadedBytes.store(0);
    FileDownload output;
    output.file = outputFile;
    output.expectedSize = info.size;
    output.progress = &downloadedBytes;
    output.cancel = &cancelRequested;
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(outputFile);
        std::remove(temporaryPath.c_str());
        result.message = "Falha ao iniciar o download HTTPS";
        return result;
    }
    const std::string userAgent = "Switch-Vitrine/" + std::string(kAppVersion) +
                                  " (Nintendo Switch homebrew)";
    curl_easy_setopt(curl, CURLOPT_URL, info.downloadUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(kMaximumNroSize));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, updateTransferProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &output);
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    const bool closeSucceeded = std::fclose(outputFile) == 0;

    if (cancelRequested.load()) {
        std::remove(temporaryPath.c_str());
        result.cancelled = true;
        result.message = "Atualizacao cancelada; a versao atual foi preservada";
        return result;
    }
    if (code != CURLE_OK || status < 200 || status >= 300 || output.overflow ||
        output.writeFailed || !closeSucceeded) {
        std::remove(temporaryPath.c_str());
        result.message = output.overflow ? "O download excedeu o tamanho esperado" :
                         output.writeFailed || !closeSucceeded ? "Falha ao gravar a atualizacao no SD" :
                         std::string("Falha no download: ") + curl_easy_strerror(code);
        return result;
    }
    if (output.written != info.size || output.hash.finishHex() != info.sha256) {
        std::remove(temporaryPath.c_str());
        result.message = "SHA-256 ou tamanho invalido; a versao atual foi preservada";
        return result;
    }
    if (!validNro(temporaryPath, output.written)) {
        std::remove(temporaryPath.c_str());
        result.message = "O arquivo baixado nao e um NRO valido";
        return result;
    }
    if (!commitSdCard()) {
        std::remove(temporaryPath.c_str());
        result.message = "O Switch nao confirmou a gravacao no cartao SD";
        return result;
    }

    std::string fileError;
    if (!removeFileIfPresent(backupPath, fileError)) {
        std::remove(temporaryPath.c_str());
        result.message = fileError;
        return result;
    }

    // Some homebrew launchers keep the active NRO in a state where Horizon FS
    // refuses to rename it. Sphaira's own updater avoids that operation: copy
    // the running NRO to a recovery file, then copy the validated update over it.
    FileFingerprint backupFingerprint;
    if (!copyFileVerified(executablePath_, backupPath, backupFingerprint, fileError) ||
        !validNro(backupPath, backupFingerprint.size) || !commitSdCard()) {
        std::remove(temporaryPath.c_str());
        if (fileError.empty()) fileError = "O Switch nao confirmou o backup no cartao SD";
        result.message = "Nao foi possivel criar um backup seguro. " + fileError;
        return result;
    }

    const auto restoreBackup = [&]() {
        FileFingerprint restoredFingerprint;
        std::string restoreError;
        const bool copied = copyFileVerified(backupPath, executablePath_, restoredFingerprint, restoreError);
        const bool matches = copied && restoredFingerprint.size == backupFingerprint.size &&
                             restoredFingerprint.sha256 == backupFingerprint.sha256;
        return matches && commitSdCard();
    };

    FileFingerprint installedFingerprint;
    fileError.clear();
    const bool installed = copyFileVerified(temporaryPath, executablePath_, installedFingerprint, fileError) &&
                           installedFingerprint.size == info.size &&
                           installedFingerprint.sha256 == info.sha256 &&
                           validNro(executablePath_, installedFingerprint.size);
    if (!installed) {
        const bool restored = restoreBackup();
        std::remove(temporaryPath.c_str());
        result.message = restored ? "Falha ao trocar o NRO; a versao anterior foi restaurada" :
                                    "Falha ao trocar o NRO; restaure o arquivo .bak pelo Sphaira";
        if (!fileError.empty()) result.message += ". " + fileError;
        return result;
    }
    if (!commitSdCard()) {
        const bool restored = restoreBackup();
        std::remove(temporaryPath.c_str());
        result.message = restored ? "Falha ao confirmar o update; a versao anterior foi restaurada" :
                                    "Falha ao confirmar o update; restaure o arquivo .bak pelo Sphaira";
        return result;
    }

    std::remove(temporaryPath.c_str());
    commitSdCard();

    result.success = true;
    result.message = "Atualizacao instalada. Volte ao Sphaira e abra o Vitrine novamente.";
    return result;
}

}  // namespace vitrine
