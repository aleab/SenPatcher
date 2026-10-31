#include "exe_patch.h"

#include <algorithm>
#include <bit>

#include "util/bit.h"
#include "x86/emitter.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen2 {
void PatchHorseRiding(PatchExecData& execData, float maxCameraDistance) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;

    // change max camera distance
    maxCameraDistance = std::clamp(fabsf(maxCameraDistance), 5.0f, 15.0f);
    if (maxCameraDistance != 7.0f) {
        // Inject pos:
        //      mov     dword ptr [esi+64Ch], 0             C7 86 4C 06 00 00 00 00 00 00
        // ->#  mov     dword ptr [esi+654h], 40E00000h     C7 86 54 06 00 00 00 00 E0 40       7.0f
        //      mov     dword ptr [esi+658h], 3F402037h     C7 86 58 06 00 00 37 20 40 3F
        char* address = GetCodeAddressJpEn(version, textRegion, 0x555AE9, 0x555779) + 6;

        uint32_t uMaxCameraDistance = std::bit_cast<uint32_t>(maxCameraDistance);
        PageUnprotect page(logger, address, 4);
        WriteInstruction32(address, BSWAP32(uMaxCameraDistance));
    }
}
}
