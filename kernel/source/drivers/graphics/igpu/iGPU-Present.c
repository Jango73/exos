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


    Intel graphics (drawing, surfaces and present)

\************************************************************************/

#include "iGPU-Internal.h"

#include "text/CoreString.h"
#include "system/System.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "drivers/graphics/common/Graphics-TextRenderer.h"

/************************************************************************/

static INTEL_GFX_SURFACE IntelGfxSurfaces[INTEL_GFX_MAX_SURFACES] = {0};

/************************************************************************/

UINT IntelGfxFlushContextRegionToScanout(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, U32 Width, U32 Height) {
    U32 BytesPerPixel = 0;
    U32 CopyBytes = 0;
    U32 Row = 0;

    if (Context == NULL || Context->MemoryBase == NULL || Width == 0 || Height == 0) {
        return DF_RETURN_GENERIC;
    }

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    if (Context->MemoryBase == (U8*)(LINEAR)IntelGfxState.FrameBufferLinear) {
        return DF_RETURN_SUCCESS;
    }

    if (X < 0 || Y < 0) {
        return DF_RETURN_GENERIC;
    }

    if ((U32)X + Width > IntelGfxState.ActiveWidth || (U32)Y + Height > IntelGfxState.ActiveHeight) {
        return DF_RETURN_GENERIC;
    }

    BytesPerPixel = Context->BitsPerPixel / 8;
    if (BytesPerPixel == 0) {
        return DF_RETURN_GENERIC;
    }

    CopyBytes = Width * BytesPerPixel;
    for (Row = 0; Row < Height; Row++) {
        U32 SourceOffset = ((U32)Y + Row) * (U32)Context->BytesPerScanLine + ((U32)X * BytesPerPixel);
        U32 DestinationOffset = ((U32)Y + Row) * IntelGfxState.ActiveStride + ((U32)X * BytesPerPixel);
        U8* Source = Context->MemoryBase + SourceOffset;
        U8* Destination = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear + DestinationOffset;
        if (BlitMemoryAsm(Destination, Source, CopyBytes) == FALSE) {
            MemoryCopy(Destination, Source, CopyBytes);
        }
    }

    IntelGfxNotePresentBlit();
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static BOOL IntelGfxEnsureShadowFrameBufferSize(UINT RequiredSize) {
    if (RequiredSize == 0) {
        return FALSE;
    }

    if (IntelGfxState.ShadowFrameBufferLinear != 0 && IntelGfxState.ShadowFrameBufferSize != RequiredSize) {
        FreeRegion(IntelGfxState.ShadowFrameBufferLinear, IntelGfxState.ShadowFrameBufferSize);
        IntelGfxState.ShadowFrameBufferLinear = 0;
        IntelGfxState.ShadowFrameBufferSize = 0;
    }

    if (IntelGfxState.ShadowFrameBufferLinear == 0) {
        IntelGfxState.ShadowFrameBufferLinear = AllocRegion(VMA_KERNEL,
                                                            0,
                                                            RequiredSize,
                                                            ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
                                                            TEXT("IntelGfxShadowFrameBuffer"));
        if (IntelGfxState.ShadowFrameBufferLinear == 0) {
            ERROR(TEXT("AllocRegion failed size=%u"), RequiredSize);
            return FALSE;
        }

        IntelGfxState.ShadowFrameBufferSize = RequiredSize;
    }

    return TRUE;
}

/************************************************************************/

UINT IntelGfxScrollRegionViaShadow(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_REGION_INFO Info) {
    GRAPHICSCONTEXT ShadowContext;
    I32 PixelX = 0;
    I32 PixelY = 0;
    U32 PixelWidth = 0;
    U32 PixelHeight = 0;
    U32 BytesPerPixel = 0;
    U32 Row = 0;
    U32 RowBytes = 0;
    BOOL Result = FALSE;
    U8* FrameBuffer = NULL;
    U8* ShadowBuffer = NULL;
    UINT FrameSize = 0;

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) {
        return DF_RETURN_GENERIC;
    }

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    PixelX = (I32)(Info->CellX * Info->GlyphCellWidth);
    PixelY = (I32)(Info->CellY * Info->GlyphCellHeight);
    PixelWidth = Info->RegionCellWidth * Info->GlyphCellWidth;
    PixelHeight = Info->RegionCellHeight * Info->GlyphCellHeight;

    if (PixelWidth == 0 || PixelHeight == 0 || PixelX < 0 || PixelY < 0) {
        return DF_RETURN_GENERIC;
    }

    if ((U32)PixelX + PixelWidth > IntelGfxState.ActiveWidth || (U32)PixelY + PixelHeight > IntelGfxState.ActiveHeight) {
        return DF_RETURN_GENERIC;
    }

    FrameSize = IntelGfxState.FrameBufferSize;
    if (!IntelGfxEnsureShadowFrameBufferSize(FrameSize)) {
        return DF_RETURN_UNEXPECTED;
    }

    BytesPerPixel = Context->BitsPerPixel / 8;
    if (BytesPerPixel == 0) {
        return DF_RETURN_GENERIC;
    }

    RowBytes = PixelWidth * BytesPerPixel;
    FrameBuffer = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear;
    ShadowBuffer = (U8*)(LINEAR)IntelGfxState.ShadowFrameBufferLinear;

    for (Row = 0; Row < PixelHeight; Row++) {
        U32 Offset = ((U32)PixelY + Row) * IntelGfxState.ActiveStride + ((U32)PixelX * BytesPerPixel);
        if (BlitMemoryAsm(ShadowBuffer + Offset, FrameBuffer + Offset, RowBytes) == FALSE) {
            MemoryCopy(ShadowBuffer + Offset, FrameBuffer + Offset, RowBytes);
        }
    }

    ShadowContext = *Context;
    ShadowContext.MemoryBase = ShadowBuffer;

    Result = GfxTextScrollRegion(&ShadowContext, Info);
    if (!Result) {
        return DF_RETURN_GENERIC;
    }

    for (Row = 0; Row < PixelHeight; Row++) {
        U32 Offset = ((U32)PixelY + Row) * IntelGfxState.ActiveStride + ((U32)PixelX * BytesPerPixel);
        if (BlitMemoryAsm(FrameBuffer + Offset, ShadowBuffer + Offset, RowBytes) == FALSE) {
            MemoryCopy(FrameBuffer + Offset, ShadowBuffer + Offset, RowBytes);
        }
    }

    IntelGfxNotePresentBlit();
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static U32 IntelGfxGetSurfaceBytesPerPixel(U32 Format) {
    switch (Format) {
        case GFX_FORMAT_UNKNOWN:
        case GFX_FORMAT_XRGB8888:
        case GFX_FORMAT_ARGB8888:
            return 4;
    }

    return 0;
}

/************************************************************************/

static LPINTEL_GFX_SURFACE IntelGfxFindSurface(U32 SurfaceId) {
    UINT Index = 0;

    if (SurfaceId == 0) {
        return NULL;
    }

    for (Index = 0; Index < INTEL_GFX_MAX_SURFACES; Index++) {
        if (IntelGfxSurfaces[Index].InUse && IntelGfxSurfaces[Index].SurfaceId == SurfaceId) {
            return &IntelGfxSurfaces[Index];
        }
    }

    return NULL;
}

/************************************************************************/

static LPINTEL_GFX_SURFACE IntelGfxAllocateSurfaceSlot(void) {
    UINT Index = 0;

    for (Index = 0; Index < INTEL_GFX_MAX_SURFACES; Index++) {
        if (!IntelGfxSurfaces[Index].InUse) {
            return &IntelGfxSurfaces[Index];
        }
    }

    return NULL;
}

/************************************************************************/

static U32 IntelGfxGenerateSurfaceId(void) {
    U32 Candidate = 0;
    UINT Attempt = 0;

    if (IntelGfxState.NextSurfaceId < INTEL_GFX_SURFACE_FIRST_ID) {
        IntelGfxState.NextSurfaceId = INTEL_GFX_SURFACE_FIRST_ID;
    }

    for (Attempt = 0; Attempt < MAX_U32; Attempt++) {
        Candidate = IntelGfxState.NextSurfaceId++;
        if (Candidate < INTEL_GFX_SURFACE_FIRST_ID) {
            IntelGfxState.NextSurfaceId = INTEL_GFX_SURFACE_FIRST_ID;
            Candidate = IntelGfxState.NextSurfaceId++;
        }

        if (IntelGfxFindSurface(Candidate) == NULL) {
            return Candidate;
        }
    }

    return 0;
}

/************************************************************************/

static void IntelGfxReleaseSurface(LPINTEL_GFX_SURFACE Surface) {
    if (Surface == NULL || !Surface->InUse) {
        return;
    }

    if (Surface->MemoryBase != NULL) {
        FreeRegion((LINEAR)Surface->MemoryBase, Surface->SizeBytes);
    }

    *Surface = (INTEL_GFX_SURFACE){0};
}

/************************************************************************/

void IntelGfxReleaseAllSurfaces(void) {
    UINT Index = 0;

    for (Index = 0; Index < INTEL_GFX_MAX_SURFACES; Index++) {
        IntelGfxReleaseSurface(&IntelGfxSurfaces[Index]);
    }

    IntelGfxState.ScanoutSurfaceId = 0;
    IntelGfxState.NextSurfaceId = INTEL_GFX_SURFACE_FIRST_ID;

    IntelGfxTextShutdownRuntime();
}

/************************************************************************/

static BOOL IntelGfxResolveDirtyRegion(LPRECT DirtyRect, LPINTEL_GFX_SURFACE Surface, U32* X, U32* Y, U32* Width, U32* Height) {
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;

    if (Surface == NULL || X == NULL || Y == NULL || Width == NULL || Height == NULL) {
        return FALSE;
    }

    if (DirtyRect != NULL) {
        X1 = DirtyRect->X1;
        Y1 = DirtyRect->Y1;
        X2 = DirtyRect->X2;
        Y2 = DirtyRect->Y2;
    }

    if (DirtyRect == NULL || X2 < X1 || Y2 < Y1) {
        X1 = 0;
        Y1 = 0;
        X2 = (I32)Surface->Width - 1;
        Y2 = (I32)Surface->Height - 1;
    }

    if (X1 < 0) X1 = 0;
    if (Y1 < 0) Y1 = 0;
    if (X2 >= (I32)Surface->Width) X2 = (I32)Surface->Width - 1;
    if (Y2 >= (I32)Surface->Height) Y2 = (I32)Surface->Height - 1;
    if (X2 < X1 || Y2 < Y1) {
        return FALSE;
    }

    *X = (U32)X1;
    *Y = (U32)Y1;
    *Width = (U32)(X2 - X1 + 1);
    *Height = (U32)(Y2 - Y1 + 1);
    return TRUE;
}

/************************************************************************/

static UINT IntelGfxBlitSurfaceRegionToScanout(LPINTEL_GFX_SURFACE Surface, U32 X, U32 Y, U32 Width, U32 Height) {
    U32 Row = 0;
    U32 CopyBytes = 0;

    if (Surface == NULL || Surface->MemoryBase == NULL || Width == 0 || Height == 0) {
        return DF_RETURN_GENERIC;
    }

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    if (X + Width > IntelGfxState.ActiveWidth || Y + Height > IntelGfxState.ActiveHeight || X + Width > Surface->Width ||
        Y + Height > Surface->Height) {
        return DF_RETURN_GENERIC;
    }

    CopyBytes = Width << 2;
    for (Row = 0; Row < Height; Row++) {
        U32 SourceOffset = (Y + Row) * Surface->Pitch + (X << 2);
        U32 DestinationOffset = (Y + Row) * IntelGfxState.ActiveStride + (X << 2);
        U8* Source = Surface->MemoryBase + SourceOffset;
        U8* Destination = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear + DestinationOffset;
        if (BlitMemoryAsm(Destination, Source, CopyBytes) == FALSE) {
            MemoryCopy(Destination, Source, CopyBytes);
        }
    }

    IntelGfxNotePresentBlit();
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxAllocateSurface(LPGFX_SURFACE_INFO Info) {
    LPINTEL_GFX_SURFACE Surface = NULL;
    U32 BytesPerPixel = 0;
    U32 Format = 0;
    U32 Width = 0;
    U32 Height = 0;
    U32 Pitch = 0;
    U32 SizeBytes = 0;
    U32 Flags = 0;
    LINEAR MemoryLinear = 0;
    U8* Memory = NULL;
    U32 SurfaceId = 0;

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    SAFE_USE(Info) {
        Width = Info->Width;
        Height = Info->Height;
        Format = (Info->Format == GFX_FORMAT_UNKNOWN) ? GFX_FORMAT_XRGB8888 : Info->Format;
        Flags = Info->Flags;
    }

    if (Width == 0 || Height == 0) {
        return DF_RETURN_GENERIC;
    }

    if (Width > IntelGfxState.Capabilities.MaxWidth || Height > IntelGfxState.Capabilities.MaxHeight || Width > IntelGfxState.ActiveWidth ||
        Height > IntelGfxState.ActiveHeight) {
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    BytesPerPixel = IntelGfxGetSurfaceBytesPerPixel(Format);
    if (BytesPerPixel == 0) {
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (Width > MAX_U32 / BytesPerPixel) {
        return DF_RETURN_GENERIC;
    }

    Pitch = Width * BytesPerPixel;
    if (Height > MAX_U32 / Pitch) {
        return DF_RETURN_GENERIC;
    }
    SizeBytes = Pitch * Height;

    Surface = IntelGfxAllocateSurfaceSlot();
    if (Surface == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    SurfaceId = IntelGfxGenerateSurfaceId();
    if (SurfaceId == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    MemoryLinear = AllocRegion(VMA_KERNEL,
                               0,
                               SizeBytes,
                               ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
                               TEXT("IntelGfxSurface"));
    if (MemoryLinear == 0) {
        return DF_RETURN_UNEXPECTED;
    }
    Memory = (U8*)(LINEAR)MemoryLinear;
    MemorySet(Memory, 0, SizeBytes);

    *Surface = (INTEL_GFX_SURFACE){
        .InUse = TRUE,
        .SurfaceId = SurfaceId,
        .Width = Width,
        .Height = Height,
        .Format = Format,
        .Pitch = Pitch,
        .Flags = Flags | GFX_SURFACE_FLAG_CPU_VISIBLE,
        .SizeBytes = SizeBytes,
        .MemoryBase = Memory
    };

    SAFE_USE(Info) {
        Info->SurfaceId = Surface->SurfaceId;
        Info->Format = Surface->Format;
        Info->Pitch = Surface->Pitch;
        Info->MemoryBase = Surface->MemoryBase;
        Info->Flags = Surface->Flags;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxFreeSurface(LPGFX_SURFACE_INFO Info) {
    U32 SurfaceId = 0;
    LPINTEL_GFX_SURFACE Surface = NULL;

    SAFE_USE(Info) { SurfaceId = Info->SurfaceId; }
    if (SurfaceId == 0) {
        return DF_RETURN_GENERIC;
    }

    Surface = IntelGfxFindSurface(SurfaceId);
    if (Surface == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    if (IntelGfxState.ScanoutSurfaceId == SurfaceId) {
        IntelGfxState.ScanoutSurfaceId = 0;
    }

    IntelGfxReleaseSurface(Surface);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxSetScanout(LPGFX_SCANOUT_INFO Info) {
    LPINTEL_GFX_SURFACE Surface = NULL;

    SAFE_USE(Info) { Surface = IntelGfxFindSurface(Info->SurfaceId); }
    if (Surface == NULL) {
        return DF_RETURN_GENERIC;
    }

    if (Surface->Width != IntelGfxState.ActiveWidth || Surface->Height != IntelGfxState.ActiveHeight) {
        WARNING(TEXT("Surface dimensions mismatch (%ux%u expected=%ux%u)"),
            Surface->Width,
            Surface->Height,
            IntelGfxState.ActiveWidth,
            IntelGfxState.ActiveHeight);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    IntelGfxState.ScanoutSurfaceId = Surface->SurfaceId;

    SAFE_USE(Info) {
        Info->Width = Surface->Width;
        Info->Height = Surface->Height;
        Info->Format = Surface->Format;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxPresent(LPGFX_PRESENT_INFO Info) {
    LPINTEL_GFX_SURFACE Surface = NULL;
    LPGRAPHICSCONTEXT SourceContext = NULL;
    INTEL_GFX_SURFACE TemporarySurface = {0};
    RECT DirtyRect = {0};
    U32 SourceSurfaceId = 0;
    U32 X = 0;
    U32 Y = 0;
    U32 Width = 0;
    U32 Height = 0;
    U32 PresentFlags = 0;
    UINT Result = DF_RETURN_SUCCESS;

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    if (Info == NULL) {
        return DF_RETURN_GENERIC;
    }

    SourceSurfaceId = Info->SurfaceId;
    DirtyRect = Info->DirtyRect;
    PresentFlags = Info->Flags;
    SourceContext = (LPGRAPHICSCONTEXT)Info->GC;

    if (SourceContext != NULL && SourceContext->TypeID == KOID_GRAPHICSCONTEXT && SourceContext->MemoryBase != NULL &&
        SourceSurfaceId == 0) {
        TemporarySurface = (INTEL_GFX_SURFACE){
            .InUse = TRUE,
            .Width = (U32)SourceContext->Width,
            .Height = (U32)SourceContext->Height,
            .Format = (SourceContext->BitsPerPixel == 32) ? GFX_FORMAT_XRGB8888 :
                      (SourceContext->BitsPerPixel == 24) ? GFX_FORMAT_RGB888 :
                                                            GFX_FORMAT_RGB565,
            .Pitch = SourceContext->BytesPerScanLine,
            .MemoryBase = SourceContext->MemoryBase
        };

        if (!IntelGfxResolveDirtyRegion(&DirtyRect, &TemporarySurface, &X, &Y, &Width, &Height)) {
            return DF_RETURN_SUCCESS;
        }

        if ((PresentFlags & GFX_PRESENT_FLAG_WAIT_VBLANK) != 0) {
            Result = IntelGfxWaitForNextVBlank(INTEL_GFX_WAIT_VBLANK_DEFAULT_TIMEOUT_MS, NULL);
            if (Result != DF_RETURN_SUCCESS) {
                return Result;
            }
        }

        return IntelGfxFlushContextRegionToScanout(SourceContext, (I32)X, (I32)Y, Width, Height);
    }

    if (SourceSurfaceId == 0) {
        SourceSurfaceId = IntelGfxState.ScanoutSurfaceId;
    }

    if (SourceSurfaceId == 0) {
        return DF_RETURN_SUCCESS;
    }

    Surface = IntelGfxFindSurface(SourceSurfaceId);
    if (Surface == NULL || Surface->MemoryBase == NULL) {
        return DF_RETURN_GENERIC;
    }

    if (!IntelGfxResolveDirtyRegion(&DirtyRect, Surface, &X, &Y, &Width, &Height)) {
        return DF_RETURN_SUCCESS;
    }

    if ((PresentFlags & GFX_PRESENT_FLAG_WAIT_VBLANK) != 0) {
        Result = IntelGfxWaitForNextVBlank(INTEL_GFX_WAIT_VBLANK_DEFAULT_TIMEOUT_MS, NULL);
        if (Result != DF_RETURN_SUCCESS) {
            return Result;
        }
    }

    LockMutex(&(IntelGfxState.Context.Mutex), INFINITY);
    Result = IntelGfxBlitSurfaceRegionToScanout(Surface, X, Y, Width, Height);
    UnlockMutex(&(IntelGfxState.Context.Mutex));
    return Result;
}

/************************************************************************/
