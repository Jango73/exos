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


    Generic docking host behavior implementation

\************************************************************************/

#include "ui/DockHost.h"
#include "exos-runtime-main.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>

/************************************************************************/

typedef struct tag_DOCK_EDGE_BUCKET {
    LPDOCKABLE Items[DOCK_HOST_MAX_ITEMS];
    UINT Count;
} DOCK_EDGE_BUCKET, *LPDOCK_EDGE_BUCKET;

/************************************************************************/

/**
 * @brief Check one edge value.
 * @param Edge Candidate edge.
 * @return TRUE when valid.
 */
static BOOL DockHostIsValidEdge(U32 Edge) {
    if (Edge == DOCK_EDGE_NONE) return TRUE;
    if (Edge == DOCK_EDGE_TOP) return TRUE;
    if (Edge == DOCK_EDGE_BOTTOM) return TRUE;
    if (Edge == DOCK_EDGE_LEFT) return TRUE;
    if (Edge == DOCK_EDGE_RIGHT) return TRUE;
    return FALSE;
}

/**
 * @brief Validate one host rectangle.
 * @param Rect Candidate rectangle.
 * @return TRUE when valid.
 */
static BOOL DockHostIsValidRect(LPRECT Rect) {
    if (Rect == NULL) return FALSE;
    if (Rect->X2 < Rect->X1) return FALSE;
    if (Rect->Y2 < Rect->Y1) return FALSE;
    return TRUE;
}

/**
 * @brief Compare two dockables with deterministic tie-break.
 * @param Left First dockable.
 * @param Right Second dockable.
 * @return Negative when Left should be first.
 */
static I32 DockHostCompareDockables(LPDOCKABLE Left, LPDOCKABLE Right) {
    if (Left->Priority < Right->Priority) return -1;
    if (Left->Priority > Right->Priority) return 1;
    if (Left->Order < Right->Order) return -1;
    if (Left->Order > Right->Order) return 1;
    if (Left->InsertionIndex < Right->InsertionIndex) return -1;
    if (Left->InsertionIndex > Right->InsertionIndex) return 1;
    return 0;
}

/************************************************************************/

/**
 * @brief Stable-sort one edge bucket according to docking order contract.
 * @param Bucket Target bucket.
 */
