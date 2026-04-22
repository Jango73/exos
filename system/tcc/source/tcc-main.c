/************************************************************************\

    EXOS TinyCC Bootstrap
    Copyright (c) 1999-2026 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

\************************************************************************/

#include "../../../runtime/include/exos-runtime.h"
#include "../../../runtime/include/setjmp.h"

#define stdin NULL
#define stdout NULL
#define stderr NULL
#define environ NULL
#define main TinyCcMain

#define EXOS_TCC_DEFAULT_SOURCE_WARNING_LIMIT (256 * 1024)
#define EXOS_TCC_DEFAULT_SOURCE_HARD_LIMIT (1024 * 1024)

typedef struct tag_EXOS_TCC_ALLOCATION_HEADER {
    size_t Size;
} EXOS_TCC_ALLOCATION_HEADER;

typedef struct tag_EXOS_TCC_STATE {
    size_t HeapLimit;
    size_t HeapUsed;
    size_t HeapPeak;
    size_t SourceWarningLimit;
    size_t SourceHardLimit;
    int SourceWarningIssued;
} EXOS_TCC_STATE;

static EXOS_TCC_STATE ExosTinyCcState;

/************************************************************************/

void longjmp(jmp_buf Environment, int Value) {
    UNUSED(Value);
    __builtin_longjmp(Environment, 1);
}

/************************************************************************/

#include "../../../third/tinycc/tcc.c"

#undef main
#undef malloc
#undef realloc
#undef free
#undef strdup

/************************************************************************/

static int ExosTinyCcStringStartsWith(const char* Text, const char* Prefix) {
    size_t PrefixLength;

    if (Text == NULL || Prefix == NULL) {
        return 0;
    }

    PrefixLength = strlen(Prefix);
    return strncmp(Text, Prefix, PrefixLength) == 0;
}

/************************************************************************/

static int ExosTinyCcStringEndsWith(const char* Text, const char* Suffix) {
    size_t TextLength;
    size_t SuffixLength;

    if (Text == NULL || Suffix == NULL) {
        return 0;
    }

    TextLength = strlen(Text);
    SuffixLength = strlen(Suffix);

    if (TextLength < SuffixLength) {
        return 0;
    }

    return strcmp(Text + TextLength - SuffixLength, Suffix) == 0;
}

/************************************************************************/

static size_t ExosTinyCcParseSize(const char* Text, int* IsValid) {
    char* End;
    unsigned long Value;

    *IsValid = 0;

    if (Text == NULL || Text[0] == '\0') {
        return 0;
    }

    Value = strtoul(Text, &End, 10);
    if (End == Text || *End != '\0' || Value == 0) {
        return 0;
    }

    *IsValid = 1;
    return (size_t)Value;
}

/************************************************************************/

static void ExosTinyCcPrintInvalidOption(const char* Option) {
    printf("EXOS TinyCC: invalid numeric option: %s\n", Option);
}

/************************************************************************/

static int ExosTinyCcApplyOption(char* Argument) {
    static const char HeapLimitPrefix[] = "--heap-limit=";
    static const char SourceLimitPrefix[] = "--source-limit=";
    static const char SourceWarningPrefix[] = "--source-warning=";
    int IsValid;
    size_t Value;

    if (ExosTinyCcStringStartsWith(Argument, HeapLimitPrefix)) {
        Value = ExosTinyCcParseSize(Argument + strlen(HeapLimitPrefix), &IsValid);
        if (!IsValid) {
            ExosTinyCcPrintInvalidOption(Argument);
            exit(1);
        }
        ExosTinyCcState.HeapLimit = Value;
        return 1;
    }

    if (ExosTinyCcStringStartsWith(Argument, SourceLimitPrefix)) {
        Value = ExosTinyCcParseSize(Argument + strlen(SourceLimitPrefix), &IsValid);
        if (!IsValid) {
            ExosTinyCcPrintInvalidOption(Argument);
            exit(1);
        }
        ExosTinyCcState.SourceHardLimit = Value;
        return 1;
    }

    if (ExosTinyCcStringStartsWith(Argument, SourceWarningPrefix)) {
        Value = ExosTinyCcParseSize(Argument + strlen(SourceWarningPrefix), &IsValid);
        if (!IsValid) {
            ExosTinyCcPrintInvalidOption(Argument);
            exit(1);
        }
        ExosTinyCcState.SourceWarningLimit = Value;
        return 1;
    }

    return 0;
}

/************************************************************************/

static size_t ExosTinyCcParseOptionValue(const char* Option, const char* Value) {
    int IsValid;
    size_t Size;

    Size = ExosTinyCcParseSize(Value, &IsValid);
    if (!IsValid) {
        ExosTinyCcPrintInvalidOption(Option);
        exit(1);
    }

    return Size;
}

/************************************************************************/

