
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


    Text

\************************************************************************/

#include "Base.h"

/***************************************************************************/

const STR Text_NewLine[] = "\n";
const STR Text_Space[] = " ";
const STR Text_Colon[] = ":";
const STR Text_0[] = "0";
const STR Text_Clk[] = "Clk";
const STR Text_Prefix_RAMDrive[] = "r";
const STR Text_Prefix_FloppyDrive[] = "f";
const STR Text_Prefix_USBDrive[] = "u";
const STR Text_Prefix_NVMe[] = "n";
const STR Text_Prefix_SATADrive[] = "s";
const STR Text_Prefix_ATADrive[] = "a";
const STR Text_Prefix_Drive[] = "d";
const STR Text_Dev[] = "dev";
const STR Text_Eth[] = "eth";
const STR Text_KB[] = "KB";
const STR Text_Exit[] = "Exit";
const STR Text_Image[] = "Image :";
const STR Text_Separator[] = "================\n";
const STR Text_Credits[] = "EXOS - Extensible Operating System\nDesign and architecture: Jango73\nCoding: Codex\nFixing: Jango73\n";

#ifdef __EXOS_ARCH_X86_32__
const STR Text_Architecture[] = "x86-32";
#endif

#ifdef __EXOS_ARCH_X86_64__
const STR Text_Architecture[] = "x86-64";
#endif
