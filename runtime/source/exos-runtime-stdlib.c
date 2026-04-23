/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime C Library Helpers

\************************************************************************/

#include "../../kernel/include/User.h"
#include "../include/exos-runtime.h"
#include "../include/exos.h"
#include "../include/exos-string.h"
#include "../include/fcntl.h"
#include "../include/sys/mman.h"
#include "../include/sys/stat.h"
#include "../include/sys/time.h"
#include "../include/time.h"

/************************************************************************/

int __exos_errno = 0;
extern PROCESS_INFO _ProcessInfo;

FILE* stdin = NULL;
FILE* stdout = NULL;
FILE* stderr = NULL;

static FILE RuntimeStandardInputStream;
static FILE RuntimeStandardOutputStream;
static FILE RuntimeStandardErrorStream;

/************************************************************************/

int* __errno_location(void) { return &__exos_errno; }

/************************************************************************/

static HANDLE RuntimeStandardHandleByDescriptor(int FileDescriptor) {
    if (FileDescriptor == STDIN_FILENO) {
        return _ProcessInfo.StdIn;
    }

    if (FileDescriptor == STDOUT_FILENO) {
        return _ProcessInfo.StdOut;
    }

    if (FileDescriptor == STDERR_FILENO) {
        return _ProcessInfo.StdErr;
    }

    return 0;
}

/************************************************************************/

static void RuntimeInitializeStandardStream(FILE* Stream, HANDLE Handle) {
    if (Stream == NULL) {
        return;
    }

    memset(Stream, 0, sizeof(FILE));
    Stream->_handle = Handle;
}

/************************************************************************/

void RuntimeInitializeStandardStreams(void) {
    RuntimeInitializeStandardStream(&RuntimeStandardInputStream, _ProcessInfo.StdIn);
    RuntimeInitializeStandardStream(&RuntimeStandardOutputStream, _ProcessInfo.StdOut);
    RuntimeInitializeStandardStream(&RuntimeStandardErrorStream, _ProcessInfo.StdErr);

    stdin = (_ProcessInfo.StdIn != 0) ? &RuntimeStandardInputStream : NULL;
    stdout = (_ProcessInfo.StdOut != 0) ? &RuntimeStandardOutputStream : NULL;
    stderr = (_ProcessInfo.StdErr != 0) ? &RuntimeStandardErrorStream : NULL;
}

/************************************************************************/

int atoi(const char* str) { return (int)strtol(str, NULL, 10); }

/************************************************************************/

static int RuntimeFileModeFromFlags(int Flags, char* Mode) {
    if ((Flags & O_RDWR) == O_RDWR) {
        Mode[0] = (Flags & O_TRUNC) ? 'w' : 'r';
        Mode[1] = '+';
        Mode[2] = 'b';
        Mode[3] = '\0';
        return 0;
    }

    if ((Flags & O_WRONLY) == O_WRONLY) {
        Mode[0] = (Flags & O_APPEND) ? 'a' : 'w';
        Mode[1] = 'b';
        Mode[2] = '\0';
        return 0;
    }

    Mode[0] = 'r';
    Mode[1] = 'b';
    Mode[2] = '\0';
    return 0;
}

/************************************************************************/

static void RuntimeFreeFileWrapper(FILE* File) {
    if (File->_base != NULL) {
        free(File->_base);
    }
    free(File);
}

/************************************************************************/

static int RuntimeReadHandle(HANDLE FileHandle, void* Buffer, unsigned Count) {
    FILE_OPERATION Operation;

    Operation.Header.Size = sizeof(Operation);
    Operation.Header.Version = EXOS_ABI_VERSION;
    Operation.Header.Flags = 0;
    Operation.File = FileHandle;
    Operation.NumBytes = Count;
    Operation.Buffer = Buffer;

    return (int)exoscall(SYSCALL_ReadFile, EXOS_PARAM(&Operation));
}

/************************************************************************/

static int RuntimeWriteHandle(HANDLE FileHandle, const void* Buffer, unsigned Count) {
    FILE_OPERATION Operation;

    Operation.Header.Size = sizeof(Operation);
    Operation.Header.Version = EXOS_ABI_VERSION;
    Operation.Header.Flags = 0;
    Operation.File = FileHandle;
    Operation.NumBytes = Count;
    Operation.Buffer = (void*)Buffer;

    return (int)exoscall(SYSCALL_WriteFile, EXOS_PARAM(&Operation));
}

