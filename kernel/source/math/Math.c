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


    Math primitives

\************************************************************************/

#include "math/Math.h"

#include "core/KernelData.h"
#include "arch/intel/x86-Common.h"
#include "system/Clock.h"

/************************************************************************/

#define MATH_LUT_SIZE 16

/************************************************************************/

UINT RandomInteger(void) {
    static UINT Seed = 0;

    if (Seed == 0) {
        DATETIME t;
        if (!GetLocalTime(&t)) {
            Seed = (UINT)0xA5A5A5A5;
        } else {
            UINT seed = 0;

#ifdef __EXOS_64__
            seed ^= ((UINT)t.Year   * (UINT)11400714819323198485);
            seed ^= ((UINT)t.Month  << 60);
            seed ^= ((UINT)t.Day    << 54);
            seed ^= ((UINT)t.Hour   << 48);
            seed ^= ((UINT)t.Minute << 42);
            seed ^= ((UINT)t.Second << 36);
            seed ^= ((UINT)t.Milli  << 26);
#else
            seed ^= ((UINT)t.Year   * (UINT)2654435761);
            seed ^= ((UINT)t.Month  << 28);
            seed ^= ((UINT)t.Day    << 22);
            seed ^= ((UINT)t.Hour   << 17);
            seed ^= ((UINT)t.Minute << 11);
            seed ^= ((UINT)t.Second << 5);
            seed ^= (UINT)t.Milli;
#endif

            if (seed == 0) {
#ifdef __EXOS_64__
                seed = (UINT)11400714819323198485;
#else
                seed = (UINT)0x6D2B79F5;
#endif
            }

            Seed = seed;
        }
    }

#ifdef __EXOS_64__
    Seed = Seed * (UINT)6364136223846793005 + (UINT)1;
#else
    Seed = Seed * (UINT)1103515245 + (UINT)12345;
#endif

    return Seed;
}

/************************************************************************/

/**
 * @brief Normalize one F32 angle to [0, 2PI).
 * @param Radians Angle in radians.
 * @return Normalized angle.
 */
static F32 NormalizeRadiansPositiveF32(F32 Radians) {
    I32 Turns = (I32)(Radians / MATH_TWO_PI_F32);
    F32 Local = Radians - ((F32)Turns * MATH_TWO_PI_F32);

    while (Local < 0.0f) Local += MATH_TWO_PI_F32;
    while (Local >= MATH_TWO_PI_F32) Local -= MATH_TWO_PI_F32;
    return Local;
}

/************************************************************************/

/**
 * @brief Normalize one F64 angle to [0, 2PI).
 * @param Radians Angle in radians.
 * @return Normalized angle.
 */
static F64 NormalizeRadiansPositiveF64(F64 Radians) {
    INT Turns = (INT)(Radians / MATH_TWO_PI_F64);
    F64 Local = Radians - ((F64)Turns * MATH_TWO_PI_F64);

    while (Local < 0.0) Local += MATH_TWO_PI_F64;
    while (Local >= MATH_TWO_PI_F64) Local -= MATH_TWO_PI_F64;
    return Local;
}

/************************************************************************/

/**
 * @brief Normalize one F32 angle to [-PI, PI].
 * @param Radians Angle in radians.
 * @return Normalized angle.
 */
static F32 NormalizeRadiansSignedF32(F32 Radians) {
    F32 Local = NormalizeRadiansPositiveF32(Radians);
    if (Local > MATH_PI_F32) Local -= MATH_TWO_PI_F32;
    return Local;
}

/************************************************************************/

/**
 * @brief Normalize one F64 angle to [-PI, PI].
 * @param Radians Angle in radians.
 * @return Normalized angle.
 */
static F64 NormalizeRadiansSignedF64(F64 Radians) {
    F64 Local = NormalizeRadiansPositiveF64(Radians);
    if (Local > MATH_PI_F64) Local -= MATH_TWO_PI_F64;
    return Local;
}

/************************************************************************/

/**
 * @brief Evaluate one short sine polynomial on normalized F32 radians.
 * @param Radians Angle in [-PI, PI].
 * @return Approximate sine.
 */
static F32 SinPolynomialF32(F32 Radians) {
    F32 X = NormalizeRadiansSignedF32(Radians);
    F32 X2 = X * X;

    return X * (1.0f + X2 * (-0.16666667f + X2 * (0.0083333310f + X2 * (-0.0001984090f))));
}

/************************************************************************/

/**
 * @brief Evaluate one short cosine polynomial on normalized F32 radians.
 * @param Radians Angle in [-PI, PI].
 * @return Approximate cosine.
 */
static F32 CosPolynomialF32(F32 Radians) {
    F32 X = NormalizeRadiansSignedF32(Radians);
    F32 X2 = X * X;

    return 1.0f + X2 * (-0.5f + X2 * (0.041666638f + X2 * (-0.0013888378f)));
}

/************************************************************************/

/**
 * @brief Evaluate one short sine polynomial on normalized F64 radians.
 * @param Radians Angle in [-PI, PI].
 * @return Approximate sine.
 */
