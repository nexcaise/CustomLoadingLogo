#include <android/log.h>
#include <android/input.h>
#include "Gloss.h"
#include <cstddef>
#include <cstdint>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>

#include <libhat.hpp>
#include <libhat/scanner.hpp>
#include "mcTitlepng.h"

#define LOG_TAG "mcTitlePatch"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static void* resolveBL(void* insnAddr) {
    uint32_t insn = *reinterpret_cast<uint32_t*>(insnAddr);
    if ((insn & 0xFC000000u) != 0x94000000u) return nullptr;       // not a BL
    int32_t imm26 = static_cast<int32_t>((insn & 0x03FFFFFFu) << 6) >> 6;
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(insnAddr) + (static_cast<int64_t>(imm26) << 2));
}
static void* traceB(void* p, int maxHops = 4) {
    for (int i = 0; i < maxHops; ++i) {
        uint32_t insn = *reinterpret_cast<uint32_t*>(p);
        if ((insn & 0xFC000000u) != 0x14000000u) break;
        int32_t imm26 = static_cast<int32_t>((insn & 0x03FFFFFFu) << 6) >> 6;
        p = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p) + (static_cast<int64_t>(imm26) << 2));
    }
    return p;
}

static uint32_t* findStrXref(uintptr_t start, size_t size, uintptr_t str) {
    for (uintptr_t pc = start; pc + 16 <= start + size; pc += 4) {
        uint32_t i = *reinterpret_cast<uint32_t*>(pc);
        if ((i & 0x9F000000u) != 0x90000000u) continue;

        uint64_t imm21 = (uint64_t((i >> 5) & 0x7FFFFu) << 2) | ((i >> 29) & 3u);
        int64_t  imm   = static_cast<int64_t>(imm21 << 43) >> 43;
        uintptr_t page = (pc & ~uintptr_t(0xFFF)) + static_cast<uintptr_t>(imm << 12);

        if (str < page || str >= page + 0x1000) continue;
        for (int k = 1; k <= 3; ++k) {
            uint32_t j = *reinterpret_cast<uint32_t*>(pc + 4 * k);
            if ((j & 0xFFC00000u) == 0x91000000u &&
                ((j >> 5) & 31u) == (i & 31u) &&
                page + ((j >> 10) & 0xFFFu) == str)
                return reinterpret_cast<uint32_t*>(pc);
        }
    }
    return nullptr;
}

static void* findLoaderBL(uint32_t* xref, uint32_t lo) {
    for (int off = -100; off <= 100; ++off) {
        uint32_t i = xref[off];
        if ((i & 0xFFE0001Fu) != 0x52800002u || ((i >> 5) & 0xFFFFu) != lo) continue;
        for (int k = 1; k <= 16; ++k)
            if ((xref[off + k] & 0xFC000000u) == 0x94000000u) return &xref[off + k];
    }
    return nullptr;
}

struct idkStruct {
    char    shit1[16];
    uint8_t ok;
    char    shit2[7];
};

void hook_loadingScreen(){
    size_t sizero = 0, sizetext = 0;

    auto* roBase   = reinterpret_cast<std::byte*>(GlossGetLibSection("libminecraftpe.so", ".rodata", &sizero));
    auto* textBase = reinterpret_cast<std::byte*>(GlossGetLibSection("libminecraftpe.so", ".text",   &sizetext));
    if (!roBase || !textBase) { LOGI("section not found"); return; }

    std::span<std::byte> ro{roBase, sizero};
    std::span<std::byte> text{textBase, sizetext};

    auto* str = hat::find_pattern(ro, hat::object_to_signature("Loading Screen")).get();
    if (!str) { LOGI("string fucked up"); return; }
    LOGI("str = %p", str);

    auto* xref = findStrXref((uintptr_t)text.data(), text.size(), (uintptr_t)str);
    if (!xref) { LOGI("xref fucked up"); return; }

    void* bl = findLoaderBL(xref, 86796 & 0xFFFF); // the low half of 86796
    if (!bl) { LOGI("BL fucked up"); return; }

    using Fn_t = idkStruct (*)(void* mce_image_mungkin, const void* data, unsigned int size, bool flag);
    static Fn_t orig = nullptr;

    Fn_t detour = [](void* img, const void* data, unsigned int size, bool flag){
        LOGI("size : %u", size);
        if (size == 86796)
            return orig(img, mctitle_png, mctitle_png_len, flag);
        return orig(img, data, size, flag);
    };

    void* targ = traceB(resolveBL(bl));
    if (!targ) { LOGI("not a BL"); return; }
    LOGI("xref=%p bl=%p targ=%p", xref, bl, targ);

    GlossHook(targ, (void*)detour, (void**)&orig); // the target is mce::ImageUtils::loadImageFromMemory callsite
}


void* m_thread(void*) {
    GlossInit(true);
    hook_loadingScreen();
    return nullptr;
}

__attribute__((constructor))
static void hehehaha() {
    pthread_t t;
    pthread_create(&t, nullptr, m_thread, nullptr);
}