/************************************************************************/

static int RuntimeSeekHandle(HANDLE FileHandle, long Offset, int Whence) {
    FILE_OPERATION Operation;
    long Current;
    long Size;
    long Target;
    UINT Result;

    Current = (long)exoscall(SYSCALL_GetFilePointer, EXOS_PARAM(FileHandle));
    Target = 0;

    switch (Whence) {
        case SEEK_SET:
            Target = Offset;
            break;
        case SEEK_CUR:
            Target = Current + Offset;
            break;
        case SEEK_END:
            Size = (long)exoscall(SYSCALL_GetFileSize, EXOS_PARAM(FileHandle));
            Target = Size + Offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (Target < 0) {
        errno = EINVAL;
        return -1;
    }

    Operation.Header.Size = sizeof(Operation);
    Operation.Header.Version = EXOS_ABI_VERSION;
    Operation.Header.Flags = 0;
    Operation.File = FileHandle;
    Operation.NumBytes = (U32)Target;
    Operation.Buffer = NULL;

    Result = (UINT)exoscall(SYSCALL_SetFilePointer, EXOS_PARAM(&Operation));
    if (Result != DF_RETURN_SUCCESS) {
        errno = EBADF;
        return -1;
    }

    return 0;
}

/************************************************************************/

static int RuntimeIsSpaceCharacter(int Character) {
    return Character == ' ' || Character == '\f' || Character == '\n' || Character == '\r' || Character == '\t' ||
           Character == '\v';
}

/************************************************************************/

static int RuntimeDigitValue(int Character) {
    if (Character >= '0' && Character <= '9') return Character - '0';
    if (Character >= 'A' && Character <= 'Z') return Character - 'A' + 10;
    if (Character >= 'a' && Character <= 'z') return Character - 'a' + 10;
    return -1;
}

/************************************************************************/

static const char* RuntimeSkipIntegerDigits(const char* Text, int Base) {
    int Digit;

    while ((Digit = RuntimeDigitValue((unsigned char)*Text)) >= 0 && Digit < Base) {
        Text++;
    }

    return Text;
}

/************************************************************************/

static const char* RuntimeParseIntegerPrefix(const char* Text, int* Base, int* Negative) {
    const char* Cursor;

    Cursor = Text;
    while (RuntimeIsSpaceCharacter((unsigned char)*Cursor)) {
        Cursor++;
    }

    *Negative = 0;
    if (*Cursor == '+' || *Cursor == '-') {
        *Negative = (*Cursor == '-');
        Cursor++;
    }

    if ((*Base == 0 || *Base == 16) && Cursor[0] == '0' && (Cursor[1] == 'x' || Cursor[1] == 'X')) {
        *Base = 16;
        Cursor += 2;
    } else if (*Base == 0 && Cursor[0] == '0') {
        *Base = 8;
    } else if (*Base == 0) {
        *Base = 10;
    }

    return Cursor;
}

/************************************************************************/

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    const char* Cursor;
    const char* FirstDigit;
    unsigned long Value;
    unsigned long Cutoff;
    int Cutlim;
    int Digit;
    int Negative;
    int AnyDigit;

    if (nptr == NULL || base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (endptr != NULL) *endptr = (char*)nptr;
        return 0;
    }

    Cursor = RuntimeParseIntegerPrefix(nptr, &base, &Negative);
    FirstDigit = Cursor;
    Value = 0;
    AnyDigit = 0;
    Cutoff = ULONG_MAX / (unsigned long)base;
    Cutlim = (int)(ULONG_MAX % (unsigned long)base);

    while ((Digit = RuntimeDigitValue((unsigned char)*Cursor)) >= 0 && Digit < base) {
        AnyDigit = 1;
        if (Value > Cutoff || (Value == Cutoff && Digit > Cutlim)) {
            errno = ERANGE;
            Value = ULONG_MAX;
            Cursor = RuntimeSkipIntegerDigits(Cursor, base);
            break;
        }
        Value = (Value * (unsigned long)base) + (unsigned long)Digit;
        Cursor++;
    }

    if (!AnyDigit) {
        Cursor = FirstDigit;
    }

    if (endptr != NULL) {
        *endptr = (char*)(AnyDigit ? Cursor : nptr);
    }

    return Negative ? (unsigned long)(0 - Value) : Value;
}

