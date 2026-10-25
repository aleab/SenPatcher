#include "exe_patch.h"

#include <cassert>
#include <algorithm>
#include <bit>

#include "util/memread.h"
#include "util/memwrite.h"
#include "x86/emitter.h"
#include "x86/inject_jump_into.h"
#include "x86/page_unprotect.h"

namespace SenLib::Sen1 {
extern float RealTimeStep;

void PatchMapControls(PatchExecData& execData,
                      bool invertMapCameraYAxis,
                      float mapCameraMinTiltDeg,
                      bool adjustTimersForTurbo) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    const float* const addrRealTimeStep = &RealTimeStep;
    const char* addrCameraZoomFactor;

    // use unscaled timestep (camera rotation)
    if (adjustTimersForTurbo || invertMapCameraYAxis) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x625285, 0x626835);

        const char* const addrCameraRotationFactor = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, 0x40000000); // 2.0f

        // Inject pos:
        //      call    sub_40CFCC      E8 ?? ?? ?? ??
        //      fld     [ebp-14h]       D9 45 EC            camera rotation delta
        // ->#  fld     [ebp+8h]        D9 45 08            timestep
        //   #  fld     st              D9 C0
        //      fmulp   st(2),st        DE CA
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        // invert Y axis rotation
        if (invertMapCameraYAxis)
            WriteInstruction16(codespace, 0xd9e0);              // fchs

        if (adjustTimersForTurbo) {
            WriteInstruction16(codespace, 0xd905);              // fld dword ptr[&CameraRotationFactor]
            std::memcpy(codespace, &addrCameraRotationFactor, 4);
            codespace += 4;
            WriteInstruction16(codespace, 0xd80d);              // fmul dword ptr[&RealTimeStep]
            std::memcpy(codespace, &addrRealTimeStep, 4);
            codespace += 4;
            WriteInstruction16(codespace, 0xd9c0);              // fld st
        }

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use unscaled timestep (camera panning)
    if(adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x625371, 0x626921);

        const char* const addrCameraPanningFactor = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, 0x40200000); // 2.5f

        // Inject pos:
        //      fld     [ebp-10h]       D9 45 F0            camera panning delta
        // ->#  fld     [ebp+8h]        D9 45 08            timestep
        //   #  fld     st              D9 C0
        //      fmulp   st(2),st        DE CA
        //      fld     ds:dbl_B3BA28   DD 05 ?? ?? ?? ??
        const auto inject = InjectJumpIntoCode<5>(logger, addressInjectPos, codespace);

        WriteInstruction16(codespace, 0xd905);              // fld dword ptr[&CameraPanningFactor]
        std::memcpy(codespace, &addrCameraPanningFactor, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd80d);              // fmul dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd9c0);              // fld st

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use unscaled timestep (camera zoom+)
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x6251BE, 0x62676E);

        addrCameraZoomFactor = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, 0x40800000); // 4.0f

        // Inject pos:
        //      fld     dword ptr [esi+2ECh]    D9 86 EC 02 00 00
        // ->#  fsub    [ebp+8h]                D8 65 08            timestep (delta zoom)
        //   #  fstp    [ebp-4h]                D9 5D FC
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        WriteInstruction16(codespace, 0xd905);              // fld dword ptr[&CameraZoomFactor]
        std::memcpy(codespace, &addrCameraZoomFactor, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd80d);              // fmul dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xdee9);              // fsubp st(1),st

        std::memcpy(codespace, inject.OverwrittenInstructions.data() + 3, inject.OverwrittenInstructions.size() - 3);
        codespace += (inject.OverwrittenInstructions.size() - 3);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // use unscaled timestep (camera zoom-)
    if (adjustTimersForTurbo) {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x625202, 0x6267B2);

        // Inject pos:
        //      jz      short loc_62681D        74 6B
        // ->#  fld     [ebp+8h]                D9 45 08            timestep (delta zoom)
        //   #  fadd    dword ptr [esi+2ECh]    D8 86 EC 02 00 00
        //      fstp    [ebp-4h]                D9 5D FC
        const auto inject = InjectJumpIntoCode<9, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        WriteInstruction16(codespace, 0xd905); // fld dword ptr[&CameraZoomFactor]
        std::memcpy(codespace, &addrCameraZoomFactor, 4);
        codespace += 4;
        WriteInstruction16(codespace, 0xd80d);         // fmul dword ptr[&RealTimeStep]
        std::memcpy(codespace, &addrRealTimeStep, 4);
        codespace += 4;

        std::memcpy(codespace, inject.OverwrittenInstructions.data() + 3, inject.OverwrittenInstructions.size() - 3);
        codespace += (inject.OverwrittenInstructions.size() - 3);

        BranchHelper4Byte jmpBack;
        jmpBack.SetTarget(inject.JumpBackAddress);
        jmpBack.WriteJump(codespace, JC::JMP);

        execData.Codespace = codespace;
    }

    // change minimum tilt
    mapCameraMinTiltDeg = std::clamp(fmodf(fabsf(mapCameraMinTiltDeg), 360.0f), 0.0f, 45.0f);
    if (mapCameraMinTiltDeg < 45.0f) {
        char* codespace = execData.Codespace;
        char* addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x6252C5, 0x626875);

        // Inject pos:
        // ->#  fld     ds:flt_B43424           D9 05 ?? ?? ?? ??   (45.0f)
        //      fcom    dword ptr [esi+2E4h]    D8 96 E4 02 00 00
        //      fnstsw  ax                      DF E0

        const char* const addrMinCameraTilt = codespace;
        HyoutaUtils::MemWrite::WriteAdvUInt32(codespace, std::bit_cast<uint32_t>(mapCameraMinTiltDeg));

        PageUnprotect page(logger, addressInjectPos + 2, 4);
        std::memcpy(addressInjectPos + 2, &addrMinCameraTilt, 4);

        execData.Codespace = codespace;
    }
}
} // namespace SenLib::Sen1
