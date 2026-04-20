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


    Generic dockable contract for UI layout hosts

\************************************************************************/

#ifndef DESKTOP_COMPONENTS_DOCKABLE_H_INCLUDED
#define DESKTOP_COMPONENTS_DOCKABLE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "exos.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

struct tag_DOCK_HOST;

typedef enum tag_DOCK_EDGE {
    DOCK_EDGE_NONE = 0,
    DOCK_EDGE_TOP = 1,
    DOCK_EDGE_BOTTOM = 2,
    DOCK_EDGE_LEFT = 3,
    DOCK_EDGE_RIGHT = 4
} DOCK_EDGE, *LPDOCK_EDGE;

typedef enum tag_DOCK_AXIS {
    DOCK_AXIS_NONE = 0,
    DOCK_AXIS_HORIZONTAL = 1,
    DOCK_AXIS_VERTICAL = 2
} DOCK_AXIS, *LPDOCK_AXIS;

typedef enum tag_DOCK_LAYOUT_POLICY {
    DOCK_LAYOUT_POLICY_AUTO = 0,
    DOCK_LAYOUT_POLICY_FIXED = 1,
    DOCK_LAYOUT_POLICY_WEIGHTED = 2
} DOCK_LAYOUT_POLICY, *LPDOCK_LAYOUT_POLICY;

typedef enum tag_DOCK_LAYOUT_STATUS {
    DOCK_LAYOUT_STATUS_SUCCESS = 0,
    DOCK_LAYOUT_STATUS_INVALID_PARAMETER = 1,
    DOCK_LAYOUT_STATUS_INVALID_EDGE = 2,
    DOCK_LAYOUT_STATUS_INVALID_POLICY = 3,
    DOCK_LAYOUT_STATUS_INVALID_SIZE = 4,
    DOCK_LAYOUT_STATUS_NOT_ATTACHED = 5,
    DOCK_LAYOUT_STATUS_ALREADY_ATTACHED = 6,
    DOCK_LAYOUT_STATUS_ALREADY_REGISTERED = 7,
    DOCK_LAYOUT_STATUS_DUPLICATE_IDENTIFIER = 8,
    DOCK_LAYOUT_STATUS_CAPACITY_EXCEEDED = 9,
    DOCK_LAYOUT_STATUS_CONSTRAINT_VIOLATION = 10,
    DOCK_LAYOUT_STATUS_LAYOUT_REJECTED = 11,
    DOCK_LAYOUT_STATUS_NOT_SUPPORTED = 12,
    DOCK_LAYOUT_STATUS_OUT_OF_MEMORY = 13
} DOCK_LAYOUT_STATUS, *LPDOCK_LAYOUT_STATUS;

typedef struct tag_DOCK_SIZE_REQUEST {
    U32 Policy;
    I32 PreferredPrimarySize;
    I32 MinimumPrimarySize;
    I32 MaximumPrimarySize;
    U32 Weight;
} DOCK_SIZE_REQUEST, *LPDOCK_SIZE_REQUEST;

typedef struct tag_DOCKABLE DOCKABLE;
typedef DOCKABLE* LPDOCKABLE;
typedef struct tag_DOCK_HOST DOCK_HOST;
typedef DOCK_HOST* LPDOCK_HOST;

typedef U32 (*DOCKABLE_MEASURE_CALLBACK)(
    LPDOCKABLE Dockable,
    LPDOCK_HOST Host,
    LPRECT HostRect,
    LPDOCK_SIZE_REQUEST Request
);

typedef U32 (*DOCKABLE_APPLY_RECT_CALLBACK)(
    LPDOCKABLE Dockable,
    LPDOCK_HOST Host,
    LPRECT AssignedRect,
    LPRECT WorkRect
);

typedef U32 (*DOCKABLE_DOCK_CHANGED_CALLBACK)(
    LPDOCKABLE Dockable,
    U32 OldEdge,
    U32 NewEdge
);

typedef U32 (*DOCKABLE_WORK_RECT_CHANGED_CALLBACK)(
    LPDOCKABLE Dockable,
    LPDOCK_HOST Host,
    LPRECT WorkRect
);

typedef struct tag_DOCKABLE_CALLBACKS {
    DOCKABLE_MEASURE_CALLBACK Measure;
    DOCKABLE_APPLY_RECT_CALLBACK ApplyRect;
    DOCKABLE_DOCK_CHANGED_CALLBACK OnDockChanged;
    DOCKABLE_WORK_RECT_CHANGED_CALLBACK OnHostWorkRectChanged;
} DOCKABLE_CALLBACKS, *LPDOCKABLE_CALLBACKS;

typedef struct tag_DOCKABLE {
    LPCSTR Identifier;
    LPVOID Context;
    U32 Edge;
    I32 Priority;
    I32 Order;
    U32 Band;
    U32 InsertionIndex;
    BOOL Visible;
    BOOL Enabled;
    DOCK_SIZE_REQUEST SizeRequest;
    DOCKABLE_CALLBACKS Callbacks;
} DOCKABLE;

/************************************************************************/

BOOL DockableInit(LPDOCKABLE Dockable, LPCSTR Identifier, LPVOID Context);
U32 DockableSetEdge(LPDOCKABLE Dockable, U32 Edge);
U32 DockableSetOrder(LPDOCKABLE Dockable, I32 Priority, I32 Order);
U32 DockableSetSizeRequest(LPDOCKABLE Dockable, LPDOCK_SIZE_REQUEST Request);
U32 DockableSetCallbacks(LPDOCKABLE Dockable, LPDOCKABLE_CALLBACKS Callbacks);

/************************************************************************/

#pragma pack(pop)

#endif
