#include "platform/sdl/NativeStorage.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace rocket {
namespace {

std::string boolText(bool value) { return value ? "1" : "0"; }

bool parseBool(std::string_view value, bool fallback)
{
    if (value == "1" || value == "true") return true;
    if (value == "0" || value == "false") return false;
    return fallback;
}

double parseDouble(std::string_view value, double fallback)
{
    try { return std::stod(std::string(value)); } catch (...) { return fallback; }
}

std::string promptName(ControllerPromptFamily family)
{
    switch (family) {
    case ControllerPromptFamily::Generic: return "generic";
    case ControllerPromptFamily::Xbox: return "xbox";
    case ControllerPromptFamily::PlayStation: return "playstation";
    case ControllerPromptFamily::SteamDeck: return "steamdeck";
    case ControllerPromptFamily::Auto:
    default: return "auto";
    }
}

ControllerPromptFamily parsePrompt(std::string_view value)
{
    if (value == "generic") return ControllerPromptFamily::Generic;
    if (value == "xbox") return ControllerPromptFamily::Xbox;
    if (value == "playstation") return ControllerPromptFamily::PlayStation;
    if (value == "steamdeck") return ControllerPromptFamily::SteamDeck;
    return ControllerPromptFamily::Auto;
}

std::string sanitizeLine(std::string value)
{
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    return value;
}

} // namespace

AtomicTextFile::AtomicTextFile(std::filesystem::path path) : path_(std::move(path)) {}

std::string AtomicTextFile::load()
{
    lastError_.clear();
    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        if (std::filesystem::exists(path_)) lastError_ = "Unable to read " + path_.string();
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool AtomicTextFile::store(std::string_view data)
{
    lastError_.clear();
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) {
        lastError_ = "Unable to create preference directory: " + error.message();
        return false;
    }

    const std::filesystem::path temporary = path_.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            lastError_ = "Unable to open temporary file for " + path_.string();
            return false;
        }
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) {
            lastError_ = "Unable to finish writing temporary file for " + path_.string();
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        lastError_ = "Unable to replace " + path_.string() + " (Windows error " + std::to_string(GetLastError()) + ")";
        std::filesystem::remove(temporary, error);
        return false;
    }
#else
    std::filesystem::rename(temporary, path_, error);
    if (error) {
        lastError_ = "Unable to replace " + path_.string() + ": " + error.message();
        std::filesystem::remove(temporary, error);
        return false;
    }
#endif
    return true;
}

bool AtomicTextFile::clear()
{
    lastError_.clear();
    std::error_code error;
    std::filesystem::remove(path_, error);
    if (error) {
        lastError_ = "Unable to remove " + path_.string() + ": " + error.message();
        return false;
    }
    return true;
}

const std::string& AtomicTextFile::lastError() const { return lastError_; }
const std::filesystem::path& AtomicTextFile::path() const { return path_; }

NativeSaveStore::NativeSaveStore(const std::filesystem::path& directory) : file_(directory / "save_v1.txt") {}
std::string NativeSaveStore::load() { return file_.load(); }
bool NativeSaveStore::storeAtomic(std::string_view data) { return file_.store(data); }
bool NativeSaveStore::clear() { return file_.clear(); }
std::string NativeSaveStore::lastError() const { return file_.lastError(); }
const std::filesystem::path& NativeSaveStore::path() const { return file_.path(); }

NativePreferenceStore::NativePreferenceStore(const std::filesystem::path& directory) : file_(directory / "preferences_v1.txt") {}

void NativePreferenceStore::ensureLoaded()
{
    if (loaded_) return;
    loaded_ = true;
    const std::string data = file_.load();
    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string_view key(line.data(), separator);
        const std::string_view value(line.data() + separator + 1, line.size() - separator - 1);
        if (key == "controller.promptFamily") cached_.controller.promptFamily = parsePrompt(value);
        else if (key == "controller.stickDeadzone") cached_.controller.stickDeadzone = std::clamp(parseDouble(value, 0.20), 0.10, 0.35);
        else if (key == "controller.invertFlightY") cached_.controller.invertFlightY = parseBool(value, false);
        else if (key == "controller.swapConfirmCancel") cached_.controller.swapConfirmCancel = parseBool(value, false);
        else if (key == "controller.vibrationEnabled") cached_.controller.vibrationEnabled = parseBool(value, true);
        else if (key == "render.resolution") cached_.resolutionPreset = std::string(value);
        else if (key == "game.speed") cached_.gameSpeed = std::clamp(parseDouble(value, 1.0), 0.25, 8.0);
        else if (key == "debug.tools") cached_.debugToolsEnabled = parseBool(value, false);
        else if (key == "debug.performanceStats") cached_.performanceStatsEnabled = parseBool(value, false);
        else if (key == "accessibility.helpDisabled") cached_.helpDisabled = parseBool(value, false);
        else if (key == "render.cameraShakeDisabled") cached_.cameraShakeDisabled = parseBool(value, false);
        else if (key == "window.fullscreen") cached_.fullscreen = parseBool(value, false);
        else if (key == "help.dismissed" && !value.empty()) cached_.dismissedHelpTopics.emplace_back(value);
    }
}

AppPreferences NativePreferenceStore::load()
{
    ensureLoaded();
    return cached_;
}

bool NativePreferenceStore::store(const AppPreferences& preferences)
{
    AppPreferences normalized = preferences;
    normalized.controller.stickDeadzone = std::clamp(normalized.controller.stickDeadzone, 0.10, 0.35);
    normalized.gameSpeed = std::clamp(normalized.gameSpeed, 0.25, 8.0);
    std::ostringstream stream;
    stream << "controller.promptFamily=" << promptName(normalized.controller.promptFamily) << '\n'
           << "controller.stickDeadzone=" << normalized.controller.stickDeadzone << '\n'
           << "controller.invertFlightY=" << boolText(normalized.controller.invertFlightY) << '\n'
           << "controller.swapConfirmCancel=" << boolText(normalized.controller.swapConfirmCancel) << '\n'
           << "controller.vibrationEnabled=" << boolText(normalized.controller.vibrationEnabled) << '\n'
           << "render.resolution=" << sanitizeLine(normalized.resolutionPreset) << '\n'
           << "game.speed=" << normalized.gameSpeed << '\n'
           << "debug.tools=" << boolText(normalized.debugToolsEnabled) << '\n'
           << "debug.performanceStats=" << boolText(normalized.performanceStatsEnabled) << '\n'
           << "accessibility.helpDisabled=" << boolText(normalized.helpDisabled) << '\n'
           << "render.cameraShakeDisabled=" << boolText(normalized.cameraShakeDisabled) << '\n'
           << "window.fullscreen=" << boolText(normalized.fullscreen) << '\n';
    for (const std::string& topic : normalized.dismissedHelpTopics) stream << "help.dismissed=" << sanitizeLine(topic) << '\n';
    if (!file_.store(stream.str())) return false;
    cached_ = std::move(normalized);
    loaded_ = true;
    ++revision_;
    return true;
}

std::uint64_t NativePreferenceStore::revision() const { return revision_; }
std::string NativePreferenceStore::lastError() const { return file_.lastError(); }

} // namespace rocket