static void ExosTinyCcFilterOptions(int* ArgumentCount, char** Arguments) {
    int ReadIndex;
    int WriteIndex;

    WriteIndex = 0;
    for (ReadIndex = 0; ReadIndex < *ArgumentCount; ReadIndex++) {
        if (ReadIndex > 0 && strcmp(Arguments[ReadIndex], "--heap-limit") == 0) {
            if (ReadIndex + 1 >= *ArgumentCount) {
                ExosTinyCcPrintInvalidOption(Arguments[ReadIndex]);
                exit(1);
            }
            ExosTinyCcState.HeapLimit = ExosTinyCcParseOptionValue(Arguments[ReadIndex], Arguments[ReadIndex + 1]);
            ReadIndex++;
            continue;
        }

        if (ReadIndex > 0 && strcmp(Arguments[ReadIndex], "--source-limit") == 0) {
            if (ReadIndex + 1 >= *ArgumentCount) {
                ExosTinyCcPrintInvalidOption(Arguments[ReadIndex]);
                exit(1);
            }
            ExosTinyCcState.SourceHardLimit =
                ExosTinyCcParseOptionValue(Arguments[ReadIndex], Arguments[ReadIndex + 1]);
            ReadIndex++;
            continue;
        }

        if (ReadIndex > 0 && strcmp(Arguments[ReadIndex], "--source-warning") == 0) {
            if (ReadIndex + 1 >= *ArgumentCount) {
                ExosTinyCcPrintInvalidOption(Arguments[ReadIndex]);
                exit(1);
            }
            ExosTinyCcState.SourceWarningLimit =
                ExosTinyCcParseOptionValue(Arguments[ReadIndex], Arguments[ReadIndex + 1]);
            ReadIndex++;
            continue;
        }

        if (ReadIndex > 0 && ExosTinyCcApplyOption(Arguments[ReadIndex])) {
            continue;
        }

        Arguments[WriteIndex] = Arguments[ReadIndex];
        WriteIndex++;
    }

    Arguments[WriteIndex] = NULL;
    *ArgumentCount = WriteIndex;
}

/************************************************************************/

static long ExosTinyCcGetFileSize(const char* Path) {
    FILE* File;
    long Size;

    File = fopen(Path, "rb");
    if (File == NULL) {
        return -1;
    }

    Size = (long)exoscall(SYSCALL_GetFileSize, (uint_t)File->_handle);
    fclose(File);
    return Size;
}

/************************************************************************/

static void ExosTinyCcCheckSourceFile(const char* Path) {
    long FileSize;

    if (!ExosTinyCcStringEndsWith(Path, ".c")) {
        return;
    }

    FileSize = ExosTinyCcGetFileSize(Path);
    if (FileSize < 0) {
        return;
    }

    if ((size_t)FileSize > ExosTinyCcState.SourceHardLimit) {
        printf(
            "EXOS TinyCC: source file too large: %s is %u bytes, limit is %u bytes\n", Path, (unsigned)FileSize,
            (unsigned)ExosTinyCcState.SourceHardLimit);
        exit(1);
    }

    if (!ExosTinyCcState.SourceWarningIssued && (size_t)FileSize > ExosTinyCcState.SourceWarningLimit) {
        printf(
            "EXOS TinyCC: source file is large: %s is %u bytes, warning limit is %u bytes\n", Path, (unsigned)FileSize,
            (unsigned)ExosTinyCcState.SourceWarningLimit);
        ExosTinyCcState.SourceWarningIssued = 1;
    }
}

/************************************************************************/

static void ExosTinyCcCheckSourceFiles(int ArgumentCount, char** Arguments) {
    int Index;

    for (Index = 1; Index < ArgumentCount; Index++) {
        if (Arguments[Index][0] == '-') {
            continue;
        }

        ExosTinyCcCheckSourceFile(Arguments[Index]);
    }
}

/************************************************************************/

static void* ExosTinyCcReallocate(void* Pointer, unsigned long Size) {
    EXOS_TCC_ALLOCATION_HEADER* Header;
    size_t OldSize;
    size_t NewUsed;
    void* Allocation;

    OldSize = 0;
    Header = NULL;

    if (Pointer != NULL) {
        Header = ((EXOS_TCC_ALLOCATION_HEADER*)Pointer) - 1;
        OldSize = Header->Size;
    }

    if (Size == 0) {
        if (Header != NULL) {
            ExosTinyCcState.HeapUsed -= OldSize;
            free(Header);
        }
        return NULL;
    }

    NewUsed = ExosTinyCcState.HeapUsed - OldSize + (size_t)Size;
    if (ExosTinyCcState.HeapLimit > 0 && NewUsed > ExosTinyCcState.HeapLimit) {
        printf(
            "EXOS TinyCC: heap limit exceeded: requested %u bytes, used %u bytes, limit is %u bytes\n", (unsigned)Size,
            (unsigned)ExosTinyCcState.HeapUsed, (unsigned)ExosTinyCcState.HeapLimit);
        exit(1);
    }

    Allocation = realloc(Header, sizeof(EXOS_TCC_ALLOCATION_HEADER) + (size_t)Size);
    if (Allocation == NULL) {
        printf(
            "EXOS TinyCC: memory allocation failed: requested %u bytes, used %u bytes\n", (unsigned)Size,
            (unsigned)ExosTinyCcState.HeapUsed);
        exit(1);
    }

    Header = (EXOS_TCC_ALLOCATION_HEADER*)Allocation;
    Header->Size = (size_t)Size;
    ExosTinyCcState.HeapUsed = NewUsed;
    if (ExosTinyCcState.HeapUsed > ExosTinyCcState.HeapPeak) {
        ExosTinyCcState.HeapPeak = ExosTinyCcState.HeapUsed;
    }

    return Header + 1;
}

/************************************************************************/

int main(int ArgumentCount, char** Arguments) {
    ExosTinyCcState.HeapLimit = 0;
    ExosTinyCcState.HeapUsed = 0;
    ExosTinyCcState.HeapPeak = 0;
    ExosTinyCcState.SourceWarningLimit = EXOS_TCC_DEFAULT_SOURCE_WARNING_LIMIT;
    ExosTinyCcState.SourceHardLimit = EXOS_TCC_DEFAULT_SOURCE_HARD_LIMIT;
    ExosTinyCcState.SourceWarningIssued = 0;

    ExosTinyCcFilterOptions(&ArgumentCount, Arguments);
    ExosTinyCcCheckSourceFiles(ArgumentCount, Arguments);
    tcc_set_realloc(ExosTinyCcReallocate);

    return TinyCcMain(ArgumentCount, Arguments);
}
