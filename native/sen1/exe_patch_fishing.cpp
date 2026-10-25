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

    // use unscaled timestep for fishing
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x60DA3E, 0x60F05E);

        // Inject pos:
        // ->#  fsub    [ebp+8h]        D8 65 08
        //   #  fcom    st(1)           D8 D1
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        Emit_FSUB_DwordPtr(codespace, addrRealTimeStep);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data() + 3,
                    inject.OverwrittenInstructions.size() - 3);
        codespace += inject.OverwrittenInstructions.size() - 3;

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x60DB42, 0x60F162);

        // Inject pos:
        // ->#  fld     [ebp+8h]        D9 45 08
        //   #  fld     st              D9 C0
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        WriteInstruction16(codespace, 0x8d05);  // lea eax,dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd900);  // fld dword ptr[eax]

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data() + 3,
                    inject.OverwrittenInstructions.size() - 3);
        codespace += inject.OverwrittenInstructions.size() - 3;

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x60DCBA, 0x60F2DA);

        // Inject pos:
        // ->#  fsub    [ebp+8h]        D8 65 08
        //   #  fstp    [ebp-4h]        D9 5D FC
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        Emit_FSUB_DwordPtr(codespace, addrRealTimeStep);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data() + 3,
                    inject.OverwrittenInstructions.size() - 3);
        codespace += inject.OverwrittenInstructions.size() - 3;

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x60DD19, 0x60F339);

        // Inject pos:
        // ->#  fadd    [ebp+8h]                D8 45 08
        //   #  fld     dword ptr [esi+218h]    D9 86 18 02 00 00
        const auto inject = InjectJumpIntoCode<9, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        Emit_FADD_DwordPtr(codespace, addrRealTimeStep);

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
