
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

/***************************************************************************/

EXOS_MODULE_EXPORT UINT ModuleImportResolve(void) { return ModuleSampleSharedValue(0x34) + 0x09; }
