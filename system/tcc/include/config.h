/* TinyCC configuration for the EXOS userland bootstrap compiler. */

#ifndef TINYCC_CONFIG_H_INCLUDED
#define TINYCC_CONFIG_H_INCLUDED

#define TCC_VERSION "0.9.28rc-exos"
#define CC_NAME CC_gcc
#define GCC_MAJOR 0
#define GCC_MINOR 0

#if !(TCC_TARGET_I386 || TCC_TARGET_X86_64 || TCC_TARGET_ARM || TCC_TARGET_ARM64 || TCC_TARGET_RISCV64 || TCC_TARGET_C67)
#define TCC_TARGET_I386 1
#endif

#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_PREDEFS 0
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCCDIR "/system/apps/tcc"
#define CONFIG_TCC_SYSINCLUDEPATHS "/system/c/include:/system/apps/tcc/include"
#define CONFIG_TCC_LIBPATHS "/system/apps/tcc"
#define CONFIG_TCC_CRTPREFIX "/system/apps/tcc"
#define CONFIG_TCC_SWITCHES "-static -Wl,-Ttext=0x00400000 -Wl,-section-alignment=0x1000 -include tccdefs.h"
#ifdef TCC_TARGET_X86_64
#define CONFIG_TRIPLET "x86_64-exos"
#else
#define CONFIG_TRIPLET "i386-exos"
#endif
#define TCC_LIBTCC1 ""

#endif
