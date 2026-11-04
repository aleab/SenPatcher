#include "exe_patch.h"

#include <bit>

#include "util/bit.h"
#include "util/memwrite.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"

namespace SenLib::Sen2 {
void PatchBikeRiding(PatchExecData& execData, int disableCameraAutoCentering) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const uint32_t addrGetSettings =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x6CF5B0, 0x6D0520));
    const uint32_t addrGetDisableCamAutoCenter =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x6CF4D0, 0x6D0440));

    // use Left Stick Y- (S on keyboard) to brake/reverse
    // BUG: can't steer while reversing this way
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5469DD, 0x5465FD);

        const char* const addrDeadzone = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(-0.01f));

        // Inject pos:
        // ->#  mov     ecx, [esi+684h]                 8B 8E 84 06 00 00
        //      push    1                               6A 01
        //      push    0                               6A 00
        //      push    4                               6A 04
        //      movss   dword ptr [esp+24h], xmm1       F3 0F 11 4C 24 24
        //      movss   dword ptr [esp+48h], xmm0       F3 0F 11 44 24 48
        //      call    isButtonPressed                 E8 ?? ?? ?? ??
        //      test    al, al                          84 C0
        //      jnz     short loc_54669A                75 7C
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper4Byte jReverse;
        jReverse.SetTarget((addressInjectPos + 33) + *(addressInjectPos + 32)); // loc_54669A

        WriteInstruction24(codespace, 0x0f2f05);    // comiss xmm0, dword ptr [addrDeadzone]
        std::memcpy(codespace, &addrDeadzone, 4);
        codespace += 4;
        jReverse.WriteJump(codespace, JC::JBE);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use Left Stick Y+ (W on keyboard) to accelerate
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5469FE, 0x54661E);

        const char* const addrDeadzone = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(0.01f));

        // Inject pos:
        // ->#  mov     ecx, [esi+684h]         8B 8E 84 06 00 00
        //      push    1                       6A 01
        //      push    0                       6A 00
        //      push    5                       6A 05
        //      call    isButtonPressed         E8 ?? ?? ?? ??
        //      test    al, al                  84 C0
        //      jnz     short loc_54663F        75 0C
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper4Byte jAccelerate;
        jAccelerate.SetTarget((addressInjectPos + 21) + *(addressInjectPos + 20)); // loc_54663F

        WriteInstruction24(codespace, 0x0f2f05);    // comiss xmm0, dword ptr [addrDeadzone]
        std::memcpy(codespace, &addrDeadzone, 4);
        codespace += 4;
        jAccelerate.WriteJump(codespace, JC::JAE);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // disable camera auto-centering
    if (disableCameraAutoCentering != 0) {
        // Inject pos:
        //      movss   xmm0, dword ptr [esp+0Ch]       F3 0F 10 44 24 0C
        // ->#  comiss  xmm0, ds:dword_8EBB7C           0F 2F 05 ?? ?? ?? ??
        //      jbe     short loc_54770F                76 71
        //      movss   xmm1, dword ptr [edi+0D10h]     F3 0F 10 8F ?? 0D 00 00
        //      addss   xmm1, ds:dword_8EC8DC           F3 0F 58 0D ?? ?? ?? ??
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x547A75, 0x547695);

        if (disableCameraAutoCentering == 1) {
            PageUnprotect page(logger, addressInjectPos + 7, 1);
            *(addressInjectPos + 7) = static_cast<char>(0xeb); // jbe -> jmp
        } else {
            char* codespace = execData.Codespace;
            const auto inject = InjectJumpIntoCode<7, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

            BranchHelper4Byte jSkip;
            jSkip.SetTarget((inject.JumpBackAddress + 2) + *(inject.JumpBackAddress + 1)); // loc_54770F

            const uint32_t offGetSettings = addrGetSettings - reinterpret_cast<uint32_t>(codespace + 5);
            WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetSettings));              // call getSettings
            WriteInstruction16(codespace, 0x8bc8);                                              // mov  ecx, eax
            const uint32_t offGetDisableCamAutoCenter = addrGetDisableCamAutoCenter - reinterpret_cast<uint32_t>(codespace + 5);
            WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetDisableCamAutoCenter));  // call getDisableCamAutoCenter
            WriteInstruction16(codespace, 0x84c0);                                              // test al, al

            jSkip.WriteJump(codespace, JC::JNZ);

            std::memcpy(codespace,
                        inject.OverwrittenInstructions.data(),
                        inject.OverwrittenInstructions.size());
            codespace += inject.OverwrittenInstructions.size();

            BranchHelper4Byte jmpBack;
            jmpBack.SetTarget(inject.JumpBackAddress);
            jmpBack.WriteJump(codespace, JC::JMP);

            execData.Codespace = codespace;
        }
    }
}
}