static void DockHostSortBucket(LPDOCK_EDGE_BUCKET Bucket) {
    UINT I;
    UINT J;
    LPDOCKABLE Temp;

    if (Bucket == NULL) return;

    for (I = 0; I < Bucket->Count; I++) {
        for (J = I + 1; J < Bucket->Count; J++) {
            if (DockHostCompareDockables(Bucket->Items[J], Bucket->Items[I]) < 0) {
                Temp = Bucket->Items[I];
                Bucket->Items[I] = Bucket->Items[J];
                Bucket->Items[J] = Temp;
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Build one edge bucket from all attached dockables.
 * @param Host Source host.
 * @param Edge Target edge.
 * @param Bucket Destination bucket.
 */
static void DockHostBuildBucket(LPDOCK_HOST Host, U32 Edge, LPDOCK_EDGE_BUCKET Bucket) {
    UINT Index;
    LPDOCKABLE Dockable;

    Bucket->Count = 0;
    for (Index = 0; Index < Host->ItemCount; Index++) {
        Dockable = Host->Items[Index];
        if (Dockable == NULL) continue;
        if (Dockable->Visible == FALSE) continue;
        if (Dockable->Enabled == FALSE) continue;
        if (Dockable->Edge != Edge) continue;
        if (Bucket->Count >= DOCK_HOST_MAX_ITEMS) break;
        Bucket->Items[Bucket->Count++] = Dockable;
    }

    DockHostSortBucket(Bucket);
}

/************************************************************************/

/**
 * @brief Validate one dock size request.
 * @param Request Candidate request.
 * @return TRUE when valid.
 */
static BOOL DockHostValidateSizeRequest(LPDOCK_SIZE_REQUEST Request) {
    if (Request == NULL) return FALSE;
    if (Request->Policy != DOCK_LAYOUT_POLICY_AUTO &&
        Request->Policy != DOCK_LAYOUT_POLICY_FIXED &&
        Request->Policy != DOCK_LAYOUT_POLICY_WEIGHTED) {
        return FALSE;
    }
    if (Request->PreferredPrimarySize < 0) return FALSE;
    if (Request->MinimumPrimarySize < 0) return FALSE;
    if (Request->MaximumPrimarySize < 0) return FALSE;
    if (Request->MaximumPrimarySize > 0 && Request->MinimumPrimarySize > Request->MaximumPrimarySize) return FALSE;
    if (Request->PreferredPrimarySize > 0 && Request->PreferredPrimarySize < Request->MinimumPrimarySize) return FALSE;
    if (Request->MaximumPrimarySize > 0 && Request->PreferredPrimarySize > Request->MaximumPrimarySize) return FALSE;
    if (Request->Policy == DOCK_LAYOUT_POLICY_WEIGHTED && Request->Weight == 0) return FALSE;
    if (Request->Policy == DOCK_LAYOUT_POLICY_FIXED && Request->PreferredPrimarySize <= 0) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve one dockable size request from stored dockable state.
 * @param Dockable Source dockable.
 * @param RequestOut Resolved request output.
 * @return Docking status code.
 */
static U32 DockHostResolveSizeRequest(LPDOCKABLE Dockable, LPDOCK_SIZE_REQUEST RequestOut) {
    if (Dockable == NULL || RequestOut == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    *RequestOut = Dockable->SizeRequest;
    if (DockHostValidateSizeRequest(RequestOut) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_SIZE;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Resolve one requested size value from one request.
 * @param Request Source request.
 * @return Requested size value.
 */
static I32 DockHostResolveRequestedSize(LPDOCK_SIZE_REQUEST Request) {
    I32 Size;

    Size = Request->PreferredPrimarySize;
    if (Size < Request->MinimumPrimarySize) Size = Request->MinimumPrimarySize;
    if (Request->MaximumPrimarySize > 0 && Size > Request->MaximumPrimarySize) Size = Request->MaximumPrimarySize;
    if (Size <= 0) Size = 1;
    return Size;
}

/************************************************************************/

/**
 * @brief Resolve requested edge thickness for one bucket.
 * @param Requests Per-dockable resolved requests.
 * @param Count Request count.
 * @return Thickness in pixels.
 */
static I32 DockHostResolveEdgeThickness(LPDOCK_SIZE_REQUEST Requests, UINT Count) {
    UINT Index;
    I32 Thickness = 0;
    I32 Candidate;

    if (Requests == NULL || Count == 0) return 0;

    for (Index = 0; Index < Count; Index++) {
        Candidate = DockHostResolveRequestedSize(&(Requests[Index]));
        if (Candidate > Thickness) Thickness = Candidate;
    }

    if (Thickness <= 0) Thickness = 1;
    return Thickness;
}

/************************************************************************/

/**
 * @brief Fill per-item primary sizes based on request policies and available space.
 * @param Requests Resolved requests.
 * @param Count Item count.
 * @param Policy Overflow policy.
 * @param ContentAvailable Available primary size excluding spacing.
 * @param SizesOut Output per-item size array.
 * @return Docking status code.
 */
static U32 DockHostResolvePrimarySizes(
    LPDOCK_SIZE_REQUEST Requests,
    UINT Count,
    U32 Policy,
    I32 ContentAvailable,
    I32* SizesOut
) {
    UINT Index;
    I32 SumFixed = 0;
    U32 WeightTotal = 0;
    I32 Remaining;
    I32 Share;
    I32 Remainder;

    if (Requests == NULL || SizesOut == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (Count == 0) return DOCK_LAYOUT_STATUS_SUCCESS;

    if (ContentAvailable <= 0) {
        if (Policy == DOCK_OVERFLOW_POLICY_REJECT) return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
        for (Index = 0; Index < Count; Index++) SizesOut[Index] = 0;
        return DOCK_LAYOUT_STATUS_SUCCESS;
    }

    for (Index = 0; Index < Count; Index++) {
        if (Requests[Index].Policy == DOCK_LAYOUT_POLICY_FIXED) {
            SizesOut[Index] = DockHostResolveRequestedSize(&(Requests[Index]));
            SumFixed += SizesOut[Index];
        } else {
            SizesOut[Index] = 0;
            WeightTotal += Requests[Index].Policy == DOCK_LAYOUT_POLICY_WEIGHTED ? Requests[Index].Weight : 1;
        }
    }

    if (SumFixed > ContentAvailable) {
        if (Policy == DOCK_OVERFLOW_POLICY_REJECT) return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;

        if (Policy == DOCK_OVERFLOW_POLICY_CLIP) {
            for (Index = 0; Index < Count; Index++) {
                if (Requests[Index].Policy != DOCK_LAYOUT_POLICY_FIXED) SizesOut[Index] = 0;
            }
            return DOCK_LAYOUT_STATUS_SUCCESS;
        }

        SumFixed = 0;
        WeightTotal = Count;
        for (Index = 0; Index < Count; Index++) {
            SizesOut[Index] = 0;
        }
    }

    Remaining = ContentAvailable - SumFixed;
    if (Remaining < 0) Remaining = 0;

    if (WeightTotal > 0 && Remaining > 0) {
        for (Index = 0; Index < Count; Index++) {
            if (Requests[Index].Policy == DOCK_LAYOUT_POLICY_FIXED) continue;

            Share = Requests[Index].Policy == DOCK_LAYOUT_POLICY_WEIGHTED ? Requests[Index].Weight : 1;
            SizesOut[Index] = (Remaining * Share) / (I32)WeightTotal;
        }

        Remainder = Remaining;
        for (Index = 0; Index < Count; Index++) Remainder -= SizesOut[Index];
        for (Index = 0; Remainder > 0 && Index < Count; Index++) {
            if (Requests[Index].Policy == DOCK_LAYOUT_POLICY_FIXED) continue;
            SizesOut[Index]++;
            Remainder--;
            if (Index + 1 == Count && Remainder > 0) Index = (UINT)-1;
        }
    }

    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Fill one layout frame object with host baseline values.
 * @param Host Source host.
 * @param Frame Destination layout frame.
 */
static void DockHostInitializeLayoutFrame(LPDOCK_HOST Host, LPDOCK_LAYOUT_FRAME Frame) {
    Frame->Status = DOCK_LAYOUT_STATUS_SUCCESS;
    Frame->HostRect = Host->HostRect;
    Frame->WorkRect = Host->WorkRect;
    Frame->DockableCount = Host->ItemCount;
    Frame->AppliedCount = 0;
    Frame->RejectedCount = 0;
    Frame->AssignmentCount = 0;
}

/************************************************************************/

/**
 * @brief Initialize one public layout result from one layout frame.
 * @param Result Destination result.
 * @param Frame Source frame.
 */
static void DockHostFillResultFromFrame(LPDOCK_LAYOUT_RESULT Result, LPDOCK_LAYOUT_FRAME Frame) {
    if (Result == NULL || Frame == NULL) return;

    Result->Status = Frame->Status;
    Result->HostRect = Frame->HostRect;
    Result->WorkRect = Frame->WorkRect;
    Result->DockableCount = Frame->DockableCount;
    Result->AppliedCount = Frame->AppliedCount;
    Result->RejectedCount = Frame->RejectedCount;
}

/************************************************************************/

/**
 * @brief Append one assignment to a frame.
 * @param Frame Destination frame.
 * @param Dockable Dockable reference.
 * @param AssignedRect Assigned rectangle.
 * @param Status Assignment status.
 * @return TRUE on success.
 */
static BOOL DockHostAppendAssignment(
    LPDOCK_LAYOUT_FRAME Frame,
    LPDOCKABLE Dockable,
    LPRECT AssignedRect,
    U32 Status
) {
    LPDOCK_LAYOUT_ASSIGNMENT Assignment;

    if (Frame == NULL || Dockable == NULL || AssignedRect == NULL) return FALSE;
    if (Frame->AssignmentCount >= DOCK_LAYOUT_MAX_ASSIGNMENTS) return FALSE;

    Assignment = &(Frame->Assignments[Frame->AssignmentCount++]);
    Assignment->Dockable = Dockable;
    Assignment->AssignedRect = *AssignedRect;
    Assignment->Status = Status;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Set default host policy values.
 * @param Host Target host.
 */
static void DockHostSetDefaultPolicy(LPDOCK_HOST Host) {
    Host->Policy.PaddingTop = 0;
    Host->Policy.PaddingBottom = 0;
    Host->Policy.PaddingLeft = 0;
    Host->Policy.PaddingRight = 0;

    Host->Policy.Top.MarginStart = 0;
    Host->Policy.Top.MarginEnd = 0;
    Host->Policy.Top.Spacing = 0;
    Host->Policy.Top.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;

    Host->Policy.Bottom.MarginStart = 0;
    Host->Policy.Bottom.MarginEnd = 0;
    Host->Policy.Bottom.Spacing = 0;
    Host->Policy.Bottom.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;

    Host->Policy.Left.MarginStart = 0;
    Host->Policy.Left.MarginEnd = 0;
    Host->Policy.Left.Spacing = 0;
    Host->Policy.Left.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;

    Host->Policy.Right.MarginStart = 0;
    Host->Policy.Right.MarginEnd = 0;
    Host->Policy.Right.Spacing = 0;
    Host->Policy.Right.OverflowPolicy = DOCK_OVERFLOW_POLICY_SHRINK;
}

/************************************************************************/

/**
 * @brief Return one edge policy by edge value.
 * @param Host Host owning policy.
 * @param Edge Target edge.
 * @return Edge policy pointer or NULL.
 */
static LPDOCK_EDGE_LAYOUT_POLICY DockHostGetEdgePolicy(LPDOCK_HOST Host, U32 Edge) {
    if (Edge == DOCK_EDGE_TOP) return &(Host->Policy.Top);
    if (Edge == DOCK_EDGE_BOTTOM) return &(Host->Policy.Bottom);
    if (Edge == DOCK_EDGE_LEFT) return &(Host->Policy.Left);
    if (Edge == DOCK_EDGE_RIGHT) return &(Host->Policy.Right);
    return NULL;
}

/************************************************************************/

/**
 * @brief Apply one edge bucket in one side-by-side band.
 * @param Host Target host.
 * @param Edge Edge value.
 * @param Bucket Sorted edge bucket.
 * @param WorkRect In-out work rectangle.
 * @param Frame In-out layout frame.
 * @return Docking status code.
 */
static U32 DockHostApplyEdgeBucket(
    LPDOCK_HOST Host,
    U32 Edge,
    LPDOCK_EDGE_BUCKET Bucket,
    LPRECT WorkRect,
    LPDOCK_LAYOUT_FRAME Frame
) {
    LPDOCK_EDGE_LAYOUT_POLICY EdgePolicy;
    DOCK_SIZE_REQUEST Requests[DOCK_HOST_MAX_ITEMS];
    I32 PrimarySizes[DOCK_HOST_MAX_ITEMS];
    I32 Thickness;
    I32 PrimaryStart;
    I32 PrimaryEnd;
    I32 PrimaryAvailable;
    I32 EffectiveSpacing;
    I32 SpacingTotal;
    I32 ContentAvailable;
    I32 Cursor;
    I32 ItemStart;
    I32 ItemEnd;
    I32 ConsumedPrimary;
    RECT AssignedRect;
    UINT Index;
    U32 Status;

    if (Bucket->Count == 0) return DOCK_LAYOUT_STATUS_SUCCESS;

    EdgePolicy = DockHostGetEdgePolicy(Host, Edge);
    if (EdgePolicy == NULL) return DOCK_LAYOUT_STATUS_INVALID_EDGE;

    for (Index = 0; Index < Bucket->Count; Index++) {
        Status = DockHostResolveSizeRequest(Bucket->Items[Index], &(Requests[Index]));
        if (Status != DOCK_LAYOUT_STATUS_SUCCESS) {
            Frame->RejectedCount++;
            if (Frame->Status == DOCK_LAYOUT_STATUS_SUCCESS) Frame->Status = Status;
            if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_REJECT) return Status;
            Requests[Index] = Bucket->Items[Index]->SizeRequest;
        }
    }

    Thickness = DockHostResolveEdgeThickness(Requests, Bucket->Count);
    UNUSED(Thickness);
    if (Edge == DOCK_EDGE_TOP || Edge == DOCK_EDGE_BOTTOM) {
        PrimaryStart = WorkRect->Y1 + EdgePolicy->MarginStart;
        PrimaryEnd = WorkRect->Y2 - EdgePolicy->MarginEnd;
    } else {
        PrimaryStart = WorkRect->X1 + EdgePolicy->MarginStart;
        PrimaryEnd = WorkRect->X2 - EdgePolicy->MarginEnd;
    }

    if (PrimaryEnd < PrimaryStart) {
        Frame->RejectedCount += Bucket->Count;
        return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
    }

    PrimaryAvailable = PrimaryEnd - PrimaryStart + 1;
    EffectiveSpacing = EdgePolicy->Spacing;
    if (Bucket->Count <= 1) EffectiveSpacing = 0;

    SpacingTotal = EffectiveSpacing * ((I32)Bucket->Count - 1);
    if (SpacingTotal >= PrimaryAvailable) {
        if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_REJECT) {
            Frame->RejectedCount += Bucket->Count;
            return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
        }
        if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_SHRINK) {
            EffectiveSpacing = 0;
            SpacingTotal = 0;
        }
    }

    ContentAvailable = PrimaryAvailable - SpacingTotal;
    Status = DockHostResolvePrimarySizes(
        Requests,
        Bucket->Count,
        EdgePolicy->OverflowPolicy,
        ContentAvailable,
        PrimarySizes
    );
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS) {
        Frame->RejectedCount += Bucket->Count;
        if (Frame->Status == DOCK_LAYOUT_STATUS_SUCCESS) Frame->Status = Status;
        return Status;
    }

    Cursor = PrimaryStart;

    for (Index = 0; Index < Bucket->Count; Index++) {
        ItemStart = Cursor;
        ItemEnd = ItemStart + PrimarySizes[Index] - 1;
        if (ItemEnd > PrimaryEnd) ItemEnd = PrimaryEnd;

        if (Edge == DOCK_EDGE_TOP) {
            AssignedRect.X1 = WorkRect->X1;
            AssignedRect.X2 = WorkRect->X2;
            AssignedRect.Y1 = ItemStart;
            AssignedRect.Y2 = ItemEnd;
        } else if (Edge == DOCK_EDGE_BOTTOM) {
            AssignedRect.X1 = WorkRect->X1;
            AssignedRect.X2 = WorkRect->X2;
            AssignedRect.Y2 = WorkRect->Y2 - (ItemStart - PrimaryStart);
            AssignedRect.Y1 = AssignedRect.Y2 - (PrimarySizes[Index] - 1);
        } else if (Edge == DOCK_EDGE_LEFT) {
            AssignedRect.Y1 = WorkRect->Y1;
            AssignedRect.Y2 = WorkRect->Y2;
            AssignedRect.X1 = ItemStart;
            AssignedRect.X2 = ItemEnd;
        } else {
            AssignedRect.Y1 = WorkRect->Y1;
            AssignedRect.Y2 = WorkRect->Y2;
            AssignedRect.X2 = WorkRect->X2 - (ItemStart - PrimaryStart);
            AssignedRect.X1 = AssignedRect.X2 - (PrimarySizes[Index] - 1);
        }

        if (ItemEnd < ItemStart) {
            Frame->RejectedCount++;
            Frame->Status = DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
            if (EdgePolicy->OverflowPolicy == DOCK_OVERFLOW_POLICY_REJECT) return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
            Cursor += PrimarySizes[Index] + EffectiveSpacing;
            continue;
        }

        if (DockHostAppendAssignment(Frame, Bucket->Items[Index], &AssignedRect, DOCK_LAYOUT_STATUS_SUCCESS) == FALSE) {
            Frame->RejectedCount++;
            if (Frame->Status == DOCK_LAYOUT_STATUS_SUCCESS) Frame->Status = DOCK_LAYOUT_STATUS_CAPACITY_EXCEEDED;
            return DOCK_LAYOUT_STATUS_CAPACITY_EXCEEDED;
        }

        Frame->AppliedCount++;

        Cursor += PrimarySizes[Index] + EffectiveSpacing;
    }

    ConsumedPrimary = Cursor - PrimaryStart;
    if (Bucket->Count > 0 && EffectiveSpacing > 0) {
        ConsumedPrimary -= EffectiveSpacing;
    }
    if (ConsumedPrimary < 0) ConsumedPrimary = 0;

    if (Edge == DOCK_EDGE_TOP) {
        WorkRect->Y1 += ConsumedPrimary;
    } else if (Edge == DOCK_EDGE_BOTTOM) {
        WorkRect->Y2 -= ConsumedPrimary;
    } else if (Edge == DOCK_EDGE_LEFT) {
        WorkRect->X1 += ConsumedPrimary;
    } else {
        WorkRect->X2 -= ConsumedPrimary;
    }

    if (WorkRect->X2 < WorkRect->X1) WorkRect->X2 = WorkRect->X1;
    if (WorkRect->Y2 < WorkRect->Y1) WorkRect->Y2 = WorkRect->Y1;

    UNUSED(PrimaryAvailable);
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

BOOL DockHostInit(LPDOCK_HOST Host, LPCSTR Identifier, LPVOID Context) {
    if (Host == NULL || Identifier == NULL) return FALSE;

    Host->Identifier = Identifier;
    Host->Context = Context;
    Host->HostRect.X1 = 0;
    Host->HostRect.Y1 = 0;
    Host->HostRect.X2 = 0;
    Host->HostRect.Y2 = 0;
    Host->WorkRect = Host->HostRect;
    Host->LayoutSequence = 0;
    Host->LayoutDirty = TRUE;
    Host->LastDirtyReason = DOCK_DIRTY_REASON_NONE;
    Host->ItemCount = 0;
    Host->Capacity = DOCK_HOST_MAX_ITEMS;

    DockHostSetDefaultPolicy(Host);
    return TRUE;
}

/************************************************************************/

BOOL DockHostReset(LPDOCK_HOST Host) {
    if (Host == NULL) return FALSE;
    Host->ItemCount = 0;
    Host->WorkRect = Host->HostRect;
    Host->LayoutDirty = TRUE;
    Host->LastDirtyReason = DOCK_DIRTY_REASON_NONE;
    return TRUE;
}

/************************************************************************/

U32 DockHostSetHostRect(LPDOCK_HOST Host, LPRECT HostRect) {
    if (Host == NULL || HostRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockHostIsValidRect(HostRect) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Host->HostRect = *HostRect;
    Host->WorkRect = *HostRect;
    Host->LayoutDirty = TRUE;
    Host->LastDirtyReason = DOCK_DIRTY_REASON_HOST_RECT_CHANGED;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

U32 DockHostAttachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable) {
    UINT Index;

    if (Host == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockHostIsValidEdge(Dockable->Edge) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_EDGE;

    for (Index = 0; Index < Host->ItemCount; Index++) {
        if (Host->Items[Index] == Dockable) return DOCK_LAYOUT_STATUS_ALREADY_ATTACHED;
        if (Host->Items[Index] == NULL) continue;
        if (Host->Items[Index]->Identifier == NULL || Dockable->Identifier == NULL) continue;
        if (StringCompare(Host->Items[Index]->Identifier, Dockable->Identifier) == 0) {
            return DOCK_LAYOUT_STATUS_DUPLICATE_IDENTIFIER;
        }
    }

    if (Host->ItemCount >= Host->Capacity || Host->ItemCount >= DOCK_HOST_MAX_ITEMS) {
        return DOCK_LAYOUT_STATUS_CAPACITY_EXCEEDED;
    }

    Dockable->InsertionIndex = ++Host->LayoutSequence;
    Host->Items[Host->ItemCount++] = Dockable;
    Host->LayoutDirty = TRUE;
    Host->LastDirtyReason = DOCK_DIRTY_REASON_ATTACH_DETACH;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

U32 DockHostDetachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable) {
    UINT Index;
    UINT MoveIndex;

    if (Host == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < Host->ItemCount; Index++) {
        if (Host->Items[Index] != Dockable) continue;
        for (MoveIndex = Index; MoveIndex + 1 < Host->ItemCount; MoveIndex++) {
            Host->Items[MoveIndex] = Host->Items[MoveIndex + 1];
        }
        Host->Items[Host->ItemCount - 1] = NULL;
        Host->ItemCount--;
        Host->LayoutDirty = TRUE;
        Host->LastDirtyReason = DOCK_DIRTY_REASON_ATTACH_DETACH;
        return DOCK_LAYOUT_STATUS_SUCCESS;
    }

    return DOCK_LAYOUT_STATUS_NOT_ATTACHED;
}

/************************************************************************/

U32 DockHostMarkDirty(LPDOCK_HOST Host, U32 Reason) {
    if (Host == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (Reason == DOCK_DIRTY_REASON_NONE) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Host->LayoutDirty = TRUE;
    Host->LastDirtyReason = Reason;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

U32 DockHostBuildLayoutFrame(LPDOCK_HOST Host, LPDOCK_LAYOUT_FRAME Frame) {
    DOCK_EDGE_BUCKET Bucket;
    RECT WorkRect;
    U32 Status;

    if (Host == NULL || Frame == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    if (DockHostIsValidRect(&(Host->HostRect)) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    WorkRect = Host->HostRect;
    WorkRect.X1 += Host->Policy.PaddingLeft;
    WorkRect.Y1 += Host->Policy.PaddingTop;
    WorkRect.X2 -= Host->Policy.PaddingRight;
    WorkRect.Y2 -= Host->Policy.PaddingBottom;
    if (WorkRect.X2 < WorkRect.X1 || WorkRect.Y2 < WorkRect.Y1) {
        return DOCK_LAYOUT_STATUS_CONSTRAINT_VIOLATION;
    }

    Host->WorkRect = WorkRect;
    DockHostInitializeLayoutFrame(Host, Frame);

    DockHostBuildBucket(Host, DOCK_EDGE_TOP, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_TOP, &Bucket, &WorkRect, Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame->Status == DOCK_LAYOUT_STATUS_SUCCESS) Frame->Status = Status;

    DockHostBuildBucket(Host, DOCK_EDGE_BOTTOM, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_BOTTOM, &Bucket, &WorkRect, Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame->Status == DOCK_LAYOUT_STATUS_SUCCESS) Frame->Status = Status;

    DockHostBuildBucket(Host, DOCK_EDGE_LEFT, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_LEFT, &Bucket, &WorkRect, Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame->Status == DOCK_LAYOUT_STATUS_SUCCESS) Frame->Status = Status;

    DockHostBuildBucket(Host, DOCK_EDGE_RIGHT, &Bucket);
    Status = DockHostApplyEdgeBucket(Host, DOCK_EDGE_RIGHT, &Bucket, &WorkRect, Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame->Status == DOCK_LAYOUT_STATUS_SUCCESS) Frame->Status = Status;

    Frame->WorkRect = WorkRect;
    return Frame->Status;
}

/************************************************************************/

U32 DockHostApplyLayoutFrame(LPDOCK_HOST Host, LPDOCK_LAYOUT_FRAME Frame, LPDOCK_LAYOUT_RESULT Result) {
    UINT Index;
    LPDOCK_LAYOUT_ASSIGNMENT Assignment;
    U32 CallbackStatus;
    U32 Status;

    if (Host == NULL || Frame == NULL || Result == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Status = Frame->Status;
    debug("[DockHostApplyLayoutFrame] host=%x frame_status=%u assignments=%u host_rect=%d,%d,%d,%d work_rect=%d,%d,%d,%d",
        (UINT)(LINEAR)Host,
        Frame->Status,
        Frame->AssignmentCount,
        Frame->HostRect.X1,
        Frame->HostRect.Y1,
        Frame->HostRect.X2,
        Frame->HostRect.Y2,
        Frame->WorkRect.X1,
        Frame->WorkRect.Y1,
        Frame->WorkRect.X2,
        Frame->WorkRect.Y2);
    for (Index = 0; Index < Frame->AssignmentCount; Index++) {
        Assignment = &(Frame->Assignments[Index]);
        if (Assignment->Dockable == NULL) continue;
        if (Assignment->Dockable->Callbacks.ApplyRect == NULL) continue;

        debug("[DockHostApplyLayoutFrame] assignment index=%u dockable=%x context=%x rect=%d,%d,%d,%d status=%u",
            Index,
            (UINT)(LINEAR)Assignment->Dockable,
            (UINT)(LINEAR)Assignment->Dockable->Context,
            Assignment->AssignedRect.X1,
            Assignment->AssignedRect.Y1,
            Assignment->AssignedRect.X2,
            Assignment->AssignedRect.Y2,
            Assignment->Status);
        CallbackStatus = Assignment->Dockable->Callbacks.ApplyRect(
            Assignment->Dockable,
            Host,
            &(Assignment->AssignedRect),
            &(Frame->WorkRect)
        );
        if (CallbackStatus != DOCK_LAYOUT_STATUS_SUCCESS) {
            Frame->RejectedCount++;
            if (Status == DOCK_LAYOUT_STATUS_SUCCESS) Status = CallbackStatus;
            debug("[DockHostApplyLayoutFrame] callback rejected index=%u status=%u", Index, CallbackStatus);
        }
    }

    Host->WorkRect = Frame->WorkRect;
    Host->LayoutDirty = FALSE;
    Host->LastDirtyReason = DOCK_DIRTY_REASON_NONE;

    for (Index = 0; Index < Host->ItemCount; Index++) {
        if (Host->Items[Index] == NULL) continue;
        if (Host->Items[Index]->Callbacks.OnHostWorkRectChanged == NULL) continue;
        (void)Host->Items[Index]->Callbacks.OnHostWorkRectChanged(
            Host->Items[Index],
            Host,
            &(Host->WorkRect)
        );
    }

    Frame->Status = Status;
    DockHostFillResultFromFrame(Result, Frame);
    return Status;
}
