#pragma once

// Signature-less locator for mce::ImageUtils::loadImageFromMemory as called by the loading
// screen. Ported from mcTitlePatch: instead of a hard-coded offset, it follows the
// "Loading Screen" string -> ADRP/ADD xref -> `mov w2, #<low16 of title png size>` -> BL, so it
// keeps working across game updates. Pure functions over memory ranges (no NMDK/Gloss needed).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace customloadinglogo::locator {

// Byte length of the PNG embedded in libminecraftpe.so for the title logo.
inline constexpr std::uint32_t kOriginalTitlePngSize = 86796u;

// "Loading Screen" including the trailing NUL (same needle mcTitlePatch uses).
inline constexpr std::string_view kAnchorString{"Loading Screen", 15};

struct Range {
    std::uintptr_t begin{};
    std::uintptr_t end{};

    [[nodiscard]] bool contains(std::uintptr_t p, std::size_t n) const noexcept {
        return p >= begin && p + n <= end && p + n >= p;
    }
};

[[nodiscard]] inline std::uintptr_t findBytes(Range r, std::string_view needle,
                                              std::uintptr_t from = 0) noexcept {
    if (needle.empty() || r.end <= r.begin) return 0;
    std::uintptr_t p = from > r.begin ? from : r.begin;
    const auto first = static_cast<int>(static_cast<unsigned char>(needle[0]));
    while (p + needle.size() <= r.end) {
        const void* hit = std::memchr(reinterpret_cast<const void*>(p), first,
                                      static_cast<std::size_t>(r.end - p - needle.size() + 1));
        if (!hit) return 0;
        const auto addr = reinterpret_cast<std::uintptr_t>(hit);
        if (std::memcmp(hit, needle.data(), needle.size()) == 0) return addr;
        p = addr + 1;
    }
    return 0;
}

[[nodiscard]] inline std::uint32_t readInsn(std::uintptr_t addr) noexcept {
    std::uint32_t v;
    std::memcpy(&v, reinterpret_cast<const void*>(addr), sizeof(v));
    return v;
}

// BL imm26 -> destination, or 0 if `insnAddr` is not a BL.
[[nodiscard]] inline std::uintptr_t resolveBL(std::uintptr_t insnAddr) noexcept {
    const std::uint32_t insn = readInsn(insnAddr);
    if ((insn & 0xFC000000u) != 0x94000000u) return 0;
    const std::int32_t imm26 = static_cast<std::int32_t>((insn & 0x03FFFFFFu) << 6) >> 6;
    return insnAddr + static_cast<std::uintptr_t>(static_cast<std::int64_t>(imm26) * 4);
}

// Follow up to `maxHops` unconditional B thunks.
[[nodiscard]] inline std::uintptr_t traceB(std::uintptr_t p, int maxHops = 4) noexcept {
    for (int i = 0; i < maxHops; ++i) {
        const std::uint32_t insn = readInsn(p);
        if ((insn & 0xFC000000u) != 0x14000000u) break;
        const std::int32_t imm26 = static_cast<std::int32_t>((insn & 0x03FFFFFFu) << 6) >> 6;
        p += static_cast<std::uintptr_t>(static_cast<std::int64_t>(imm26) * 4);
    }
    return p;
}

// First `adrp xN, page; add xN, xN, #lo` pair (add within 3 insns) that materialises `str`.
[[nodiscard]] inline std::uintptr_t findStrXref(Range text, std::uintptr_t str) noexcept {
    for (std::uintptr_t pc = text.begin; pc + 16 <= text.end; pc += 4) {
        const std::uint32_t i = readInsn(pc);
        if ((i & 0x9F000000u) != 0x90000000u) continue;  // not ADRP

        const std::uint64_t imm21 = (std::uint64_t((i >> 5) & 0x7FFFFu) << 2) | ((i >> 29) & 3u);
        const std::int64_t imm = static_cast<std::int64_t>(imm21 << 43) >> 43;
        const std::uintptr_t page =
            (pc & ~std::uintptr_t(0xFFF)) + static_cast<std::uintptr_t>(imm << 12);

        if (str < page || str >= page + 0x1000) continue;
        for (int k = 1; k <= 3; ++k) {
            const std::uint32_t j = readInsn(pc + 4 * k);
            if ((j & 0xFFC00000u) == 0x91000000u && ((j >> 5) & 31u) == (i & 31u) &&
                page + ((j >> 10) & 0xFFFu) == str)
                return pc;
        }
    }
    return 0;
}

// Around the xref, find `mov w2, #lo` and return the first BL that follows it (<= 16 insns).
// All reads stay inside `text`.
[[nodiscard]] inline std::uintptr_t findLoaderBL(Range text, std::uintptr_t xref,
                                                 std::uint32_t lo) noexcept {
    for (int off = -100; off <= 100; ++off) {
        const std::uintptr_t at = xref + static_cast<std::intptr_t>(off) * 4;
        if (!text.contains(at, 4)) continue;
        const std::uint32_t i = readInsn(at);
        if ((i & 0xFFE0001Fu) != 0x52800002u || ((i >> 5) & 0xFFFFu) != lo) continue;
        for (int k = 1; k <= 16; ++k) {
            const std::uintptr_t cand = at + static_cast<std::uintptr_t>(k) * 4;
            if (!text.contains(cand, 4)) break;
            if ((readInsn(cand) & 0xFC000000u) == 0x94000000u) return cand;
        }
    }
    return 0;
}

// Returns the address of loadImageFromMemory, or 0 when the pattern chain does not resolve.
// `readable` = every readable range of the library (string may live in .rodata), `text` =
// executable ranges.
[[nodiscard]] inline std::uintptr_t locateLoadImageFromMemory(
    const std::vector<Range>& readable, const std::vector<Range>& text) noexcept {
    const std::uint32_t lo = kOriginalTitlePngSize & 0xFFFFu;

    for (const Range& ro : readable) {
        for (std::uintptr_t str = findBytes(ro, kAnchorString); str != 0;
             str = findBytes(ro, kAnchorString, str + 1)) {
            for (const Range& tx : text) {
                const std::uintptr_t xref = findStrXref(tx, str);
                if (!xref) continue;
                const std::uintptr_t bl = findLoaderBL(tx, xref, lo);
                if (!bl) continue;
                const std::uintptr_t dest = resolveBL(bl);
                if (!dest) continue;
                return traceB(dest);
            }
        }
    }
    return 0;
}

}  // namespace customloadinglogo::locator