static F64 SinPolynomialF64(F64 Radians) {
    F64 X = NormalizeRadiansSignedF64(Radians);
    F64 X2 = X * X;

    return X * (1.0 + X2 * (-0.166666666666 + X2 * (0.008333333333 + X2 * (-0.000198412698))));
}

/************************************************************************/

/**
 * @brief Evaluate one short cosine polynomial on normalized F64 radians.
 * @param Radians Angle in [-PI, PI].
 * @return Approximate cosine.
 */
static F64 CosPolynomialF64(F64 Radians) {
    F64 X = NormalizeRadiansSignedF64(Radians);
    F64 X2 = X * X;

    return 1.0 + X2 * (-0.5 + X2 * (0.041666666666 + X2 * (-0.001388888888)));
}

/************************************************************************/

/**
 * @brief Lookup/interpolate cosine from fixed table.
 * @param Radians Angle in radians.
 * @return Approximate cosine.
 */
static F32 CosLookupF32(F32 Radians) {
    static const F32 CosTable[MATH_LUT_SIZE] = {
        1.0f, 0.9238795f, 0.7071068f, 0.3826834f,
        0.0f, -0.3826834f, -0.7071068f, -0.9238795f,
        -1.0f, -0.9238795f, -0.7071068f, -0.3826834f,
        0.0f, 0.3826834f, 0.7071068f, 0.9238795f};
    F32 Position;
    UINT Index;
    UINT NextIndex;
    F32 Fraction;
    F32 Angle;

    Angle = NormalizeRadiansPositiveF32(Radians);
    Position = (Angle * (F32)MATH_LUT_SIZE) / MATH_TWO_PI_F32;
    Index = (UINT)Position;
    NextIndex = (Index + 1) % MATH_LUT_SIZE;
    Fraction = Position - (F32)Index;

    return CosTable[Index % MATH_LUT_SIZE] +
           (CosTable[NextIndex] - CosTable[Index % MATH_LUT_SIZE]) * Fraction;
}

/************************************************************************/

/**
 * @brief Lookup/interpolate sine from fixed table.
 * @param Radians Angle in radians.
 * @return Approximate sine.
 */
static F32 SinLookupF32(F32 Radians) {
    return CosLookupF32(Radians - MATH_HALF_PI_F32);
}

/************************************************************************/

/**
 * @brief Return TRUE when CPU advertises one hardware FPU.
 * @return TRUE when x87 feature bit is present.
 */
BOOL MathHasHardwareFPU(void) {
    LPCPU_INFORMATION CpuInfo = GetKernelCPUInfo();

    if (CpuInfo == NULL) {
        return TRUE;
    }

    return (CpuInfo->Features & INTEL_CPU_FEAT_FPU) != 0;
}

/************************************************************************/

/**
 * @brief Compute sine for one F32 angle in radians.
 * @param Radians Angle in radians.
 * @return Approximate sine.
 */
F32 MathSinF32(F32 Radians) {
    if (MathHasHardwareFPU() != FALSE) {
        return SinPolynomialF32(Radians);
    }

    return SinLookupF32(Radians);
}

/************************************************************************/

/**
 * @brief Compute cosine for one F32 angle in radians.
 * @param Radians Angle in radians.
 * @return Approximate cosine.
 */
F32 MathCosF32(F32 Radians) {
    if (MathHasHardwareFPU() != FALSE) {
        return CosPolynomialF32(Radians);
    }

    return CosLookupF32(Radians);
}

/************************************************************************/

/**
 * @brief Compute sine for one F64 angle in radians.
 * @param Radians Angle in radians.
 * @return Approximate sine.
 */
F64 MathSinF64(F64 Radians) {
    if (MathHasHardwareFPU() != FALSE) {
        return SinPolynomialF64(Radians);
    }

    return (F64)MathSinF32((F32)Radians);
}

/************************************************************************/

/**
 * @brief Compute cosine for one F64 angle in radians.
 * @param Radians Angle in radians.
 * @return Approximate cosine.
 */
F64 MathCosF64(F64 Radians) {
    if (MathHasHardwareFPU() != FALSE) {
        return CosPolynomialF64(Radians);
    }

    return (F64)MathCosF32((F32)Radians);
}

/************************************************************************/

/**
 * @brief Compute square root for one F32 value.
 * @param Value Input value.
 * @return Square root or 0 for non-positive input.
 */
F32 MathSqrtF32(F32 Value) {
    F32 Guess;
    UINT Iteration;

    if (Value <= 0.0f) {
        return 0.0f;
    }

    Guess = (Value >= 1.0f) ? Value : 1.0f;
    for (Iteration = 0; Iteration < 8; Iteration++) {
        Guess = 0.5f * (Guess + (Value / Guess));
    }

    return Guess;
}

/************************************************************************/

/**
 * @brief Compute square root for one F64 value.
 * @param Value Input value.
 * @return Square root or 0 for non-positive input.
 */
F64 MathSqrtF64(F64 Value) {
    F64 Guess;
    UINT Iteration;

    if (Value <= 0.0) {
        return 0.0;
    }

    Guess = (Value >= 1.0) ? Value : 1.0;
    for (Iteration = 0; Iteration < 12; Iteration++) {
        Guess = 0.5 * (Guess + (Value / Guess));
    }

    return Guess;
}

/************************************************************************/
