
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


    Macro Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"

/************************************************************************/

typedef struct {
    U8 Field1;
    U16 Field2;
    U32 Field3;
    U64 Field4;
    U8 Field5[10];
} TEST_STRUCT_1;

/************************************************************************/

typedef struct {
    U32 Value1;
    U32 Value2;
} TEST_STRUCT_2;

/************************************************************************/

typedef struct {
    U8 ByteField;
    TEST_STRUCT_2 EmbeddedStruct;
    U16 WordField;
} TEST_STRUCT_3;

/************************************************************************/

typedef struct __attribute__((packed)) {
    U8 PackedField1;
    U32 PackedField2;
    U16 PackedField3;
    U8 PackedField4;
} TEST_STRUCT_PACKED;

/************************************************************************/

// Structure with weird alignment requirements
typedef struct {
    U8 WeirdField1;
    U64 WeirdField2 __attribute__((aligned(16)));  // Force 16-byte alignment
    U8 WeirdField3;
    U32 WeirdField4 __attribute__((aligned(32)));  // Force 32-byte alignment
    U16 WeirdField5;
} TEST_STRUCT_WEIRD;

/************************************************************************/

// Zero-length array (flexible array member)
typedef struct {
    U32 FlexLength;
    U8 FlexData[];
} TEST_STRUCT_FLEX;

/************************************************************************/

// Nested packed structures
typedef struct __attribute__((packed)) {
    U8 InnerByte;
    U32 InnerDword;
} INNER_PACKED_STRUCT;

typedef struct {
    U16 OuterWord;
    INNER_PACKED_STRUCT InnerPacked;
    U8 OuterByte;
} TEST_STRUCT_NESTED_PACKED;

/************************************************************************/

// Bitfields
typedef struct {
    U32 BitField1 : 3;
    U32 BitField2 : 5;
    U32 BitField3 : 8;
    U32 BitField4 : 16;
    U8 NormalField;
} TEST_STRUCT_BITFIELDS;

/************************************************************************/

typedef struct {
    U32 ExpectedOffset;
    LPCSTR FieldName;
    LPCSTR Description;
} MEMBER_OFFSET_TEST;

/************************************************************************/

static MEMBER_OFFSET_TEST MemberOffsetTests[] = {
    // TEST_STRUCT_1 tests
    {0, TEXT("Field1"), TEXT("First field should be at offset 0")},
    {2, TEXT("Field2"), TEXT("Field2 after U8 with alignment")},
    {4, TEXT("Field3"), TEXT("Field3 after U16 with alignment")},
    {8, TEXT("Field4"), TEXT("Field4 after U32 with alignment")},
    {16, TEXT("Field5"), TEXT("Field5 after U64")},

    // TEST_STRUCT_3 tests
    {0, TEXT("ByteField"), TEXT("ByteField at start")},
    {4, TEXT("EmbeddedStruct"), TEXT("EmbeddedStruct after U8 with alignment")},
    {12, TEXT("WordField"), TEXT("WordField after embedded struct")},

    // TEST_STRUCT_PACKED tests (no alignment padding)
    {0, TEXT("PackedField1"), TEXT("PackedField1 at offset 0")},
    {1, TEXT("PackedField2"), TEXT("PackedField2 immediately after U8")},
    {5, TEXT("PackedField3"), TEXT("PackedField3 immediately after U32")},
    {7, TEXT("PackedField4"), TEXT("PackedField4 immediately after U16")},

    // TEST_STRUCT_WEIRD tests (extreme alignment)
    {0, TEXT("WeirdField1"), TEXT("WeirdField1 at offset 0")},
    {16, TEXT("WeirdField2"), TEXT("WeirdField2 aligned to 16 bytes")},
    {24, TEXT("WeirdField3"), TEXT("WeirdField3 after U64")},
    {32, TEXT("WeirdField4"), TEXT("WeirdField4 aligned to 32 bytes")},
    {36, TEXT("WeirdField5"), TEXT("WeirdField5 after U32")},

    // TEST_STRUCT_FLEX tests (flexible array)
    {0, TEXT("FlexLength"), TEXT("FlexLength at start")},
    {4, TEXT("FlexData"), TEXT("FlexData after U32")},

    // TEST_STRUCT_NESTED_PACKED tests
    {0, TEXT("OuterWord"), TEXT("OuterWord at offset 0")},
    {2, TEXT("InnerPacked"), TEXT("InnerPacked after U16")},
    {7, TEXT("OuterByte"), TEXT("OuterByte after packed struct")},

    // TEST_STRUCT_BITFIELDS tests (bitfields have no direct offset)
    {4, TEXT("NormalField"), TEXT("NormalField after bitfields")}
};

static const U32 NumMemberOffsetTests = sizeof(MemberOffsetTests) / sizeof(MemberOffsetTests[0]);

/************************************************************************/

typedef struct {
    U32 StructSize;
    BOOL ExpectedHasMember;
    LPCSTR TestDescription;
} HAS_MEMBER_TEST;

/************************************************************************/

