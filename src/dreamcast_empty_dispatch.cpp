#include <cstdint>

// The KOS executable deliberately links no cart AOT corpus. The interpreter
// still enters here for all cart PCs; this sentinel keeps the production
// dispatch ABI complete while making native coverage unambiguously zero.
struct DispatchEntry {
    std::uint32_t addr;
    std::uint8_t thumb;
    std::uint8_t resume;
    void (*fn)(void);
};

extern "C" const DispatchEntry kDispatchTable[] = {
    {0u, 0u, 0u, nullptr},
};
extern "C" const unsigned kDispatchTableLen = 0;
