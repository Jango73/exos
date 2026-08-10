
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


    TOML

\************************************************************************/
#ifndef TOML_H_INCLUDED
#define TOML_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

// TomlParse allocates the TOML structure, the whole TOML_ITEM array and one
// contiguous string area in a single block. Key and Value point inside that
// block, and TomlFree releases everything with one free. The items are
// read-only after parsing.
typedef struct tag_TOMLITEM {
    LPSTR Key;
    LPSTR Value;
    struct tag_TOMLITEM* Next;
} TOML_ITEM, *LPTOMLITEM;

typedef struct tag_TOML {
    LPTOMLITEM First;
} TOML, *LPTOML;

/***************************************************************************/

LPTOML TomlParse(LPCSTR Source);
LPCSTR TomlGet(LPTOML Toml, LPCSTR Path);
void TomlFree(LPTOML Toml);

/***************************************************************************/

#endif
