#include "exe_patch.h"

#include "util/bit.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"

namespace SenLib::Sen1 {
extern float RealTimeStep;

void PatchFieldCamera(PatchExecData& execData, bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;

    const uint32_t addrGetSettings =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x7BDEF0, 0x7BF7C0));
    const uint32_t addrGetDisableCamAutoCenter =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x7BD220, 0x7BEAF0));

    // disable horizontal camera auto-centering
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x53B9B0, 0x53D0D0);

        // Inject pos:
        // ->#  lea     ecx, [ebp-18h]  8D 4D E8
        //   #  fld     ds:flt_B39D4C   D9 05 ?? ?? ?? ??       -1.0f
        //      fstp    [ebp-10h]       D9 5D F0
        const auto inject = InjectJumpIntoCode<9, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper1Byte j0, j1;

        const uint32_t offGetSettings = addrGetSettings - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetSettings));              // call getSettings
        WriteInstruction16(codespace, 0x8bc8);                                              // mov  ecx, eax
        const uint32_t offGetDisableCamAutoCenter = addrGetDisableCamAutoCenter - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetDisableCamAutoCenter));  // call getDisableCamAutoCenter
        WriteInstruction16(codespace, 0x84c0);                                              // test al, al

        j0.WriteJump(codespace, JC::JZ);
        WriteInstruction16(codespace, 0xd9ee); // fldz
        j1.WriteJump(codespace, JC::JMP);
        j0.SetTarget(codespace);
        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data() + 3,
                    inject.OverwrittenInstructions.size() - 3);
        codespace += (inject.OverwrittenInstructions.size() - 3);
        j1.SetTarget(codespace);

        std::memcpy(codespace, inject.OverwrittenInstructions.data(), 3); // lea ecx, [ebp-18h]
        codespace += 3;

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use unscaled timestep
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5A6368, 0x5A74D8);

        // Inject pos:
        // ->#  fld     [ebp+8h]                        D9 45 08
        //   #  lea     ecx, [esi+8]                    8D 4E 08
        //      fstp    dword ptr [esp+28h+var_24+4]    D9 5C 24 08
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

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
