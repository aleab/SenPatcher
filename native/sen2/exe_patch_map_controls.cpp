#include "exe_patch.h"

#include <cassert>
#include <algorithm>

#include "util/bit.h"
#include "util/memread.h"
#include "util/memwrite.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen2 {
extern float RealTimeStep;

void PatchMapControls(PatchExecData& execData,
                      bool invertMapCameraYAxis,
                      float mapCameraMinTiltDeg,
                      float defaultMapZoomLevel,
                      bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;
    const char* addrCameraZoomFactor;

    // xmmword 80000000800000008000000080000000h        00 00 00 80 00 00 00 80 00 00 00 80 00 00 00 80
    const uint32_t addrXmmSignMaskPackedSingle =
        reinterpret_cast<uint32_t>(GetCodeAddressJpEn(version, textRegion, 0x8EADA0, 0x8EBE70));

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
}
}
