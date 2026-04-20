
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

#include "../../../../runtime/include/exos.h"

/***************************************************************************/

static EXOS_THREAD_LOCAL UINT ModuleSampleCallCount = 0;
static UINT ModuleSampleSharedCount = 0;

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleSampleAdd(UINT Value) {
    ModuleSampleCallCount++;
    return Value + 0x07 + ModuleSampleCallCount;
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