/************************************************************************/

long strtol(const char* nptr, char** endptr, int base) {
    const char* Cursor;
    const char* FirstDigit;
    unsigned long Value;
    unsigned long Limit;
    int Digit;
    int Negative;
    int AnyDigit;

    if (nptr == NULL || base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (endptr != NULL) *endptr = (char*)nptr;
        return 0;
    }

    Cursor = RuntimeParseIntegerPrefix(nptr, &base, &Negative);
    FirstDigit = Cursor;
    Value = 0;
    AnyDigit = 0;
    Limit = Negative ? ((unsigned long)LONG_MAX + 1) : (unsigned long)LONG_MAX;

    while ((Digit = RuntimeDigitValue((unsigned char)*Cursor)) >= 0 && Digit < base) {
        AnyDigit = 1;
        if (Value > (Limit / (unsigned long)base) ||
            (Value == (Limit / (unsigned long)base) && (unsigned long)Digit > (Limit % (unsigned long)base))) {
            errno = ERANGE;
            Value = Limit;
            Cursor = RuntimeSkipIntegerDigits(Cursor, base);
            break;
        }
        Value = (Value * (unsigned long)base) + (unsigned long)Digit;
        Cursor++;
    }

    if (!AnyDigit) {
        Cursor = FirstDigit;
    }

    if (endptr != NULL) {
        *endptr = (char*)(AnyDigit ? Cursor : nptr);
    }

    if (Negative) {
        return (Value == ((unsigned long)LONG_MAX + 1)) ? LONG_MIN : -(long)Value;
    }

    return (long)Value;
}

/************************************************************************/

long long strtoll(const char* nptr, char** endptr, int base) { return (long long)strtol(nptr, endptr, base); }

/************************************************************************/

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    return (unsigned long long)strtoul(nptr, endptr, base);
}

/************************************************************************/

long double strtold(const char* nptr, char** endptr) {
    const char* Cursor;
    long double Value;
    long double Scale;
    int Negative;
    int Exponent;
    int ExponentNegative;
    int AnyDigit;

    if (nptr == NULL) {
        if (endptr != NULL) *endptr = (char*)nptr;
        errno = EINVAL;
        return 0.0;
    }

    Cursor = nptr;
    while (RuntimeIsSpaceCharacter((unsigned char)*Cursor)) {
        Cursor++;
    }

    Negative = 0;
    if (*Cursor == '+' || *Cursor == '-') {
        Negative = (*Cursor == '-');
        Cursor++;
    }

    Value = 0.0;
    AnyDigit = 0;
    while (isdigit((unsigned char)*Cursor)) {
        AnyDigit = 1;
        Value = (Value * 10.0) + (long double)(*Cursor - '0');
        Cursor++;
    }

    if (*Cursor == '.') {
        Cursor++;
        Scale = 0.1;
        while (isdigit((unsigned char)*Cursor)) {
            AnyDigit = 1;
            Value += (long double)(*Cursor - '0') * Scale;
            Scale *= 0.1;
            Cursor++;
        }
    }

    if (AnyDigit && (*Cursor == 'e' || *Cursor == 'E')) {
        const char* ExponentStart;

        Cursor++;
        ExponentStart = Cursor;
        ExponentNegative = 0;
        if (*Cursor == '+' || *Cursor == '-') {
            ExponentNegative = (*Cursor == '-');
            Cursor++;
        }

        Exponent = 0;
        if (!isdigit((unsigned char)*Cursor)) {
            Cursor = ExponentStart - 1;
        } else {
            while (isdigit((unsigned char)*Cursor)) {
                Exponent = (Exponent * 10) + (*Cursor - '0');
                Cursor++;
            }
            while (Exponent > 0) {
                Value = ExponentNegative ? (Value / 10.0) : (Value * 10.0);
                Exponent--;
            }
        }
    }

    if (endptr != NULL) {
        *endptr = (char*)(AnyDigit ? Cursor : nptr);
    }

    return Negative ? -Value : Value;
}

