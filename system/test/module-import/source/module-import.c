
/************************************************************************\

    EXOS Test program - Module Import
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


    Module Import - Loadable executable module import sample

\************************************************************************/

#include "../../../../runtime/include/exos.h"

/***************************************************************************/

EXOS_MODULE_IMPORT UINT ModuleSampleSharedValue(UINT Value);
EXOS_MODULE_IMPORT UINT ModuleSampleSubtract(UINT A, UINT B);
EXOS_MODULE_IMPORT UINT ModuleSampleMultiply(UINT A, UINT B);
EXOS_MODULE_IMPORT UINT ModuleSampleDivide(UINT A, UINT B);
EXOS_MODULE_IMPORT UINT ModuleSampleFactorial(UINT N);
EXOS_MODULE_IMPORT UINT ModuleSampleFibonacci(UINT N);
EXOS_MODULE_IMPORT UINT ModuleSampleGcd(UINT A, UINT B);
EXOS_MODULE_IMPORT UINT ModuleSamplePower(UINT Base, UINT Exp);

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportMathBasic(void) { return ModuleSampleSubtract(100, 37); }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportMathMultiply(void) { return ModuleSampleMultiply(12, 7); }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportMathDivide(void) { return ModuleSampleDivide(84, 6); }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportMathFactorialSum(void) {
    return ModuleSampleFactorial(4) + ModuleSampleFactorial(3);
}

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportMathFibonacci(void) { return ModuleSampleFibonacci(7); }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportMathGcd(void) { return ModuleSampleGcd(48, 18); }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportMathPower(void) { return ModuleSamplePower(2, 8); }

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportResolve(void) { return ModuleSampleSharedValue(0x34) + 0x09; }
