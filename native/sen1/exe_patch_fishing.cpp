#include "exe_patch.h"

#include <cassert>

#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen1 {
extern float RealTimeStep;

void PatchFishing(PatchExecData& execData, bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;

    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x60F484, 0x610A18);

        // Inject pos:
        // ->#  fld     [ebp+8h]        D9 45 08
        //   #  mov     ecx, esi        8B CE
        //      fstp    [esp]           D9 1C 24
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        WriteInstruction16(codespace, 0x8d05); // lea eax,dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd900); // fld dword ptr[eax]

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data() + 3,
                    inject.OverwrittenInstructions.size() - 3);
        codespace += inject.OverwrittenInstructions.size() - 3;

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
}
}
