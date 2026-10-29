#include "exe_patch.h"

#include <cassert>

#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen2 {
extern float RealTimeStep;

void PatchFishing(PatchExecData& execData, bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;

    // use unscaled timestep
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5A7E01, 0x5A7E26);

        // Inject pos:
        // ->#  movss   [esp], xmm1     F3 0F 11 0C 24
        //      call    ????            E8 ?? ?? ?? ??
        //      mov     esi, eax        8B F0
        //      test    esi, esi        85 F6
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        WriteInstruction16(codespace, 0x8d05);      // lea eax,dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd900);      // fld dword ptr[eax]
        WriteInstruction24(codespace, 0xd91c24);    // fstp dword ptr[esp]

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
}
}
