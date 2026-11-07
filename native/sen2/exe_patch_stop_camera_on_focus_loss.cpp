#include "exe_patch.h"

#include "x86/emitter.h"
#include "x86/inject_jump_into.h"

namespace SenLib::Sen2 {
static char IsFocused = 0;

void PatchStopCameraOnFocusLoss(PatchExecData& execData) {
    HyoutaUtils::Logger& logger = *execData.Logger;
    char* textRegion = execData.TextRegion;
    GameVersion version = execData.Version;

    using namespace SenPatcher::x86;
    using JC = JumpCondition;

    char* const gameStateAddress = GetCodeAddressJpEn(version, textRegion, 0x116F3C0, 0x1173C40);

    // GetAnalogInputAxes() or something. [ebp+Ch] == 1: right stick (camera)
    // Return [0,0] if out of focus. Also clear last polled mouse input (before losing focus).
    {
        char* codespace = execData.Codespace;
        char* const addressInjectPos = GetCodeAddressJpEn(version, textRegion, 0x4148BE, 0x4148CE);
        char* const addressReturnLabelPos = GetCodeAddressJpEn(version, textRegion, 0x414E7F, 0x414E8F);

        // Inject pos:
        // ->#  mov     eax, [esi+374h]     8B 86 74 03 00 00
        //      mov     edx, [eax+94h]      8B 90 94 00 00 00
        //      mov     ecx, [eax+98h]      8B 88 98 00 00 00
        //      mov     eax, edx            8B C2
        //      mov     [esp+18h], edx      89 54 24 18
        const auto inject = InjectJumpIntoCode<6, PaddingInstruction::Nop>(logger, addressInjectPos, codespace);

        BranchHelper1Byte jNotFocused, jFocused, j0;
        BranchHelper4Byte jReturn;
        jReturn.SetTarget(addressReturnLabelPos);

        Emit_MOV_R32_DwordPtr(codespace, R32::ECX, gameStateAddress);
        Emit_TEST_R32_R32(codespace, R32::ECX, R32::ECX);
        jNotFocused.WriteJump(codespace, JC::JZ);
        WriteInstruction48(codespace, 0x8a89c4090000); // mov cl, byte ptr [ecx+9C4h]
        Emit_TEST_R8_R8(codespace, R8::CL, R8::CL);
        jFocused.WriteJump(codespace, JC::JNZ);

        // not focused: return [0,0]
        jNotFocused.SetTarget(codespace);
        Emit_MOV_BytePtr_IMM8(codespace, &IsFocused, 0);
        Emit_XORPS_XMM_XMM(codespace, XMM::XMM1, XMM::XMM1);                // dx = 0
        Emit_MOV_DwordPtrR32PlusOffset_IMM32(codespace, R32::ESP, 0x1c, 0); // dy = 0
        jReturn.WriteJump(codespace, JC::JMP);

        // focused: check if this is the first frame we're back in focus
        jFocused.SetTarget(codespace);
        Emit_MOV_R8_BytePtr(codespace, R8::CL, &IsFocused);
        Emit_MOV_BytePtr_IMM8(codespace, &IsFocused, 1);
        Emit_TEST_R8_R8(codespace, R8::CL, R8::CL);
        j0.WriteJump(codespace, JC::JNZ);

        // just got focus: clear raw mouse [dx,dy] and return [0,0]
        Emit_XORPS_XMM_XMM(codespace, XMM::XMM1, XMM::XMM1);                // dx = 0
        Emit_MOV_DwordPtrR32PlusOffset_IMM32(codespace, R32::ESP, 0x1c, 0); // dy = 0
        Emit_MOV_R32_DwordPtrR32PlusOffset(codespace, R32::ECX, R32::ESI, 0x374);
        Emit_MOV_DwordPtrR32PlusOffset_IMM32(codespace, R32::ECX, 0x94, 0); // raw mouse (?) dx = 0
        Emit_MOV_DwordPtrR32PlusOffset_IMM32(codespace, R32::ECX, 0x98, 0); // raw mouse (?) dy = 0
        jReturn.WriteJump(codespace, JC::JMP);

        j0.SetTarget(codespace);
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
