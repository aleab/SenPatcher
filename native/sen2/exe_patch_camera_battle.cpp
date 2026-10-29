#include "exe_patch.h"

#include <cassert>

#include "util/bit.h"
#include "util/memread.h"
#include "util/memwrite.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen2 {
extern float RealTimeStep;

static float PointerSpeed = 0;

void PatchBattleCamera(PatchExecData& execData,
                       bool disableBattleCameraIdleRotation,
                       bool disableBattleCameraTiltReset,
                       float pointerSpeedFactor1,
                       float pointerSpeedFactor2,
                       bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;
    const float* const addrPointerSpeed = &PointerSpeed;

    const uint32_t addrGetInputSettings =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x6AC950, 0x6AD980));
    const uint32_t addrGetMouseSensitivity =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x6ACEA0, 0x6ADED0));
    const uint32_t addrIsButtonPressed =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x414FB0, 0x414FC0));
    const uint32_t addrIsButtonPressedThis =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0xA7945C, 0xA7D454));

    // disable idle rotation
    if (disableBattleCameraIdleRotation) {
        // Inject pos:
        //      cmp     byte ptr [esi+0FCh], 0      80 BE FC 00 00 00 00
        // ->#  jz      short loc_4C4D16            74 0D
        //      subss   xmm0, dword ptr [esi+100h]  F3 0F 5C 86 00 01 00 00
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4C4A47, 0x4C4D07);

        PageUnprotect page(logger, addressInjectPos, 1);
        *addressInjectPos = static_cast<char>(0xeb); // jz -> jmp
    }

    // disable pitch/tilt reset
    if (disableBattleCameraTiltReset) {
        // Inject pos:
        // ->#  movss   xmm0, [ebp+8h]          F3 0F 10 45 08
        //   #  push    1                       6A 01
        //   #  ...
        //   #  movss   [esp+10h], xmm0         F3 0F 11 44 24 10
        //   #  movq    xmm0, [ebp-0BCh]        F3 0F 7E 85 44 FF FF FF
        //   #  movq    qword ptr [eax], xmm0   66 0F D6 00
        //   #  ...
        //   #  call    setCameraRotation       E8 ?? ?? ?? ??
        //
        char* addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4C68B1, 0x4C6B71);
        size_t length = 50;

        PageUnprotect page(logger, addressInjectPos, length);

        BranchHelper4Byte jmp;
        jmp.SetTarget(addressInjectPos + length);
        jmp.WriteJump(addressInjectPos, JC::JMP);

        for (size_t i = 5; i < length; ++i) {
            *addressInjectPos++ = static_cast<char>(0x90);
        }
    }

    // disable pitch/tilt reset (on enemy turn)
    if (disableBattleCameraTiltReset) {
        // Inject pos:
        // ->#  movss   xmm0, [ebp+8h]          F3 0F 10 45 08
        //   #  push    2                       6A 02
        //   #  ...
        //   #  movss   [esp+10h], xmm0         F3 0F 11 44 24 10
        //   #  movq    xmm0, [ebp-0BCh]        F3 0F 7E 85 44 FF FF FF
        //   #  movq    qword ptr [eax], xmm0   66 0F D6 00
        //   #  ...
        //   #  call    setCameraRotation       E8 ?? ?? ?? ??
        //
        char* addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4C752B, 0x4C77B9);
        size_t length = 50;

        PageUnprotect page(logger, addressInjectPos, length);

        BranchHelper4Byte jmp;
        jmp.SetTarget(addressInjectPos + length);
        jmp.WriteJump(addressInjectPos, JC::JMP);

        for (size_t i = 5; i < length; ++i) {
            *addressInjectPos++ = static_cast<char>(0x90);
        }
    }

    // use unscaled timestep * mouse sensitivity
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4C5FED, 0x4C62AD);

        float sensitivityMult = 4.0f / 7.0f;
        const char* const addrSensitivityMult = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(sensitivityMult));

        // Inject pos:
        // ->#  movss   xmm0, [ebp+8h]          F3 0F 10 45 08
        //   #  push    ecx                     51
        //      mov     ecx, edi                8B CF
        //      movss   [esp], xmm0             F3 0F 11 04 24
        //      call    sub_40D29C              E8 ?? ?? ?? ??
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        // use (GetMouseSensitivity() * sensitivityMult * RealTimeStep) instead of scaled timestep

        if (adjustTimersForTurbo) {
            Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM0, addrRealTimeStep);
        } else {
            std::memcpy(codespace, inject.OverwrittenInstructions.data(), 5);
            codespace += 5;
        }

        const uint32_t offGetInputSettings = addrGetInputSettings - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetInputSettings));     // call     getInputSettings
        WriteInstruction8(codespace, 0x51);                                             // push     ecx
        WriteInstruction16(codespace, 0x8bc8);                                          // mov      ecx, eax
        const uint32_t offGetMouseSensitivity = addrGetMouseSensitivity - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetMouseSensitivity));  // call     getMouseSensitivity
        WriteInstruction32(codespace, 0x660f6ec0);                                      // movd     xmm0, eax
        WriteInstruction24(codespace, 0x0f5bc0);                                        // cvtdq2ps xmm0, xmm0

        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrSensitivityMult);
        if (adjustTimersForTurbo) {
            Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrRealTimeStep);
        } else {
            WriteInstruction40(codespace, 0xf30f594508); // mulss xmm0, [ebp+8h]
        }

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // pointer sensitivity - use unscaled timestep
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x46DA54, 0x46DC14);

        pointerSpeedFactor1 = pointerSpeedFactor1 == 0.0f ? 1.0f : fabsf(pointerSpeedFactor1);
        const char* const addrPointerSpeedFactor1 = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(pointerSpeedFactor1));

        pointerSpeedFactor2 = pointerSpeedFactor2 == 0.0f ? 1.0f : fabsf(pointerSpeedFactor2);
        const char* const addrPointerSpeedFactor2 = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(pointerSpeedFactor2));

        // Inject pos:
        // ->#  mulss   xmm0, [esp+10h]             F3 0F 59 44 24 10
        //      mulss   xmm0, ds:dword_8F1A34       F3 0F 59 05 ?? ?? ?? ??     7.5f
        //      mulss   xmm0, [ebp+0Ch]             F3 0F 59 45 0C              timestep
        //      addss   xmm0, dword ptr [esp+30h]   F3 0F 58 44 24 30
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        if (pointerSpeedFactor1 != 1.0f || pointerSpeedFactor2 != 1.0f) {
            if (pointerSpeedFactor1 == pointerSpeedFactor2) {
                Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM1, addrPointerSpeedFactor1);
            } else {
                WriteInstruction48(codespace, 0x8b0d00000000 | BSWAP32(addrIsButtonPressedThis));   // mov  ecx, dword_A7D454 (this)
                WriteInstruction16(codespace, 0x6a01);                                              // push 1
                WriteInstruction16(codespace, 0x6a00);                                              // push 0
                WriteInstruction16(codespace, 0x6a05);                                              // push 5 (R1/RB)
                const uint32_t offset = addrIsButtonPressed - reinterpret_cast<uint32_t>(codespace + 5);
                WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offset));                      // call isButtonPressed
                WriteInstruction16(codespace, 0x84c0);                                              // test al, al

                BranchHelper4Byte j0, j1;
                j0.WriteJump(codespace, JC::JZ);
                Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM1, addrPointerSpeedFactor2);
                j1.WriteJump(codespace, JC::JMP);
                j0.SetTarget(codespace);
                Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM1, addrPointerSpeedFactor1);
                j1.SetTarget(codespace);
            }

            if (adjustTimersForTurbo) {
                Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM1, addrRealTimeStep);
            } else {
                WriteInstruction40(codespace, 0xf30f594d0c); // mulss xmm1, [ebp+0Ch]
            }
        } else if (adjustTimersForTurbo) {
            Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM1, addrRealTimeStep);
        } else {
            WriteInstruction40(codespace, 0xf30f104d0c); // movss xmm1, [ebp+0Ch]
        }

        WriteInstruction32(codespace, 0xf30f110d); // movss dword ptr[&PointerSpeed], xmm1
        std::memcpy(codespace, &addrPointerSpeed, 4);
        codespace += 4;

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x46DA62, 0x46DC22);

        // Inject pos:
        //      mulss   xmm0, [esp+10h]             F3 0F 59 44 24 10
        //      mulss   xmm0, ds:dword_8F1A34       F3 0F 59 05 ?? ?? ?? ??     7.5f
        // ->#  mulss   xmm0, [ebp+0Ch]             F3 0F 59 45 0C              timestep
        //      addss   xmm0, dword ptr [esp+30h]   F3 0F 58 44 24 30
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrPointerSpeed);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x46DA9C, 0x46DC5C);

        // Inject pos:
        //      mulss   xmm2, [esp+10h]             F3 0F 59 54 24 10
        //      mulss   xmm2, ds:dword_8F1A34       F3 0F 59 15 ?? ?? ?? ??     7.5f
        // ->#  mulss   xmm2, [ebp+0Ch]             F3 0F 59 55 0C              timestep
        //      addss   xmm2, dword ptr [esp+38h]   F3 0F 58 54 24 38
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM2, addrPointerSpeed);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
}
}