/************************************************************************/

double strtod(const char* nptr, char** endptr) { return (double)strtold(nptr, endptr); }

/************************************************************************/

float strtof(const char* nptr, char** endptr) { return (float)strtold(nptr, endptr); }

/************************************************************************/

int isascii(int character) { return ((unsigned int)character) <= 0x7F; }

/************************************************************************/

int isblank(int character) { return character == ' ' || character == '\t'; }

/************************************************************************/

int iscntrl(int character) { return (character >= 0 && character < 0x20) || character == 0x7F; }

/************************************************************************/

int isdigit(int character) { return character >= '0' && character <= '9'; }

/************************************************************************/

int islower(int character) { return character >= 'a' && character <= 'z'; }

/************************************************************************/

int isupper(int character) { return character >= 'A' && character <= 'Z'; }

/************************************************************************/

int isalpha(int character) { return islower(character) || isupper(character); }

/************************************************************************/

int isalnum(int character) { return isalpha(character) || isdigit(character); }

/************************************************************************/

int isspace(int character) { return RuntimeIsSpaceCharacter(character); }

/************************************************************************/

int isxdigit(int character) {
    return isdigit(character) || (character >= 'A' && character <= 'F') || (character >= 'a' && character <= 'f');
}

/************************************************************************/

int isprint(int character) { return character >= 0x20 && character <= 0x7E; }

/************************************************************************/

int isgraph(int character) { return character >= 0x21 && character <= 0x7E; }

/************************************************************************/

int ispunct(int character) { return isgraph(character) && !isalnum(character); }

/************************************************************************/

int toascii(int character) { return character & 0x7F; }

/************************************************************************/

int tolower(int character) { return isupper(character) ? (character + ('a' - 'A')) : character; }

/************************************************************************/

int toupper(int character) { return islower(character) ? (character - ('a' - 'A')) : character; }

/************************************************************************/

static void RuntimeSwapBytes(unsigned char* Left, unsigned char* Right, size_t Size) {
    unsigned char Temporary;
    size_t Index;

    for (Index = 0; Index < Size; Index++) {
        Temporary = Left[Index];
        Left[Index] = Right[Index];
        Right[Index] = Temporary;
    }
}

/************************************************************************/

void qsort(void* base, size_t num, size_t size, qsort_comparator compare) {
    unsigned char* Bytes;
    size_t Gap;
    size_t Index;
    size_t Scan;

    if (base == NULL || compare == NULL || num < 2 || size == 0) {
        return;
    }

    Bytes = (unsigned char*)base;
    for (Gap = num / 2; Gap > 0; Gap /= 2) {
        for (Index = Gap; Index < num; Index++) {
            Scan = Index;
            while (Scan >= Gap && compare(Bytes + ((Scan - Gap) * size), Bytes + (Scan * size)) > 0) {
                RuntimeSwapBytes(Bytes + ((Scan - Gap) * size), Bytes + (Scan * size), size);
                Scan -= Gap;
            }
        }
    }
}

/************************************************************************/

int remove(const char* path) {
    UNUSED(path);
    errno = ENOSYS;
    return -1;
}

/************************************************************************/

int rename(const char* old_path, const char* new_path) {
    UNUSED(old_path);
    UNUSED(new_path);
    errno = ENOSYS;
    return -1;
}

/************************************************************************/

int stat(const char* path, struct stat* info) {
    FILE* File;
    long int Size;

    if (path == NULL || info == NULL) {
        errno = EINVAL;
        return -1;
    }

    File = fopen(path, "rb");
    if (File == NULL) {
        errno = ENOENT;
        return -1;
    }

    Size = (long int)exoscall(SYSCALL_GetFileSize, EXOS_PARAM(File->_handle));
    fclose(File);

    memset(info, 0, sizeof(struct stat));
    info->st_mode = S_IFREG;
    info->st_size = (off_t)Size;
    return 0;
}

/************************************************************************/

