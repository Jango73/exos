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


    Generic dockable behavior implementation

\************************************************************************/

#include "ui/Dockable.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>

/************************************************************************/

/**
 * @brief Check whether one edge value is supported by the docking API.
 * @param Edge Candidate edge value.
 * @return TRUE when valid.
 */
static BOOL DockableIsValidEdge(U32 Edge) {
    if (Edge == DOCK_EDGE_NONE) return TRUE;
    if (Edge == DOCK_EDGE_TOP) return TRUE;
    if (Edge == DOCK_EDGE_BOTTOM) return TRUE;
    if (Edge == DOCK_EDGE_LEFT) return TRUE;
    if (Edge == DOCK_EDGE_RIGHT) return TRUE;
    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether one size policy value is supported by the docking API.
 * @param Policy Candidate policy value.
 * @return TRUE when valid.
 */
static BOOL DockableIsValidPolicy(U32 Policy) {
    if (Policy == DOCK_LAYOUT_POLICY_AUTO) return TRUE;
    if (Policy == DOCK_LAYOUT_POLICY_FIXED) return TRUE;
    if (Policy == DOCK_LAYOUT_POLICY_WEIGHTED) return TRUE;
    return FALSE;
}

/************************************************************************/

/**
 * @brief Initialize mutable dockable fields to deterministic defaults.
 * @param Dockable Target dockable object.
 */
static void DockableSetMutableDefaults(LPDOCKABLE Dockable) {
    Dockable->Edge = DOCK_EDGE_NONE;
    Dockable->Priority = 0;
    Dockable->Order = 0;
    Dockable->Band = 0;
    Dockable->InsertionIndex = 0;
    Dockable->Visible = TRUE;
    Dockable->Enabled = TRUE;

    Dockable->SizeRequest.Policy = DOCK_LAYOUT_POLICY_AUTO;
    Dockable->SizeRequest.PreferredPrimarySize = 0;
    Dockable->SizeRequest.MinimumPrimarySize = 0;
    Dockable->SizeRequest.MaximumPrimarySize = 0;
    Dockable->SizeRequest.Weight = 1;

    Dockable->Callbacks.Measure = NULL;
    Dockable->Callbacks.ApplyRect = NULL;
    Dockable->Callbacks.OnDockChanged = NULL;
    Dockable->Callbacks.OnHostWorkRectChanged = NULL;
}

/************************************************************************/

/**
 * @brief Validate one size request without mutating current dockable state.
 * @param Request Candidate size request.
 * @return `DOCK_LAYOUT_STATUS_SUCCESS` on success.
 */
static U32 DockableValidateSizeRequest(LPDOCK_SIZE_REQUEST Request) {
    if (Request == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockableIsValidPolicy(Request->Policy) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_POLICY;

    if (Request->PreferredPrimarySize < 0) return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    if (Request->MinimumPrimarySize < 0) return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    if (Request->MaximumPrimarySize < 0) return DOCK_LAYOUT_STATUS_INVALID_SIZE;

    if (Request->MaximumPrimarySize != 0 && Request->MinimumPrimarySize > Request->MaximumPrimarySize) {
        return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    }

    if (Request->PreferredPrimarySize != 0 && Request->PreferredPrimarySize < Request->MinimumPrimarySize) {
        return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    }

    if (Request->MaximumPrimarySize != 0 && Request->PreferredPrimarySize > Request->MaximumPrimarySize) {
        return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    }

    if (Request->Policy == DOCK_LAYOUT_POLICY_FIXED && Request->PreferredPrimarySize <= 0) {
        return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    }

    if (Request->Policy == DOCK_LAYOUT_POLICY_WEIGHTED && Request->Weight == 0) {
        return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    }

    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Initialize one dockable object with immutable identity fields.
 * @param Dockable Target dockable object.
 * @param Identifier Immutable identifier.
 * @param Context Immutable caller context.
 * @return TRUE on success.
 */
BOOL DockableInit(LPDOCKABLE Dockable, LPCSTR Identifier, LPVOID Context) {
    if (Dockable == NULL) return FALSE;
    if (Identifier == NULL) return FALSE;

    Dockable->Identifier = Identifier;
    Dockable->Context = Context;
    DockableSetMutableDefaults(Dockable);
    return TRUE;
}

/**
 * @brief Update edge assignment for one dockable.
 * @param Dockable Target dockable object.
 * @param Edge New edge value.
 * @return Docking status code.
 */
U32 DockableSetEdge(LPDOCKABLE Dockable, U32 Edge) {
    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockableIsValidEdge(Edge) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_EDGE;

    Dockable->Edge = Edge;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Update deterministic ordering fields for one dockable.
 * @param Dockable Target dockable object.
 * @param Priority Primary order key.
 * @param Order Secondary order key.
 * @return Docking status code.
 */
U32 DockableSetOrder(LPDOCKABLE Dockable, I32 Priority, I32 Order) {
    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Dockable->Priority = Priority;
    Dockable->Order = Order;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Validate and apply one size request to one dockable.
 * @param Dockable Target dockable object.
 * @param Request Candidate size request.
 * @return Docking status code.
 */
U32 DockableSetSizeRequest(LPDOCKABLE Dockable, LPDOCK_SIZE_REQUEST Request) {
    DOCK_SIZE_REQUEST NewRequest;
    U32 Status;

    if (Dockable == NULL || Request == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    NewRequest = *Request;
    Status = DockableValidateSizeRequest(&NewRequest);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS) return Status;

    Dockable->SizeRequest = NewRequest;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Update callback table for one dockable.
 * @param Dockable Target dockable object.
 * @param Callbacks New callback table, or NULL to clear callbacks.
 * @return Docking status code.
 */
U32 DockableSetCallbacks(LPDOCKABLE Dockable, LPDOCKABLE_CALLBACKS Callbacks) {
    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    if (Callbacks == NULL) {
        Dockable->Callbacks.Measure = NULL;
        Dockable->Callbacks.ApplyRect = NULL;
        Dockable->Callbacks.OnDockChanged = NULL;
        Dockable->Callbacks.OnHostWorkRectChanged = NULL;
        return DOCK_LAYOUT_STATUS_SUCCESS;
    }

    Dockable->Callbacks = *Callbacks;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}
