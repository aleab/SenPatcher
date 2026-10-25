#include "exe_patch.h"

#include <cassert>

#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen1 {
extern float RealTimeStep;

void PatchFieldCamera(PatchExecData& execData, bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;

    // use unscaled timestep
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5A6368, 0x5A74D8);

        // Inject pos:
        // ->#  fld     [ebp+8h]                        D9 45 08
        //   #  lea     ecx, [esi+8]                    8D 4E 08
        //      fstp    dword ptr [esp+28h+var_24+4]    D9 5C 24 08
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        WriteInstruction16(codespace, 0x8d0d); // lea ecx,dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd901); // fld dword ptr[ecx]

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
