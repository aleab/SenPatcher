#include "exe_patch.h"

#include <algorithm>
#include <bit>

#include "util/bit.h"
#include "util/memwrite.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen2 {
extern float RealTimeStep;

static char ShiftKeyIsDown = 0;

void PatchMapControls(PatchExecData& execData,
                      bool invertMapCameraYAxis,
                      float mapCameraMinTiltDeg,
                      float defaultMapZoomLevel,
                      bool useFreeCursorOnWorldMap,
                      float worldMapCursorSpeed,
                      bool holdShiftToMoveFasterOnWorldMap,
                      bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;
    const char* addrShiftKeyIsDown = &ShiftKeyIsDown;
    const char* addrCameraZoomFactor;

    // xmmword 80000000800000008000000080000000h        00 00 00 80 00 00 00 80 00 00 00 80 00 00 00 80
    const uint32_t addrXmmSignMaskPackedSingle =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x8EADA0, 0x8EBE70));
    const uint32_t addrGetAsyncKeyState =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x8E9564, 0x8EA56C));

    #pragma region AREA MAP

    // use unscaled timestep (camera rotation)
    if (adjustTimersForTurbo || invertMapCameraYAxis) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5C2CD4, 0x5C2C04);

        const char* const addrCameraRotationFactor = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, 0x40000000); // 2.0f

        // Inject pos:
        //      call    ????                    E8 ?? ?? ?? ??
        //      movss   xmm0, [esp+0Ch]         F3 0F 10 44 24 0C           camera rotation delta
        // ->#  movss   xmm1, [ebp+8h]          F3 0F 10 4D 08              timestep
        //      movss   xmm2, ds:dword_8ECF10   F3 0F 10 15 ?? ?? ?? ??     100.0f
        //      mulss   xmm0, xmm1              F3 0F 59 C1
        //      mulss   xmm0, xmm2              F3 0F 59 C2
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        // invert Y axis rotation
        if (invertMapCameraYAxis) {
            WriteInstruction24(codespace, 0x0f5705);    // xorps
            WriteInstruction32(codespace, BSWAP32(addrXmmSignMaskPackedSingle));
        }

        if (adjustTimersForTurbo) {
            Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM1, addrRealTimeStep);
            Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM1, addrCameraRotationFactor);
        }

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use unscaled timestep (camera panning)
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;

        // Inject pos:
        // ->#  mulss   xmm2, [ebp+8h]      F3 0F 59 55 08              timestep
        char* const addresses[2] = {
            GetCodeAddressJpEn(version, textRegion, 0x5C2DC6, 0x5C2CF6),
            GetCodeAddressJpEn(version, textRegion, 0x5C2DF2, 0x5C2D22),
        };

        const char* const addrCameraPanningFactor = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, 0x40200000); // 2.5f

        for (auto& addressInjectPos : addresses) {
            const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

            Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM2, addrRealTimeStep);
            Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM2, addrCameraPanningFactor);

            BranchHelper4Byte jmpBack;
            jmpBack.SetTarget(inject.JumpBackAddress);
            jmpBack.WriteJump(codespace, JC::JMP);
        }

        execData.Codespace = codespace;
    }

    // use unscaled timestep (camera zoom+)
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5C2BFF, 0x5C2B2F);

        addrCameraZoomFactor = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, 0x40800000); // 4.0f

        // Inject pos:
        //      movss   xmm1, dword ptr [esi+2B4h]  F3 0F 10 8E B4 02 00 00
        // ->#  subss   xmm1, [ebp+8h]              F3 0F 5C 4D 08              timestep
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM0, addrRealTimeStep);
        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrCameraZoomFactor);
        Emit_SUBSS_XMM_XMM(codespace, XMM::XMM1, XMM::XMM0);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use unscaled timestep (camera zoom-)
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5C2C41, 0x5C2B71);

        // Inject pos:
        // ->#  movss   xmm0, [ebp+8h]              F3 0F 10 45 08              timestep
        //      addss   xmm0, dword ptr [esi+2B4h]  F3 0F 58 86 B4 02 00 00
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM0, addrRealTimeStep);
        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrCameraZoomFactor);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // change minimum tilt
    mapCameraMinTiltDeg = std::clamp(fmodf(fabsf(mapCameraMinTiltDeg), 360.0f), 0.0f, 45.0f);
    if (mapCameraMinTiltDeg < 45.0f) {
        char* codespace = execData.Codespace;

        // Inject pos:
        // ->#  movss   xmm0, ds:dword_8F1A38               F3 0F 10 05 ?? ?? ?? ??         45.0f
        //      comiss  xmm0, dword ptr [esi+2ACh]          0F 2F 86 AC 02 00 00
        //      jbe     short loc_5C2C57                    76 0A
        //   #  mov     dword ptr [esi+2ACh], 42340000h     C7 86 AC 02 00 00 00 00 34 42   45.0f
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x5C2D0C, 0x5C2C3C);

        const uint32_t uMapCameraMinTiltDeg = std::bit_cast<uint32_t>(mapCameraMinTiltDeg);
        const char* const addrMinCameraTilt = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, uMapCameraMinTiltDeg);

        char* address = addressInjectPos + 4;
        PageUnprotect page1(logger, address, 4);
        std::memcpy(address, &addrMinCameraTilt, 4);

        address += 19;
        PageUnprotect page2(logger, address, 4);
        WriteInstruction32(address, BSWAP32(uMapCameraMinTiltDeg));

        execData.Codespace = codespace;
    }

    // fix default tilt to 45deg
    {
        // Inject pos:
        // ->#  mov     dword ptr [edi+2ACh], 41F00000h     C7 87 AC 02 00 00 00 00 F0 41   30.0f
        char* address = GetCodeAddressJpEn(version, textRegion, 0x5BD7AC, 0x5BD6DC) + 6;

        const uint32_t uDefaultTiltDeg = std::bit_cast<uint32_t>(45.0f);
        PageUnprotect page(logger, address, 4);
        WriteInstruction32(address, BSWAP32(uDefaultTiltDeg));
    }

    // change default zoom level
    defaultMapZoomLevel = std::clamp(fabsf(defaultMapZoomLevel), 0.5f, 2.5f);
    if (defaultMapZoomLevel != 1.0f) {
        // Inject pos:
        // ->#  mov     dword ptr [edi+2B4h], 3F800000h     C7 87 B4 02 00 00 00 00 80 3F   1.0f
        char* address = GetCodeAddressJpEn(version, textRegion, 0x5BD95A, 0x5BD88A) + 6;

        const uint32_t uDefaultZoomLevel = std::bit_cast<uint32_t>(defaultMapZoomLevel);
        PageUnprotect page(logger, address, 4);
        WriteInstruction32(address, BSWAP32(uDefaultZoomLevel));
    }

    #pragma endregion

    #pragma region WORLD MAP

    // use unscaled timestep
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x59EDBB, 0x59F016);

        // Inject pos:
        //      movss   dword ptr [edi+6FCh], xmm0      F3 0F 11 87 FC 06 00 00
        // ->#  movss   xmm0, [ebp+8]                   F3 0F 10 45 08
        //      divss   xmm0, ds:flt_8EC224             F3 0F 5E 05 ?? ?? ?? ??
        //      lea     eax, [ebp+0CCh]                 8D 85 34 FF FF FF
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        Emit_MOVSS_XMM_DwordPtr(codespace, XMM::XMM0, addrRealTimeStep);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x59EDF7, 0x59F052);

        const char* const addrCameraRotationFactor = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(2.5f));

        // Inject pos:
        // ->#  mulss   xmm0, [ebp+0BCh]                F3 0F 59 85 44 FF FF FF
        //      addss   xmm0, dword ptr [edi+6ECh]      F3 0F 58 87 EC 06 00 00
        //      comiss  xmm1, xmm0                      0F 2F C8
        const auto inject = InjectJumpIntoCode<8, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrCameraRotationFactor);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // invert camera Y axis
    if (invertMapCameraYAxis) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x59EDDC, 0x59F037);

        // Inject pos:
        // ->#  movss   xmm0, [ebp+0C8h]                F3 0F 10 85 38 FF FF FF
        //      mulss   xmm0, ds:dword_8F58DC           F3 0F 59 05 ?? ?? ?? ??
        //      movss   xmm1, ds:dword_8FD72C           F3 0F 10 0D ?? ?? ?? ??
        //      add     esp, 4                          83 C4 04
        const auto inject = InjectJumpIntoCode<8, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        WriteInstruction24(codespace, 0x0f5705);    // xorps
        WriteInstruction32(codespace, BSWAP32(addrXmmSignMaskPackedSingle));

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use free cursor
    if (useFreeCursorOnWorldMap) {
        // Inject pos:
        // ->1  push    3                               6A 03
        //      push    ecx                             51
        //      movss   dword ptr [ebp-34h], xmm0       F3 0F 11 45 CC
        // ->2  movss   xmm0, dword ptr [ebp-20h]       F3 0F 10 45 E0
        //      lea     eax, [ebp-3Ch]                  8D 45 C4
        //      movss   dword ptr [esp], xmm0           F3 0F 11 04 24
        //      push    eax                             50
        //      lea     ecx, [ebx+8D8h]                 8D 8B D8 08 00 00
        //      mov     dword ptr [ebp-38h], 0          C7 45 C8 00 00 00 00
        //      call    sub_4B2470                      E8 ?? ?? ?? ??
        // ->3  mov     byte ptr [ebx+918h], 1          C6 83 18 09 00 00 01
        char* const baseAddress = GetCodeAddressJpEn(version, textRegion, 0x5989E6, 0x598C16);
        {
            char* const address = baseAddress;
            PageUnprotect page(logger, address + 1, 1);
            *(address + 1) = static_cast<char>(0);
        }
        {
            char* address = baseAddress + 8;
            PageUnprotect page(logger, address, 5);
            Emit_XORPS_XMM_XMM(address, XMM::XMM0, XMM::XMM0);
            *address++ = static_cast<char>(0x90);
            *address++ = static_cast<char>(0x90);
        }
        {
            char* const address = baseAddress + 40;
            PageUnprotect page(logger, address + 6, 1);
            *(address + 6) = static_cast<char>(0);
        }
    }

    // cursor speed
    worldMapCursorSpeed = std::clamp(fabsf(worldMapCursorSpeed), 0.1f, 1.5f);
    if (worldMapCursorSpeed != 1.0f) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x59EFDD, 0x59F238);

        const char* const addrWorldMapCursorSpeed = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(worldMapCursorSpeed));

        // Inject pos:
        // ->#  movss   dword ptr [ebp-0B0h], xmm0      F3 0F 11 85 50 FF FF FF
        //      call    sub_4E43B0                      E8 ?? ?? ?? ??
        //      movss   xmm1, [ebp-0C4h]                F3 0F 10 8D 3C FF FF FF
        //      movss   xmm0, [ebp-0C0h]                F3 0F 10 85 40 FF FF FF
        const auto inject = InjectJumpIntoCode<8, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        Emit_MULSS_XMM_DwordPtr(codespace, XMM::XMM0, addrWorldMapCursorSpeed);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // hold shift to move faster
    if (holdShiftToMoveFasterOnWorldMap) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x59E9EA, 0x59EC4A);
        char* const addressSkipPos = GetCodeAddressJpEn(version, textRegion, 0x59ECFE, 0x59EF59);

        // Inject pos:
        //      movss   xmm0, dword ptr [edi+910h]      F3 0F 10 87 10 09 00 00
        //      ucomiss xmm0, dword ptr [edi+90Ch]      0F 2E 87 0C 09 00 00
        //      lahf                                    9F
        //      test    ah, 44h                         F6 C4 44
        //      jp      loc_59EFE9                      0F 8A ?? ?? ?? ??
        // ->#  mov     ecx, dword_A7D454               8B 0D ?? ?? ?? ??
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper1Byte j0;
        BranchHelper4Byte jSkip;
        jSkip.SetTarget(addressSkipPos);

        // Check if VK_SHIFT is being held

        WriteInstruction16(codespace, 0x6a10);                                          // push VK_SHIFT
        WriteInstruction48(codespace, 0xff1500000000 | BSWAP32(addrGetAsyncKeyState));  // call ds:GetAsyncKeyState
        WriteInstruction24(codespace, 0x6685c0);                                        // test ax, ax
        j0.WriteJump(codespace, JC::JNS);

        // Set ShiftKeyIsDown. Skip all the isButtonPressed checks to avoid conflicting
        // behaviours in case shift is also mapped to one of the checked buttons.

        Emit_MOV_BytePtr_IMM8(codespace, addrShiftKeyIsDown, 1);
        jSkip.WriteJump(codespace, JC::JMP);

        // If shift has just been released, skip the isButtonPressed checks this frame as well.

        j0.SetTarget(codespace);
        Emit_MOV_R8_BytePtr(codespace, R8::AL, addrShiftKeyIsDown);
        Emit_MOV_BytePtr_IMM8(codespace, addrShiftKeyIsDown, 0);
        Emit_TEST_R8_R8(codespace, R8::AL, R8::AL);
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
    if (holdShiftToMoveFasterOnWorldMap) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x59EFC2, 0x59F21D);

        // Inject pos:
        //      call    isButtonPressed                 E8 ?? ?? ?? ??
        // ->#  movss   xmm0, dword ptr [ebp-0B0h]      F3 0F 10 85 50 FF FF FF
        //      test    al, al                          84 C0
        //      jz      short loc_59F231                74 08
        //      mulss   xmm0, ds:dword_8EBB80           F3 0F 59 05 ?? ?? ?? ??
        const auto inject = InjectJumpIntoCode<8, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper4Byte jMoveFaster;
        jMoveFaster.SetTarget(addressInjectPos + 12);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        WriteInstruction16(codespace, 0xf605); // test byte ptr [&ShiftKeyIsDown], 1
        std::memcpy(codespace, &addrShiftKeyIsDown, 4);
        codespace += 4;
        *codespace++ = static_cast<char>(0x01);
        jMoveFaster.WriteJump(codespace, JC::JNZ);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // swap right and left stick => mouse panning!
    /*{
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x59ED5C, 0x59EFB7);

        // Inject pos:
        //      movss   dword ptr [ebp-0C4h], xmm0      F3 0F 11 85 3C FF FF FF
        //      movss   xmm0, dword ptr [eax+4]         F3 0F 10 40 04
        // ->#  movss   dword ptr [ebp-0C0h], xmm0      F3 0F 11 85 40 FF FF FF
        const auto inject = InjectJumpIntoCode<8, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        std::memcpy(codespace,
                    inject.OverwrittenInstructions.data(),
                    inject.OverwrittenInstructions.size());
        codespace += inject.OverwrittenInstructions.size();

        WriteInstruction64(codespace, 0xf30f7e853cffffff); // movq xmm0, [ebp-0C4h]         [leftStickX, leftStickY]
        WriteInstruction64(codespace, 0xf30f7e8d34ffffff); // movq xmm1, [ebp-0CCh]         [rightStickX, rightStickY]
        WriteInstruction64(codespace, 0x660fd68d3cffffff); // movq [ebp-0C4h], xmm1
        WriteInstruction64(codespace, 0x660fd68534ffffff); // movq [ebp-0CCh], xmm0

        // feels very sluggish!!

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }*/

    #pragma endregion
}
}
