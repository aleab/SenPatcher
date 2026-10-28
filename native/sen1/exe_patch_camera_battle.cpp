#include "exe_patch.h"

#include <cassert>
#include <bit>

#include "util/bit.h"
#include "util/memread.h"
#include "util/memwrite.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen1 {
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
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x40C6AD, 0x40C6C6));
    const uint32_t addrGetMouseSensitivity =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x40BDCF, 0x40BDE8));
    const uint32_t addrIsButtonPressed =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x40DB5C, 0x40DB7A));

    // disable idle rotation
    if (disableBattleCameraIdleRotation) {
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x524121, 0x525841);

        PageUnprotect page(logger, addressInjectPos, 1);
        *addressInjectPos = static_cast<char>(0xeb); // jz -> jmp
    }

    // disable pitch/tilt reset
    if (disableBattleCameraTiltReset) {
        char* addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x525925, 0x527045);
        size_t length = 59;

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
        char* addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x52654D, 0x527C6D);
        size_t length = 59;

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
        if (adjustTimersForTurbo) {
            WriteInstruction16(codespace, 0x8d15);                                      // lea      edx, dword ptr[&RealTimeStep]
            std::memcpy(codespace, &addrRealTimeStep, 4);
            codespace += 4;
        }

        const uint32_t offGetInputSettings = addrGetInputSettings - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetInputSettings));     // call     j_getInputSettings
        WriteInstruction8(codespace, 0x51);                                             // push     ecx
        WriteInstruction16(codespace, 0x8bc8);                                          // mov      ecx, eax
        const uint32_t offGetMouseSensitivity = addrGetMouseSensitivity - reinterpret_cast<uint32_t>(codespace + 5);
        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offGetMouseSensitivity));  // call     j_getMouseSensitivity
        WriteInstruction32(codespace, 0x660f6ec0);                                      // movd     xmm0, eax
        WriteInstruction24(codespace, 0x0f5bc0);                                        // cvtdq2ps xmm0, xmm0
        WriteInstruction32(codespace, 0xf30f5905);                                      // mulss    xmm0, dword ptr[&SensitivityMult]
        std::memcpy(codespace, &addrSensitivityMult, 4);
        codespace += 4;
        if (adjustTimersForTurbo) {
            WriteInstruction32(codespace, 0xf30f5902);                                  // mulss    xmm0, dword ptr[edx]
        } else {
            WriteInstruction40(codespace, 0xf30f594508);    // mulss    xmm0, [ebp+8h]
        }
        WriteInstruction40(codespace, 0xf30f110424);                                    // movss    dword ptr[esp], xmm0

        WriteInstruction16(codespace, 0x8bcf); // mov ecx, edi

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // pointer sensitivity - use unscaled timestep
    {
        char* codespace = execData.Codespace;

        // Inject pos:
        //      fld     [ebp-Ch]        D9 45 F4            sin(atan2(y,x) - ?)
        //      fmul    [ebp-4h]        D8 4D FC            sqrt(x^2 + y^2)
        //      fmul    ds:dbl_B3EB20   DC 0D ?? ?? ?? ??
        // ->#  fmul    [ebp+Ch]        D8 4D 0C            timestep
        //   #  fstp    [ebp-Ch]        D9 5D F4
        char* const addresses[2] = {
            GetCodeAddressJpEn(version, textRegion, 0x4C54DC, 0x4C6D2C),
            GetCodeAddressJpEn(version, textRegion, 0x4C5507, 0x4C6D57),
        };

        pointerSpeedFactor1 = fabsf(pointerSpeedFactor1);
        const char* const addrPointerSpeedFactor1 = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(pointerSpeedFactor1));

        pointerSpeedFactor2 = fabsf(pointerSpeedFactor2);
        const char* const addrPointerSpeedFactor2 = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(pointerSpeedFactor2));

        bool first = true;
        for (auto& addressInjectPos : addresses) {
            const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

            if ((pointerSpeedFactor1 > 0.0f && pointerSpeedFactor1 != 1.0f)
                || (pointerSpeedFactor2 > 0.0f && pointerSpeedFactor2 != 1.0f)) {
                if (first) {
                    BranchHelper4Byte j0, j1;

                    if (pointerSpeedFactor1 == pointerSpeedFactor2) {
                        WriteInstruction16(codespace, 0xd905); // fld  dword ptr[&PointerSpeedFactor1]
                        std::memcpy(codespace, &addrPointerSpeedFactor1, 4);
                        codespace += 4;
                    } else {
                        WriteInstruction48(codespace, version == GameVersion::English ? 0x8b0d98c5c700 : 0x8b0da0a5c700);   // mov  ecx, dword_C7C598
                        WriteInstruction16(codespace, 0x6a01);                                                              // push 1
                        WriteInstruction16(codespace, 0x6a00);                                                              // push 0
                        WriteInstruction16(codespace, 0x6a05);                                                              // push 5
                        const uint32_t offset = addrIsButtonPressed - reinterpret_cast<uint32_t>(codespace + 5);
                        WriteInstruction40(codespace, 0xe800000000 | BSWAP32(offset));                                      // call j_isButtonPressed
                        WriteInstruction16(codespace, 0x84c0);                                                              // test al, al

                        j0.WriteJump(codespace, JC::JZ);
                        if (pointerSpeedFactor2 > 0.0f && pointerSpeedFactor2 != 1.0f) {
                            WriteInstruction16(codespace, 0xd905);      // fld  dword ptr[&PointerSpeedFactor2]
                            std::memcpy(codespace, &addrPointerSpeedFactor2, 4);
                            codespace += 4;
                        } else {
                            WriteInstruction16(codespace, 0xd9e8); // fld1
                        }
                        j1.WriteJump(codespace, JC::JMP);
                        j0.SetTarget(codespace);
                        if (pointerSpeedFactor1 > 0.0f && pointerSpeedFactor1 != 1.0f) {
                            WriteInstruction16(codespace, 0xd905);      // fld  dword ptr[&PointerSpeedFactor1]
                            std::memcpy(codespace, &addrPointerSpeedFactor1, 4);
                            codespace += 4;
                        } else {
                            WriteInstruction16(codespace, 0xd9e8); // fld1
                        }
                        j1.SetTarget(codespace);
                    }

                    if (adjustTimersForTurbo) {
                        WriteInstruction16(codespace, 0xd80d);          // fmul dword ptr[&RealTimeStep]
                        std::memcpy(codespace, &addrRealTimeStep, 4);
                        codespace += 4;
                    } else {
                        WriteInstruction24(codespace, 0xd84d0c); // fmul [ebp+Ch]
                    }

                    WriteInstruction16(codespace, 0xd91d);              // fstp dword ptr[&AoePointerSpeed]
                    std::memcpy(codespace, &addrPointerSpeed, 4);
                    codespace += 4;
                }

                WriteInstruction16(codespace, 0xd80d);                  // fmul dword ptr[&AoePointerSpeed]
                std::memcpy(codespace, &addrPointerSpeed, 4);
                codespace += 4;
            } else if (adjustTimersForTurbo) {
                WriteInstruction16(codespace, 0xd80d); // fmul dword ptr[&RealTimeStep]
                std::memcpy(codespace, &addrRealTimeStep, 4);
                codespace += 4;
            }

            std::memcpy(codespace,
                        inject.OverwrittenInstructions.data() + 3,
                        inject.OverwrittenInstructions.size() - 3);
            codespace += (inject.OverwrittenInstructions.size() - 3);

            BranchHelper4Byte jmpBack;
            jmpBack.SetTarget(inject.JumpBackAddress);
            jmpBack.WriteJump(codespace, JC::JMP);

            first = false;
        }

        execData.Codespace = codespace;
    }
}
}
