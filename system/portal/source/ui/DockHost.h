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


    Generic docking host contract for UI layout

\************************************************************************/

#ifndef DESKTOP_COMPONENTS_DOCK_HOST_H_INCLUDED
#define DESKTOP_COMPONENTS_DOCK_HOST_H_INCLUDED

/************************************************************************/

#include "exos.h"
#include "ui/Dockable.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define DOCK_HOST_MAX_ITEMS 64
#define DOCK_LAYOUT_MAX_ASSIGNMENTS DOCK_HOST_MAX_ITEMS

typedef enum tag_DOCK_DIRTY_REASON {
    DOCK_DIRTY_REASON_NONE = 0,
    DOCK_DIRTY_REASON_HOST_RECT_CHANGED = 1,
    DOCK_DIRTY_REASON_POLICY_CHANGED = 2,
    DOCK_DIRTY_REASON_ATTACH_DETACH = 3,
    DOCK_DIRTY_REASON_DOCKABLE_PROPERTY_CHANGED = 4,
    DOCK_DIRTY_REASON_VISIBILITY_CHANGED = 5
} DOCK_DIRTY_REASON, *LPDOCK_DIRTY_REASON;

typedef enum tag_DOCK_OVERFLOW_POLICY {
    DOCK_OVERFLOW_POLICY_CLIP = 0,
    DOCK_OVERFLOW_POLICY_SHRINK = 1,
    DOCK_OVERFLOW_POLICY_REJECT = 2
} DOCK_OVERFLOW_POLICY, *LPDOCK_OVERFLOW_POLICY;

typedef struct tag_DOCK_EDGE_LAYOUT_POLICY {
    I32 MarginStart;
    I32 MarginEnd;
    I32 Spacing;
    U32 OverflowPolicy;
} DOCK_EDGE_LAYOUT_POLICY, *LPDOCK_EDGE_LAYOUT_POLICY;

typedef struct tag_DOCK_HOST_LAYOUT_POLICY {
    I32 PaddingTop;
    I32 PaddingBottom;
    I32 PaddingLeft;
    I32 PaddingRight;
    DOCK_EDGE_LAYOUT_POLICY Top;
    DOCK_EDGE_LAYOUT_POLICY Bottom;
    DOCK_EDGE_LAYOUT_POLICY Left;
    DOCK_EDGE_LAYOUT_POLICY Right;
} DOCK_HOST_LAYOUT_POLICY, *LPDOCK_HOST_LAYOUT_POLICY;

typedef struct tag_DOCK_LAYOUT_RESULT {
    U32 Status;
    RECT HostRect;
    RECT WorkRect;
    U32 DockableCount;
    U32 AppliedCount;
    U32 RejectedCount;
} DOCK_LAYOUT_RESULT, *LPDOCK_LAYOUT_RESULT;

typedef struct tag_DOCK_LAYOUT_ASSIGNMENT {
    LPDOCKABLE Dockable;
    RECT AssignedRect;
    U32 Status;
} DOCK_LAYOUT_ASSIGNMENT, *LPDOCK_LAYOUT_ASSIGNMENT;

typedef struct tag_DOCK_LAYOUT_FRAME {
    U32 Status;
    RECT HostRect;
    RECT WorkRect;
    U32 DockableCount;
    U32 AppliedCount;
    U32 RejectedCount;
    U32 AssignmentCount;
    DOCK_LAYOUT_ASSIGNMENT Assignments[DOCK_LAYOUT_MAX_ASSIGNMENTS];
} DOCK_LAYOUT_FRAME, *LPDOCK_LAYOUT_FRAME;

typedef struct tag_DOCK_HOST {
    LPCSTR Identifier;
    LPVOID Context;
    RECT HostRect;
    RECT WorkRect;
    U32 LayoutSequence;
    BOOL LayoutDirty;
    U32 LastDirtyReason;
    U32 ItemCount;
    U32 Capacity;
    LPDOCKABLE Items[DOCK_HOST_MAX_ITEMS];
    DOCK_HOST_LAYOUT_POLICY Policy;
} DOCK_HOST;

/************************************************************************/

BOOL DockHostInit(LPDOCK_HOST Host, LPCSTR Identifier, LPVOID Context);
BOOL DockHostReset(LPDOCK_HOST Host);
U32 DockHostSetHostRect(LPDOCK_HOST Host, LPRECT HostRect);
U32 DockHostAttachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable);
U32 DockHostDetachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable);
U32 DockHostMarkDirty(LPDOCK_HOST Host, U32 Reason);
U32 DockHostBuildLayoutFrame(LPDOCK_HOST Host, LPDOCK_LAYOUT_FRAME Frame);
U32 DockHostApplyLayoutFrame(LPDOCK_HOST Host, LPDOCK_LAYOUT_FRAME Frame, LPDOCK_LAYOUT_RESULT Result);

/************************************************************************/

#pragma pack(pop)

#endif