int open(const char* path, int flags, ...) {
    char Mode[4];
    FILE* File;
    int FileDescriptor;

    RuntimeFileModeFromFlags(flags, Mode);
    File = fopen(path, Mode);
    if (File == NULL) {
        errno = ENOENT;
        return -1;
    }

    // TinyCC and POSIX-style code use a 32-bit C int file descriptor, while
    // EXOS already exposes process-local handles. The descriptor is therefore
    // the EXOS handle value, not the FILE* wrapper pointer; casting FILE* to
    // int truncates pointers on x86-64.
    FileDescriptor = (int)File->_handle;
    RuntimeFreeFileWrapper(File);
    return FileDescriptor;
}

/************************************************************************/

int close(int file_descriptor) {
    HANDLE FileHandle = 0;

    if (file_descriptor < 0) {
        errno = EBADF;
        return -1;
    }

    FileHandle = RuntimeStandardHandleByDescriptor(file_descriptor);
    if (file_descriptor <= STDERR_FILENO && FileHandle == 0) {
        errno = EBADF;
        return -1;
    }

    if (FileHandle == 0) {
        FileHandle = (HANDLE)file_descriptor;
    }

    if (FileHandle == 0) {
        errno = EBADF;
        return -1;
    }

    exoscall(SYSCALL_DeleteObject, EXOS_PARAM(FileHandle));
    return 0;
}

/************************************************************************/

