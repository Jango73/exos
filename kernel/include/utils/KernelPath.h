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


    Kernel logical path resolver

\************************************************************************/

#ifndef KERNELPATH_H_INCLUDED
#define KERNELPATH_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define KERNEL_PATH_CONFIG_PREFIX TEXT("KernelPath.")

#define KERNEL_PATH_KEY_USERS_DATABASE TEXT("UsersDatabase")
#define KERNEL_PATH_KEY_KEYBOARD_LAYOUTS TEXT("KeyboardLayouts")
#define KERNEL_PATH_KEY_USERS_ROOT TEXT("UsersRoot")
#define KERNEL_PATH_KEY_CURRENT_USER_ALIAS TEXT("CurrentUserAlias")
#define KERNEL_PATH_KEY_PRIVATE_PACKAGE_ALIAS TEXT("PrivatePackageAlias")
#define KERNEL_PATH_KEY_PRIVATE_USER_DATA_ALIAS TEXT("PrivateUserDataAlias")
#define KERNEL_PATH_KEY_SYSTEM_APPS_ROOT TEXT("SystemAppsRoot")
#define KERNEL_PATH_LIST_ENTRY_PATH_FORMAT TEXT("KernelPath.%s.%u.Path")

#define KERNEL_PATH_LIST_BINARY 0x00000001
#define KERNEL_PATH_LIST_IMAGE 0x00000002

#define KERNEL_PATH_DEFAULT_USERS_DATABASE TEXT("/system/data/users.database")
#define KERNEL_PATH_DEFAULT_KEYBOARD_LAYOUTS TEXT("/system/keyboard")
#define KERNEL_PATH_DEFAULT_USERS_ROOT TEXT("/users")
#define KERNEL_PATH_DEFAULT_CURRENT_USER_ALIAS TEXT("/current-user")
#define KERNEL_PATH_DEFAULT_PRIVATE_PACKAGE_ALIAS TEXT("/package")
#define KERNEL_PATH_DEFAULT_PRIVATE_USER_DATA_ALIAS TEXT("/user-data")
#define KERNEL_PATH_DEFAULT_SYSTEM_APPS_ROOT TEXT("/system/apps")
#define KERNEL_PATH_DEFAULT_ROOT_USER_NAME TEXT("root")
#define KERNEL_PATH_LEAF_PRIVATE_USER_DATA TEXT("data")
#define KERNEL_FILE_EXTENSION_PACKAGE TEXT(".epk")
#define KERNEL_FILE_EXTENSION_KEYBOARD_LAYOUT TEXT(".ekm1")

/***************************************************************************/

BOOL KernelPathResolve(LPCSTR Name, LPCSTR DefaultPath, LPSTR OutPath, UINT OutPathSize);
BOOL KernelPathResolveListEntry(UINT ListType, UINT Index, LPSTR OutPath, UINT OutPathSize);
BOOL KernelPathBuildFile(
    LPCSTR FolderName, LPCSTR DefaultFolder, LPCSTR LeafName, LPCSTR Extension, LPSTR OutPath, UINT OutPathSize);

/***************************************************************************/

#endif  // KERNELPATH_H_INCLUDED
