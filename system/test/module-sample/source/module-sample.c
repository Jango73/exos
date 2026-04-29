
/************************************************************************\

    EXOS Test program - Module Sample
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


    Module Sample - Loadable executable module build sample

\************************************************************************/

#include "../../../../runtime/include/exos/exos.h"

/***************************************************************************/

static EXOS_THREAD_LOCAL UINT ModuleSampleCallCount = 0;
static UINT ModuleSampleSharedCount = 0;

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleAdd(UINT Value) {
    ModuleSampleCallCount++;
    return Value + 0x07 + ModuleSampleCallCount;
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleSubtract(UINT A, UINT B) { return A - B; }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleMultiply(UINT A, UINT B) { return A * B; }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleDivide(UINT A, UINT B) {
    if (B == 0) return 0;
    return A / B;
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleFactorial(UINT N) {
    UINT Result = 1;
    UINT i;
    for (i = 2; i <= N; i++) {
        Result *= i;
    }
    return Result;
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleFibonacci(UINT N) {
    if (N == 0) return 0;
    if (N == 1) return 1;
    UINT A = 0;
    UINT B = 1;
    UINT C;
    UINT i;
    for (i = 2; i <= N; i++) {
        C = A + B;
        A = B;
        B = C;
    }
    return B;
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleGcd(UINT A, UINT B) {
    while (B != 0) {
        UINT T = B;
        B = A % B;
        A = T;
    }
    return A;
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSamplePower(UINT Base, UINT Exp) {
    UINT Result = 1;
    while (Exp > 0) {
        if (Exp & 1) {
            Result *= Base;
        }
        Base *= Base;
        Exp >>= 1;
    }
    return Result;
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleIncrementShared(void) {
    ModuleSampleSharedCount++;
    return ModuleSampleSharedCount;
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleGetShared(void) { return ModuleSampleSharedCount; }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleSharedValue(UINT Value) { return 0x1200 + Value; }
