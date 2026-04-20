
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Intel common helpers

\************************************************************************/

#include "arch/Disassemble.h"
#include "memory/Memory.h"
#include "text/CoreString.h"
#include "core/KernelData.h"
#include "arch/intel/x86-32-Asm.h"

/***************************************************************************/

/**
 * @brief Read the low 32 bits of a model-specific register.
 * @param Msr MSR index to read.
 * @return Low 32 bits of the MSR contents.
 */
U32 ReadMSR(U32 Msr) {
    U32 Low;
    U32 High;

    __asm__ volatile (
        "rdmsr"
        : "=a" (Low), "=d" (High)
        : "c" (Msr)
    );

    UNUSED(High);
    return Low;
}

/***************************************************************************/

/**
 * @brief Write a 32-bit value to a model-specific register.
 * @param Msr MSR index to update.
 * @param Value 32-bit value written to the low portion of the register.
 */
void WriteMSR(U32 Msr, U32 Value) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c" (Msr), "a" (Value), "d" (0)
    );
}

/***************************************************************************/

/**
 * @brief Write a full 64-bit value to a model-specific register.
 * @param Msr MSR index to update.
 * @param ValueLow Low 32 bits of the value.
 * @param ValueHigh High 32 bits of the value.
 */
void WriteMSR64(U32 Msr, U32 ValueLow, U32 ValueHigh) {
    __asm__ volatile (
        "wrmsr"
        :
        : "c" (Msr), "a" (ValueLow), "d" (ValueHigh)
    );
}

/***************************************************************************/

void InitializePat(void) {
    LPCPU_INFORMATION CpuInfo = GetKernelCPUInfo();
    if (CpuInfo == NULL) {
        return;
    }

    if ((CpuInfo->Features & INTEL_CPU_FEAT_PAT) == 0u) {
        return;
    }

    const U64 PatValue = U64_Make(0x00070106u, 0x00070106u);
    WriteMSR64(
        IA32_PAT_MSR,
        U64_Low32(PatValue),
        U64_High32(PatValue));
}

/***************************************************************************/

static void SetDisassemblyAttributes(U32 NumBits) {
    switch (NumBits) {
        case 16:
            SetIntelAttributes(I16BIT, I16BIT);
            break;
        case 64:
            SetIntelAttributes(I64BIT, I64BIT);
            break;
        case 32:
        default:
            SetIntelAttributes(I32BIT, I32BIT);
            break;
    }
}

/***************************************************************************/

void Disassemble(LPSTR Buffer, LINEAR InstructionPointer, U32 NumInstructions, U32 NumBits) {
    STR LineBuffer[128];
    STR DisasmBuffer[64];
    STR HexBuffer[64];

    Buffer[0] = STR_NULL;

    U8* BasePtr = (U8*)VMA_USER;
    U8* CodePtr = (U8*)InstructionPointer;

    if (InstructionPointer >= VMA_USER_LIMIT) BasePtr = (U8*)VMA_USER_LIMIT;
    if (InstructionPointer >= VMA_KERNEL) BasePtr = (U8*)VMA_KERNEL;

    if (IsValidMemory(InstructionPointer) && IsValidMemory(InstructionPointer + NumInstructions - 1)) {
        SetDisassemblyAttributes(NumBits);

        for (U32 Index = 0; Index < NumInstructions; Index++) {
            U32 InstrLength = Intel_MachineCodeToString((LPCSTR)BasePtr, (LPCSTR)CodePtr, DisasmBuffer);

            if (InstrLength > 0 && InstrLength <= 20) {
                StringPrintFormat(HexBuffer, TEXT("%x: "), CodePtr);

                for (U32 ByteIndex = 0; ByteIndex < InstrLength && ByteIndex < 8; ByteIndex++) {
                    STR ByteHex[24];
                    StringPrintFormat(ByteHex, TEXT("%x "), CodePtr[ByteIndex]);
                    StringConcat(HexBuffer, ByteHex);
                }

                while (StringLength(HexBuffer) < 40) {
                    StringConcat(HexBuffer, TEXT(" "));
                }

                StringPrintFormat(LineBuffer, TEXT("%s %s\n"), HexBuffer, DisasmBuffer);
                StringConcat(Buffer, LineBuffer);

                CodePtr += InstrLength;
            } else {
                break;
            }
        }
    } else {
        StringPrintFormat(LineBuffer, TEXT("Can't disassemble at %x (base %x)\n"), CodePtr, BasePtr);
        StringConcat(Buffer, LineBuffer);
    }
}

/***************************************************************************/
