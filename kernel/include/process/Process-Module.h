/************************************************************************\

    EXOS Kernel
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


    Process executable module bindings

\************************************************************************/

#ifndef PROCESS_MODULE_H_INCLUDED
#define PROCESS_MODULE_H_INCLUDED

/***************************************************************************/

#include "exec/Executable-Module.h"
#include "process/Process.h"

/***************************************************************************/

typedef struct tag_EXECUTABLE_MODULE_BINDING EXECUTABLE_MODULE_BINDING, *LPEXECUTABLE_MODULE_BINDING;

/***************************************************************************/

#define EXECUTABLE_MODULE_BINDING_STATE_CREATED 0x00000001
#define EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_ASSIGNED 0x00000002
#define EXECUTABLE_MODULE_BINDING_STATE_DEPENDENCIES_RESOLVED 0x00000004
#define EXECUTABLE_MODULE_BINDING_STATE_RELOCATED 0x00000008
#define EXECUTABLE_MODULE_BINDING_STATE_TLS_REGISTERED 0x00000010
#define EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_INSTALLED 0x00000020
#define EXECUTABLE_MODULE_BINDING_STATE_GLOBAL_DATA_INITIALIZED 0x00000040

/***************************************************************************/

typedef struct tag_EXECUTABLE_MODULE_BINDING_DEPENDENCY {
    LISTNODE_FIELDS
    LPEXECUTABLE_MODULE_BINDING Binding;
} EXECUTABLE_MODULE_BINDING_DEPENDENCY, *LPEXECUTABLE_MODULE_BINDING_DEPENDENCY;

/***************************************************************************/

struct tag_EXECUTABLE_MODULE_BINDING {
    LISTNODE_FIELDS
    MUTEX Mutex;
    LPPROCESS Process;
    LPEXECUTABLE_MODULE_IMAGE Image;
    UINT ProcessReferences;
    U32 StateFlags;
    LINEAR SegmentBases[EXECUTABLE_MAX_SEGMENTS];
    UINT SegmentSizes[EXECUTABLE_MAX_SEGMENTS];
    LINEAR WritableDataBase;
    UINT WritableDataSize;
    LINEAR GlobalOffsetTableBase;
    LINEAR ProcedureLinkageTableBase;
    LINEAR BookkeepingBase;
    LPLIST Dependencies;
};

/***************************************************************************/

BOOL InitializeProcessModuleBindings(LPPROCESS Process);
void DeleteProcessModuleBindings(LPPROCESS Process);
UINT GetProcessModuleBindingCount(LPPROCESS Process);
LPEXECUTABLE_MODULE_BINDING FindProcessModuleBinding(LPPROCESS Process, LPEXECUTABLE_MODULE_IMAGE Image);
LPEXECUTABLE_MODULE_BINDING AcquireProcessModuleBinding(LPPROCESS Process, LPEXECUTABLE_MODULE_IMAGE Image);
void ReleaseProcessModuleBinding(LPEXECUTABLE_MODULE_BINDING Binding);
BOOL SetProcessModuleBindingSegmentBase(
    LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding, UINT SegmentIndex, LINEAR Base);
BOOL SetProcessModuleBindingLayout(
    LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding, LINEAR WritableDataBase, LINEAR GlobalOffsetTableBase,
    LINEAR ProcedureLinkageTableBase, LINEAR BookkeepingBase, U32 StateFlags);
BOOL AddProcessModuleBindingDependency(
    LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding, LPEXECUTABLE_MODULE_BINDING Dependency);
LINEAR MapProcessModuleBindingAddress(LPVOID Context, UINT VirtualAddress);
BOOL ResolveProcessModuleBindingExport(
    LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding, LPCSTR Name, LINEAR* Address);
BOOL InitializeProcessModuleTls(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding);
BOOL InstallProcessModuleBindingSegments(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding);
void DeleteExecutableModuleBinding(LPEXECUTABLE_MODULE_BINDING Binding);

/***************************************************************************/

#endif
