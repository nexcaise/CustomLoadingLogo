#include <pl/Mod.hpp>

#include "nmdk/NMDK.hpp"

#include "TitleLocator.hpp"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace customloadinglogo {

namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";

// Return value of mce::ImageUtils::loadImageFromMemory (24 bytes, returned through x8).
struct ImageResult {
    char    head[16];
    uint8_t ok;
    char    tail[7];
};

using LoadImageFromMemory = ImageResult (*)(void* image, const void* data, unsigned int size,
                                            bool flag);

LoadImageFromMemory g_originalLoad = nullptr;
std::filesystem::path g_logoPath;  // <mod root>/config/logo.png

// Keeps the replacement PNG alive after the detour returns, in case the game's image object
// still references the buffer. Replaced on the next title load.
std::mutex                 g_pngMutex;
std::vector<unsigned char> g_pngData;

[[nodiscard]] bool readLogo(std::vector<unsigned char>& out) noexcept {
    std::FILE* file = std::fopen(g_logoPath.c_str(), "rb");
    if (!file) return false;

    bool ok = false;
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long end = std::ftell(file);
        if (end > 8 && static_cast<unsigned long long>(end) <=
                           static_cast<unsigned long long>(std::numeric_limits<int>::max())) {
            std::rewind(file);
            out.resize(static_cast<std::size_t>(end));
            ok = std::fread(out.data(), 1, out.size(), file) == out.size();
        }
    }
    std::fclose(file);

    static constexpr unsigned char kPngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return ok && std::equal(std::begin(kPngMagic), std::end(kPngMagic), out.begin());
}

ImageResult hookedLoadImage(void* image, const void* data, unsigned int size, bool flag) {
    // Only the original title PNG is replaced; every other image goes straight through.
    if (size == locator::kOriginalTitlePngSize) {
        std::lock_guard lock(g_pngMutex);
        std::vector<unsigned char> custom;
        if (readLogo(custom)) {
            g_pngData = std::move(custom);
            return g_originalLoad(image, g_pngData.data(),
                                  static_cast<unsigned int>(g_pngData.size()), flag);
        }
    }
    return g_originalLoad(image, data, size, flag);
}

[[nodiscard]] std::uintptr_t findLoadImageTarget() {
    const ::nmdk::Library lib{std::string(kMinecraftLibrary)};

    std::vector<locator::Range> readable;
    std::vector<locator::Range> text;
    for (const auto& r : lib.regions()) {
        if (!r.readable()) continue;
        readable.push_back({r.begin, r.end});
        if (r.executable()) text.push_back({r.begin, r.end});
    }
    return locator::locateLoadImageFromMemory(readable, text);
}

} // namespace

class CustomLoadingLogoMod {
  public:
    static CustomLoadingLogoMod& getInstance() {
        static CustomLoadingLogoMod instance;
        return instance;
    }

    CustomLoadingLogoMod() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load() {
        // (mod root)/config/logo.png - create config/ so the user knows where to drop the file.
        g_logoPath = mSelf.getModDir() / "config" / "logo.png";
        std::error_code ec;
        std::filesystem::create_directories(g_logoPath.parent_path(), ec);
        return true;
    }

    bool enable() {
        auto& log = mSelf.getLogger();

        const std::uintptr_t target = findLoadImageTarget();
        if (!target) {
            log.error("loadImageFromMemory not found in {}", kMinecraftLibrary);
            return false;
        }

        mHook = ::nmdk::ScopedHook(::nmdk::HookRegistry::install(
            reinterpret_cast<void*>(target), reinterpret_cast<void*>(&hookedLoadImage),
            ::nmdk::HookPriority::Normal, reinterpret_cast<void**>(&g_originalLoad)));
        if (!mHook) {
            log.error("failed to hook loadImageFromMemory at {:#x}", target);
            return false;
        }

        log.info("title logo hook installed, logo path: {}", g_logoPath.string());
        return true;
    }

    bool disable() {
        mHook.reset();
        return true;
    }

    bool unload() { return true; }

  private:
    ll::mod::NativeMod& mSelf;
    ::nmdk::ScopedHook  mHook;
};

} // namespace customloadinglogo

PL_REGISTER_MOD(customloadinglogo::CustomLoadingLogoMod, customloadinglogo::CustomLoadingLogoMod::getInstance());
