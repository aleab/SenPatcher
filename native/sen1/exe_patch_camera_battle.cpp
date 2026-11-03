#include "exe_patch.h"

#include <algorithm>
#include <bit>

#include "util/bit.h"
#include "util/memwrite.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen1 {
extern float RealTimeStep;

static char BattleStarted = 0;
static float PointerSpeed = 1.0f;

void PatchBattleCamera(PatchExecData& execData,
                       bool disableBattleCameraIdleRotation,
                       bool disableBattleCameraTiltReset,
                       float defaultCameraTiltDeg,
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
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x40C6AD, 0x40C6C6));
    const uint32_t addrGetMouseSensitivity =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x40BDCF, 0x40BDE8));
    const uint32_t addrIsButtonPressed =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x40DB5C, 0x40DB7A));
    const uint32_t addrIsButtonPressedThis =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0xC7A5A0, 0xC7C598));

    // disable idle rotation
    if (disableBattleCameraIdleRotation) {
        // Inject pos:
        //      cmp     byte ptr [esi+0F4h], 0      80 BE F4 00 00 00 00
        // ->#  jz      short loc_525851            74 0E
        //      fld     [ebp-8h]                    D9 45 F8
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x524121, 0x525841);

        PageUnprotect page(logger, addressInjectPos, 1);
        *addressInjectPos = static_cast<char>(0xeb); // jz -> jmp
    }

    // disable pitch/tilt reset
    if (disableBattleCameraTiltReset) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5249BE, 0x5260DE);

        // Inject pos:
        //      fst     dword ptr [esi+0CCh]        D9 96 CC 00 00 00
        // ->#  mov     [esi+0F0h], ebx             89 9E F0 00 00 00       default SM state
        //      fst     dword ptr [esi+0D4h]        D9 96 D4 00 00 00
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        // The subroutine at 0x525FE0 (en) seems to reset a bunch of variables related to the
        // battle camera to their default values and it's called at the start of every battle.

        Emit_MOV_BytePtr_R8(codespace, &BattleStarted, R8::BL); // 0

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    if (disableBattleCameraTiltReset) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x525925, 0x527045);

        // Inject pos:
        // ->#  fld     [ebp-9Ch]               D9 85 64 FF FF FF
        //      mov     edx, [ebp-0C4h]         8B 95 3C FF FF FF
        //      mov     ecx, [ebp-0C0h]         8B 8D 40 FF FF FF
        //      push    1                       6A 01
        //      ...
        //      call    setCameraRotation       E8 ?? ?? ?? ??
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper4Byte jmpBack, jSkip;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jSkip.SetTarget(addressInjectPos + 59);

        // SM state == 3: battle start / camera reset (?) / player turn (?)
        //    Skip the setCameraRotation call, but not if this is the first time in this
        //    battle that the SM enters this state.

        Emit_MOV_R8_BytePtr(codespace, R8::AL, &BattleStarted);
        Emit_TEST_R8_R8(codespace, R8::AL, R8::AL);
        jSkip.WriteJump(codespace, JC::JNZ);

        Emit_MOV_R8_IMM8(codespace, R8::AL, 1);
        Emit_MOV_BytePtr_R8(codespace, &BattleStarted, R8::AL);
        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    if (disableBattleCameraTiltReset) {
        // Inject pos:
        // ->#  fld     [ebp-9Ch]               D9 85 64 FF FF FF
        //      mov     edx, [ebp-0C4h]         8B 95 3C FF FF FF
        //      mov     ecx, [ebp-0C0h]         8B 8D 40 FF FF FF
        //      push    2                       6A 02
        //      ...
        //      call    setCameraRotation       E8 ?? ?? ?? ??
        char* addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x52654D, 0x527C6D);
        size_t length = 59;

        // SM state == 12: npc turn (?)
        //    Always skip the setCameraRotation call.

        PageUnprotect page(logger, addressInjectPos, length);

        BranchHelper4Byte jmp;
        jmp.SetTarget(addressInjectPos + length);
        jmp.WriteJump(addressInjectPos, JC::JMP);

        for (size_t i = 5; i < length; ++i) {
            *addressInjectPos++ = static_cast<char>(0x90);
        }
    }

    // change default camera tilt/distance
    defaultCameraTiltDeg = std::clamp(fmodf(fabsf(defaultCameraTiltDeg), 360.0f), 5.0f, 40.0f);
    if (defaultCameraTiltDeg != 15.0f) {
        char* const addresses[2] = {
            GetCodeAddressJpEn(version, textRegion, 0x5258E2, 0x527002),
            GetCodeAddressJpEn(version, textRegion, 0x526526, 0x527C46),
        };

        // Inject pos:
        //   #  fld     ds:flt_B3E8A8           D9 05 ?? ?? ?? ??
        //      mov     edx, [ebp-100h]         8B 95 00 FF FF FF
        // ->   fstp    dword ptr [ebp-0C4h]    D9 9D 3C FF FF FF
        {
            // re-arrange instructions
            auto address = addresses[0] - 12;
            PageUnprotect page(logger, address, 6);
            std::memcpy(address, address + 6, 6);
        }

        const uint32_t uDefaultCameraTiltDeg = std::bit_cast<uint32_t>(defaultCameraTiltDeg);

        // Inject pos:
        //   #  fld     ds:flt_B3E8A8           D9 05 ?? ?? ?? ??
        // ->#  fstp    dword ptr [ebp-0C4h]    D9 9D 3C FF FF FF
        for (auto address : addresses) {
            address -= 6;
            PageUnprotect page(logger, address, 12);
            Emit_MOV_DwordPtrR32PlusOffset_IMM32(address, R32::EBP, -0xc4, uDefaultCameraTiltDeg);
            WriteInstruction16(address, 0x9090); // nop
        }
    }

    // use unscaled timestep * mouse sensitivity
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x526A27, 0x528147);

        float sensitivityMult = 4.0f / 7.0f;
        const char* const addrSensitivityMult = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(sensitivityMult));

        // Inject pos:
        // ->#  fld     [ebp+8h]                D9 45 08
        //   #  push    ecx                     51
        //   #  mov     ecx, edi                8B CF
        //   #  fstp    [esp]                   D9 1C 24
        //      call    sub_40D29C              E8 ?? ?? ?? ??
        const auto inject = InjectJumpIntoCode<9, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        // use (GetMouseSensitivity() * sensitivityMult * RealTimeStep) instead of scaled timestep

        const uint32_t offGetInputSettings = addrGetInputSettings - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetInputSettings));     // call     j_getInputSettings
        WriteInstruction8(codespace, 0x51);                                             // push     ecx
        WriteInstruction16(codespace, 0x8bc8);                                          // mov      ecx, eax
        const uint32_t offGetMouseSensitivity = addrGetMouseSensitivity - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetMouseSensitivity));  // call     j_getMouseSensitivity
        WriteInstruction32(codespace, 0x660f6ec0);                                      // movd     xmm0, eax
        WriteInstruction24(codespace, 0x0f5bc0);                                        // cvtdq2ps xmm0, xmm0

        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrSensitivityMult);
        if (adjustTimersForTurbo) {
            Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrRealTimeStep);
        } else {
            WriteInstruction40(codespace, 0xf30f594508); // mulss xmm0, [ebp+8h]
        }

        WriteInstruction40(codespace, 0xf30f110424);    // movss    dword ptr[esp], xmm0
        WriteInstruction16(codespace, 0x8bcf);          // mov      ecx, edi

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // pointer sensitivity - use unscaled timestep
    pointerSpeedFactor1 = pointerSpeedFactor1 == 0.0f ? 1.0f : fabsf(pointerSpeedFactor1);
    pointerSpeedFactor2 = pointerSpeedFactor2 == 0.0f ? 1.0f : fabsf(pointerSpeedFactor2);
    if (pointerSpeedFactor1 != 1.0f || pointerSpeedFactor2 != 1.0f || adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4C54DC, 0x4C6D2C);

        const char* const addrPointerSpeedFactor1 = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(pointerSpeedFactor1));

        const char* const addrPointerSpeedFactor2 = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(pointerSpeedFactor2));

        // Inject pos:
        //      fld     [ebp-Ch]        D9 45 F4
        //      fmul    [ebp-4h]        D8 4D FC
        //      fmul    ds:dbl_B3EB20   DC 0D ?? ?? ?? ??
        // ->#  fmul    [ebp+Ch]        D8 4D 0C            timestep
        //   #  fstp    [ebp-Ch]        D9 5D F4
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        if (pointerSpeedFactor1 != 1.0f || pointerSpeedFactor2 != 1.0f) {
            if (pointerSpeedFactor1 == pointerSpeedFactor2) {
                Emit_FLD_DwordPtr(codespace, addrPointerSpeedFactor1);
            } else {
                WriteInstruction48(codespace, 0x8b0d00000000 | BSWAP32(addrIsButtonPressedThis));   // mov  ecx, dword_C7C598 (this)
                WriteInstruction16(codespace, 0x6a01);                                              // push 1
                WriteInstruction16(codespace, 0x6a00);                                              // push 0
                WriteInstruction16(codespace, 0x6a05);                                              // push 5 (R1/RB)
                const uint32_t offset = addrIsButtonPressed - reinterpret_cast<uint32_t>(codespace + 5);
                WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offset));                      // call isButtonPressed
                WriteInstruction16(codespace, 0x84c0);                                              // test al, al

                BranchHelper1Byte j0, j1;
                j0.WriteJump(codespace, JC::JZ);
                Emit_FLD_DwordPtr(codespace, addrPointerSpeedFactor2);
                j1.WriteJump(codespace, JC::JMP);
                j0.SetTarget(codespace);
                Emit_FLD_DwordPtr(codespace, addrPointerSpeedFactor1);
                j1.SetTarget(codespace);
            }

            if (adjustTimersForTurbo) {
                Emit_FMUL_DwordPtr(codespace, addrRealTimeStep);
            } else {
                WriteInstruction24(codespace, 0xd84d0c); // fmul [ebp+Ch]
            }

            WriteInstruction16(codespace, 0xd91d); // fstp dword ptr[&PointerSpeed]
            std::memcpy(codespace, &addrPointerSpeed, 4);
            codespace += 4;

            Emit_FMUL_DwordPtr(codespace, addrPointerSpeed);
        } else if (adjustTimersForTurbo) {
            Emit_FMUL_DwordPtr(codespace, addrRealTimeStep);
        }

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data() + 3,
                    inject.OverwrittenInstructions.size() - 3);
        codespace += (inject.OverwrittenInstructions.size() - 3);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    if (pointerSpeedFactor1 != 1.0f || pointerSpeedFactor2 != 1.0f || adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4C5507, 0x4C6D57);

        // Inject pos:
        //      fld     [ebp+8h]        D9 45 08
        //      fmul    [ebp-4h]        D8 4D FC
        //      fmul    ds:dbl_B3EB20   DC 0D ?? ?? ?? ??
        // ->#  fmul    [ebp+Ch]        D8 4D 0C            timestep
        //   #  fstp    [ebp+Ch]        D9 5D 0C
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        if (pointerSpeedFactor1 != 1.0f || pointerSpeedFactor2 != 1.0f) {
            Emit_FMUL_DwordPtr(codespace, addrPointerSpeed);
        } else if (adjustTimersForTurbo) {
            Emit_FMUL_DwordPtr(codespace, addrRealTimeStep);
        }

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data() + 3,
                    inject.OverwrittenInstructions.size() - 3);
        codespace += (inject.OverwrittenInstructions.size() - 3);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
}
}