static HAS_MEMBER_TEST HasMemberTests[] = {
    // Basic tests - Field1 (offset 0)
    {1, TRUE, TEXT("Field1 exists within 1 byte")},
    {4, TRUE, TEXT("Field1 exists within 4 bytes")},
    {8, TRUE, TEXT("Field1 exists within 8 bytes")},

    // Field2 tests (offset 2)
    {1, FALSE, TEXT("Field2 does not exist within 1 byte")},
    {2, FALSE, TEXT("Field2 does not exist within 2 bytes")},
    {4, TRUE, TEXT("Field2 exists within 4 bytes")},

    // Field3 tests (offset 4)
    {4, FALSE, TEXT("Field3 does not exist within 4 bytes")},
    {8, TRUE, TEXT("Field3 exists within 8 bytes")},

    // Packed structure tests
    {1, TRUE, TEXT("PackedField1 exists within 1 byte")},
    {1, FALSE, TEXT("PackedField2 does not exist within 1 byte")},
    {5, TRUE, TEXT("PackedField2 exists within 5 bytes")},

    // Weird alignment tests
    {16, FALSE, TEXT("WeirdField2 does not exist within 16 bytes")},
    {32, FALSE, TEXT("WeirdField4 does not exist within 32 bytes")},

    // Edge case
    {0, FALSE, TEXT("No fields exist within 0 bytes")}
};

static const U32 NumHasMemberTests = sizeof(HasMemberTests) / sizeof(HasMemberTests[0]);

/************************************************************************/

void TestMemberOffsetMacro(TEST_RESULTS* Results) {
    for (U32 i = 0; i < NumMemberOffsetTests; i++) {
        MEMBER_OFFSET_TEST* Test = &MemberOffsetTests[i];
        U32 ActualOffset = 0;
        BOOL TestPassed = FALSE;

        Results->TestsRun++;

        // Test different struct fields based on index
        if (i < 5) {
            // TEST_STRUCT_1 tests
            switch (i) {
                case 0: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_1, Field1); break;
                case 1: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_1, Field2); break;
                case 2: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_1, Field3); break;
                case 3: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_1, Field4); break;
                case 4: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_1, Field5); break;
            }
        } else if (i < 8) {
            // TEST_STRUCT_3 tests
            switch (i - 5) {
                case 0: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_3, ByteField); break;
                case 1: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_3, EmbeddedStruct); break;
                case 2: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_3, WordField); break;
            }
        } else if (i < 12) {
            // TEST_STRUCT_PACKED tests
            switch (i - 8) {
                case 0: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_PACKED, PackedField1); break;
                case 1: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_PACKED, PackedField2); break;
                case 2: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_PACKED, PackedField3); break;
                case 3: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_PACKED, PackedField4); break;
            }
        } else if (i < 17) {
            // TEST_STRUCT_WEIRD tests
            switch (i - 12) {
                case 0: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_WEIRD, WeirdField1); break;
                case 1: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_WEIRD, WeirdField2); break;
                case 2: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_WEIRD, WeirdField3); break;
                case 3: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_WEIRD, WeirdField4); break;
                case 4: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_WEIRD, WeirdField5); break;
            }
        } else if (i < 19) {
            // TEST_STRUCT_FLEX tests
            switch (i - 17) {
                case 0: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_FLEX, FlexLength); break;
                case 1: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_FLEX, FlexData); break;
            }
        } else if (i < 22) {
            // TEST_STRUCT_NESTED_PACKED tests
            switch (i - 19) {
                case 0: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_NESTED_PACKED, OuterWord); break;
                case 1: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_NESTED_PACKED, InnerPacked); break;
                case 2: ActualOffset = MEMBER_OFFSET(TEST_STRUCT_NESTED_PACKED, OuterByte); break;
            }
        } else {
            // TEST_STRUCT_BITFIELDS tests
            ActualOffset = MEMBER_OFFSET(TEST_STRUCT_BITFIELDS, NormalField);
        }

        TestPassed = (ActualOffset == Test->ExpectedOffset);

        if (TestPassed) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test %d FAILED: %s"), i, Test->Description);
            DEBUG(TEXT("Expected offset: %d, got: %d"), Test->ExpectedOffset, ActualOffset);
        }
    }
}

/************************************************************************/

void TestHasMemberMacro(TEST_RESULTS* Results) {
    for (U32 i = 0; i < NumHasMemberTests; i++) {
        HAS_MEMBER_TEST* Test = &HasMemberTests[i];
        BOOL ActualHasMember = FALSE;
        BOOL TestPassed = FALSE;

        Results->TestsRun++;

        // Test different fields based on test index - each test is explicit
        switch (i) {
            // Field1 tests (offset 0)
            case 0: case 1: case 2:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_1, Field1, Test->StructSize);
                break;
            // Field2 tests (offset 2)
            case 3: case 4: case 5:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_1, Field2, Test->StructSize);
                break;
            // Field3 tests (offset 4)
            case 6: case 7:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_1, Field3, Test->StructSize);
                break;
            // Packed structure tests
            case 8:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_PACKED, PackedField1, Test->StructSize);
                break;
            case 9:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_PACKED, PackedField2, Test->StructSize);
                break;
            case 10:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_PACKED, PackedField2, Test->StructSize);
                break;
            // Weird alignment tests
            case 11:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_WEIRD, WeirdField2, Test->StructSize);
                break;
            case 12:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_WEIRD, WeirdField4, Test->StructSize);
                break;
            // Edge case
            case 13:
                ActualHasMember = HAS_MEMBER(TEST_STRUCT_1, Field1, Test->StructSize);
                break;
            default:
                ActualHasMember = FALSE;
                break;
        }

        TestPassed = (ActualHasMember == Test->ExpectedHasMember);

        if (TestPassed) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test %d FAILED: %s"), i, Test->TestDescription);
            DEBUG(TEXT("Expected: %s, got: %s"),
                  Test->ExpectedHasMember ? TEXT("TRUE") : TEXT("FALSE"),
                  ActualHasMember ? TEXT("TRUE") : TEXT("FALSE"));
        }
    }
}

/************************************************************************/

void TestMacros(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    TestMemberOffsetMacro(Results);
    TestHasMemberMacro(Results);
}
