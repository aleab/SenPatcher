#include "exe_patch.h"

#include <cassert>

#include "util/bit.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen2 {
extern float RealTimeStep;

void PatchFieldCamera(PatchExecData& execData, bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;

    const uint32_t addrGetSettings =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x6CF5B0, 0x6D0520));
    const uint32_t addrGetDisableCamAutoCenter =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x6CF4D0, 0x6D0440));

    // disable horizontal camera auto-centering
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4D8478, 0x4D86B8);

        // Inject pos:
        // ->#  mov     dword ptr [esp+0A8h], 0BF800000h    C7 84 24 A8 00 00 00 00 00 80 BF    -1.0f
        const auto inject = InjectJumpIntoCode<11, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper4Byte j0, jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);

        const uint32_t offGetSettings = addrGetSettings - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetSettings));              // call getSettings
        WriteInstruction16(codespace, 0x8bc8);                                              // mov  ecx, eax
        const uint32_t offGetDisableCamAutoCenter = addrGetDisableCamAutoCenter - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetDisableCamAutoCenter));  // call getDisableCamAutoCenter
        WriteInstruction16(codespace, 0x84c0);                                              // test al, al

        j0.WriteJump(codespace, JC::JZ);
        WriteInstruction56(codespace, 0xc78424a8000000);    // mov dword ptr [esp+0A8h], 0
        WriteInstruction32(codespace, 0);
        jmpBack.WriteJump(codespace, JC::JMP);

        j0.SetTarget(codespace);
        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += (inject.OverwrittenInstructions.size());
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use unscaled timestep
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5590F1, 0x558D81);

        // Inject pos:
        //      xorps   xmm1, xmm1          0F 57 C9
        // ->#  movss   xmm0, [ebp+8h]      F3 0F 10 45 08
        //      sub     esp, 0Ch            83 EC 0C
        //      lea     ecx, [esi+8h]       8D 4E 08
        //      movss   [esp+8h], xmm0      F3 0F 11 44 24 08
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        WriteInstruction32(codespace, 0xf30f1005);      // movss xmm0,dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
}
}