int read(int file_descriptor, void* buffer, unsigned count) {
    HANDLE FileHandle = 0;

    if (file_descriptor < 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    FileHandle = RuntimeStandardHandleByDescriptor(file_descriptor);
    if (file_descriptor <= STDERR_FILENO && FileHandle == 0) {
        errno = EBADF;
        return -1;
    }

    if (FileHandle == 0) {
        FileHandle = (HANDLE)file_descriptor;
    }

    if (FileHandle == 0) {
        errno = EBADF;
        return -1;
    }

    return RuntimeReadHandle(FileHandle, buffer, count);
}

/************************************************************************/

int write(int file_descriptor, const void* buffer, unsigned count) {
    HANDLE FileHandle = 0;

    if (file_descriptor < 0 || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    FileHandle = RuntimeStandardHandleByDescriptor(file_descriptor);
    if ((file_descriptor == STDOUT_FILENO || file_descriptor == STDERR_FILENO) && FileHandle == 0) {
        char Text[MAX_STRING_BUFFER];
        unsigned CopyCount = (count < (MAX_STRING_BUFFER - 1)) ? count : (MAX_STRING_BUFFER - 1);

        memmove(Text, buffer, CopyCount);
        Text[CopyCount] = '\0';
        printf("%s", Text);
        return (int)count;
    }

    if (FileHandle == 0) {
        if (file_descriptor <= STDERR_FILENO) {
            errno = EBADF;
            return -1;
        }
        FileHandle = (HANDLE)file_descriptor;
    }

    if (FileHandle == 0) {
        errno = EBADF;
        return -1;
    }

    return RuntimeWriteHandle(FileHandle, buffer, count);
}

/************************************************************************/

long lseek(int file_descriptor, long offset, int whence) {
    HANDLE FileHandle = 0;

    if (file_descriptor < 0) {
        errno = EBADF;
        return -1;
    }

    FileHandle = RuntimeStandardHandleByDescriptor(file_descriptor);
    if (file_descriptor <= STDERR_FILENO && FileHandle == 0) {
        errno = EBADF;
        return -1;
    }

    if (FileHandle == 0) {
        FileHandle = (HANDLE)file_descriptor;
    }

    if (FileHandle == 0) {
        errno = EBADF;
        return -1;
    }

    if (RuntimeSeekHandle(FileHandle, offset, whence) != 0) {
        return -1;
    }

    return (long)exoscall(SYSCALL_GetFilePointer, EXOS_PARAM(FileHandle));
}

/************************************************************************/

FILE* fdopen(int file_descriptor, const char* mode) {
    UNUSED(mode);
    FILE* File;
    HANDLE FileHandle = 0;

    if (file_descriptor < 0) {
        errno = EBADF;
        return NULL;
    }

    FileHandle = RuntimeStandardHandleByDescriptor(file_descriptor);
    if (file_descriptor <= STDERR_FILENO && FileHandle == 0) {
        errno = EBADF;
        return NULL;
    }

    if (FileHandle == 0) {
        FileHandle = (HANDLE)file_descriptor;
    }

    File = (FILE*)malloc(sizeof(FILE));
    if (File == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    // fdopen creates the FILE* stream facade only for callers that explicitly
    // ask for stream I/O. The underlying descriptor remains the EXOS handle.
    memset(File, 0, sizeof(FILE));
    File->_handle = (unsigned)FileHandle;
    return File;
}

/************************************************************************/

FILE* freopen(const char* path, const char* mode, FILE* fp) {
    UNUSED(fp);
    return fopen(path, mode);
}

/************************************************************************/

int pipe(int pipefd[2]) {
    PIPE_INFO Info;
    UINT Result;

    if (pipefd == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&Info, 0, sizeof(Info));
    Info.Header.Size = sizeof(PIPE_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.ReadHandle = 0;
    Info.WriteHandle = 0;

    Result = (UINT)exoscall(SYSCALL_CreatePipe, EXOS_PARAM(&Info));
    if (Result != DF_RETURN_SUCCESS || Info.ReadHandle == 0 || Info.WriteHandle == 0) {
        errno = EIO;
        return -1;
    }

    pipefd[0] = (int)Info.ReadHandle;
    pipefd[1] = (int)Info.WriteHandle;
    return 0;
}

/************************************************************************/

int fputs(const char* text, FILE* fp) {
    if (text == NULL) {
        errno = EINVAL;
        return EOF;
    }

    if (fp == NULL) {
        return printf("%s", text);
    }

    return (int)fwrite(text, 1, strlen(text), fp);
}

/************************************************************************/

int fputc(int character, FILE* fp) {
    unsigned char Byte;

    Byte = (unsigned char)character;
    if (fp == NULL) {
        printf("%c", character);
        return character;
    }

    return fwrite(&Byte, 1, 1, fp) == 1 ? character : EOF;
}

/************************************************************************/

int vfprintf(FILE* fp, const char* fmt, va_list args) {
    char Buffer[MAX_STRING_BUFFER];

    StringPrintFormatArgs((LPSTR)Buffer, (LPCSTR)fmt, args);
    return fputs(Buffer, fp);
}

/************************************************************************/

int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args) {
    char Temporary[MAX_STRING_BUFFER];
    unsigned Length;
    unsigned CopyLength;

    StringPrintFormatArgs((LPSTR)Temporary, (LPCSTR)fmt, args);
    Length = strlen(Temporary);

    if (buffer != NULL && size > 0) {
        CopyLength = (Length < (size - 1)) ? Length : (unsigned)(size - 1);
        memmove(buffer, Temporary, CopyLength);
        buffer[CopyLength] = '\0';
    }

    return (int)Length;
}

/************************************************************************/

int unlink(const char* path) { return remove(path); }

/************************************************************************/

char* realpath(const char* path, char* resolved_path) {
    size_t Length;
    char* Result;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }

    Result = resolved_path;
    if (Result == NULL) {
        Length = strlen(path) + 1;
        Result = (char*)malloc(Length);
        if (Result == NULL) {
            errno = ENOMEM;
            return NULL;
        }
    }

    strcpy(Result, path);
    return Result;
}

/************************************************************************/

char* strpbrk(const char* text, const char* accept) {
    const char* Cursor;

    if (text == NULL || accept == NULL) {
        return NULL;
    }

    while (*text != '\0') {
        Cursor = accept;
        while (*Cursor != '\0') {
            if (*text == *Cursor) {
                return (char*)text;
            }
            Cursor++;
        }
        text++;
    }

    return NULL;
}

/************************************************************************/

int execvp(const char* file, char* const arguments[]) {
    UNUSED(file);
    UNUSED(arguments);
    errno = ENOSYS;
    return -1;
}

/************************************************************************/

char* strerror(int error) {
    switch (error) {
        case EPERM:
            return "operation not permitted";
        case ENOENT:
            return "not found";
        case EIO:
            return "input output error";
        case EBADF:
            return "bad file descriptor";
        case ENOMEM:
            return "not enough memory";
        case EACCES:
            return "access denied";
        case EEXIST:
            return "already exists";
        case EINVAL:
            return "invalid argument";
        case ERANGE:
            return "range error";
        case ENOSYS:
            return "not implemented";
        default:
            return "error";
    }
}

/************************************************************************/

char* getenv(const char* name) {
    UNUSED(name);
    return NULL;
}

/************************************************************************/

long double ldexpl(long double value, int exponent) {
    while (exponent > 0) {
        value *= 2.0;
        exponent--;
    }

    while (exponent < 0) {
        value *= 0.5;
        exponent++;
    }

    return value;
}

/************************************************************************/

void* mmap(void* address, size_t length, int protection, int flags, int file_descriptor, off_t offset) {
    UNUSED(address);
    UNUSED(protection);
    UNUSED(flags);
    UNUSED(file_descriptor);
    UNUSED(offset);

    if (length == 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    return malloc(length);
}

/************************************************************************/

int munmap(void* address, size_t length) {
    UNUSED(length);
    free(address);
    return 0;
}

/************************************************************************/

int mprotect(void* address, size_t length, int protection) {
    UNUSED(address);
    UNUSED(length);
    UNUSED(protection);
    return 0;
}

/************************************************************************/

int gettimeofday(struct timeval* time_value, struct timezone* time_zone) {
    if (time_value != NULL) {
        time_value->tv_sec = 0;
        time_value->tv_usec = 0;
    }

    if (time_zone != NULL) {
        time_zone->tz_minuteswest = 0;
        time_zone->tz_dsttime = 0;
    }

    return 0;
}

/************************************************************************/

time_t time(time_t* value) {
    if (value != NULL) {
        *value = 0;
    }

    return 0;
}

/************************************************************************/

struct tm* localtime(const time_t* value) {
    static struct tm LocalTime;

    UNUSED(value);

    LocalTime.tm_sec = 0;
    LocalTime.tm_min = 0;
    LocalTime.tm_hour = 0;
    LocalTime.tm_mday = 1;
    LocalTime.tm_mon = 0;
    LocalTime.tm_year = 126;
    LocalTime.tm_wday = 4;
    LocalTime.tm_yday = 0;
    LocalTime.tm_isdst = 0;

    return &LocalTime;
}

/************************************************************************/

static int RuntimeScanInteger(const char** text, int* value_out) {
    const char* Cursor;
    int Negative;
    int Value;
    int AnyDigit;

    Cursor = *text;
    Negative = 0;
    Value = 0;
    AnyDigit = 0;

    if (*Cursor == '+' || *Cursor == '-') {
        Negative = (*Cursor == '-');
        Cursor++;
    }

    while (isdigit((unsigned char)*Cursor)) {
        AnyDigit = 1;
        Value = (Value * 10) + (*Cursor - '0');
        Cursor++;
    }

    if (!AnyDigit) {
        return 0;
    }

    *text = Cursor;
    *value_out = Negative ? -Value : Value;
    return 1;
}

/************************************************************************/

int sscanf(const char* str, const char* fmt, ...) {
    va_list Args;
    const char* TextCursor;
    const char* FormatCursor;
    int Assignments;

    if (str == NULL || fmt == NULL) {
        errno = EINVAL;
        return 0;
    }

    TextCursor = str;
    FormatCursor = fmt;
    Assignments = 0;
    va_start(Args, fmt);

    while (*FormatCursor != '\0') {
        if (RuntimeIsSpaceCharacter((unsigned char)*FormatCursor)) {
            while (RuntimeIsSpaceCharacter((unsigned char)*FormatCursor)) FormatCursor++;
            while (RuntimeIsSpaceCharacter((unsigned char)*TextCursor)) TextCursor++;
            continue;
        }

        if (*FormatCursor != '%') {
            if (*TextCursor != *FormatCursor) break;
            TextCursor++;
            FormatCursor++;
            continue;
        }

        FormatCursor++;
        if (*FormatCursor == 'd' || *FormatCursor == 'u') {
            int Value;
            int* ValuePointer;

            while (RuntimeIsSpaceCharacter((unsigned char)*TextCursor)) TextCursor++;
            if (!RuntimeScanInteger(&TextCursor, &Value)) break;
            ValuePointer = va_arg(Args, int*);
            *ValuePointer = Value;
            Assignments++;
            FormatCursor++;
            continue;
        }

        break;
    }

    va_end(Args);
    return Assignments;
}
