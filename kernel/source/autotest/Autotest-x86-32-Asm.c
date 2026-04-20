
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    X86_32 Machine Code Instruction Disassembler Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "memory/Heap.h"
#include "arch/intel/x86-32-Asm.h"
#include "log/Log.h"
#include "text/CoreString.h"

/************************************************************************/

typedef struct {
    U8 MachineCode[16];  // Machine code bytes
    U32 Length;          // Length in bytes
    LPCSTR ExpectedAsm;  // Expected assembly output
    LPCSTR Description;  // Test description
} DISASM_TEST;

/************************************************************************/

static DISASM_TEST DisasmTests[] = {
    // Basic arithmetic
    {{0x00, 0x00}, 2, TEXT("ADD BYTE PTR [EAX], AL"), TEXT("ADD Eb, Gb")},
    {{0x01, 0x00}, 2, TEXT("ADD DWORD PTR [EAX], EAX"), TEXT("ADD Ev, Gv")},
    {{0x04, 0x42}, 2, TEXT("ADD AL, 0x42"), TEXT("ADD AL, Ib")},
    {{0x05, 0x34, 0x12, 0x00, 0x00}, 5, TEXT("ADD EAX, 0x1234"), TEXT("ADD EAX, Id")},

    // Stack operations
    {{0x50}, 1, TEXT("PUSH EAX"), TEXT("PUSH EAX")},
    {{0x58}, 1, TEXT("POP EAX"), TEXT("POP EAX")},
    {{0x60}, 1, TEXT("PUSHA "), TEXT("PUSHA")},
    {{0x61}, 1, TEXT("POPA "), TEXT("POPA")},

    // Prefixes and special instructions
    {{0xF0}, 1, TEXT("LOCK "), TEXT("LOCK prefix")},
    {{0xF4}, 1, TEXT("HLT "), TEXT("HLT instruction")},
    {{0x90}, 1, TEXT("NOP "), TEXT("NOP instruction")},

    // Group instructions (extensions)
    {{0xFF, 0x00}, 2, TEXT("INC DWORD PTR [EAX]"), TEXT("FF /0 - INC Ev")},
    {{0xFF, 0x08}, 2, TEXT("DEC DWORD PTR [EAX]"), TEXT("FF /1 - DEC Ev")},
    {{0xFF, 0x20}, 2, TEXT("JMP DWORD PTR [EAX]"), TEXT("FF /4 - JMP Ev")},

    // Two-byte opcodes
    {{0x0F, 0xA2}, 2, TEXT("CPUID "), TEXT("CPUID instruction")},
    {{0x0F, 0x31}, 2, TEXT("RDTSC "), TEXT("RDTSC instruction")},

    // Invalid opcodes should show ???
    {{0xD6}, 1, TEXT("??? "), TEXT("Invalid opcode")}};

static const U32 NumDisasmTests = sizeof(DisasmTests) / sizeof(DisasmTests[0]);

/************************************************************************/

void TestX86_32Disassembler(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;
    STR ResultBuffer[256];

    // Set 32-bit mode for testing
    SetIntelAttributes(I32BIT, I32BIT);

    for (U32 i = 0; i < NumDisasmTests; i++) {
        DISASM_TEST* Test = &DisasmTests[i];

        // Disassemble the machine code
        U32 Length = Intel_MachineCodeToString((LPCSTR)Test->MachineCode, (LPCSTR)Test->MachineCode, ResultBuffer);

        // Check if length matches expected
        BOOL LengthOK = (Length == Test->Length);

        // Check if assembly string matches expected
        BOOL AssemblyOK = (STRINGS_EQUAL(ResultBuffer, Test->ExpectedAsm));

        Results->TestsRun++;
        if (LengthOK && AssemblyOK) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test %d FAILED: %s"), i, Test->Description);
            if (!LengthOK) {
                DEBUG(TEXT("Length mismatch: expected %d, got %d"), Test->Length, Length);
            }
            if (!AssemblyOK) {
                DEBUG(TEXT("Assembly mismatch: expected '%s', got '%s'"), Test->ExpectedAsm, ResultBuffer);
            }
        }
    }
}

