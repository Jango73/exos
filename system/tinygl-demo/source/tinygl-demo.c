/************************************************************************\

    EXOS TinyGL Demo
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

\************************************************************************/

#include "../../../runtime/include/exos/exos-runtime-main.h"
#include "../../../runtime/include/exos/exos.h"
#include "../../../third/tinygl/include/tinygl.h"

/************************************************************************/

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/************************************************************************/

double math_sin(double x) {
    double result = 0.0;
    double x2 = x * x;

    result = x * (1.0 - x2 / 6.0 + x2 * x2 / 120.0);
    return result;
}

/************************************************************************/

double math_cos(double x) { return math_sin(x + M_PI / 2.0); }

/************************************************************************/

double math_sqrt(double x) {
    double guess;
    int i;

    if (x <= 0.0) return 0.0;

    guess = x / 2.0;
    for (i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0;
    }

    return guess;
}

/************************************************************************/

double math_ceil(double x) {
    if (x < 0.0) {
        return (double)((int)x);
    }

    if (x > (int)x) {
        return (double)((int)x + 1);
    }

    return x;
}

/************************************************************************/

#define TINYGL_DEMO_STATE_PROP TEXT("tinygl.demo.state")
#define TINYGL_DEMO_DEFAULT_WIDTH 320
#define TINYGL_DEMO_DEFAULT_HEIGHT 240
#define TINYGL_DEMO_NEAR_PLANE 1.0
#define TINYGL_DEMO_FAR_PLANE 64.0
#define TINYGL_DEMO_HALF_FRUSTUM_HEIGHT 0.75
#define TINYGL_DEMO_CAMERA_DISTANCE -6.0f
#define TINYGL_DEMO_ROTATION_X_SPEED 42.0f
#define TINYGL_DEMO_ROTATION_Y_SPEED 75.0f
#define TINYGL_DEMO_SMALL_CUBE_SCALE 0.55f
#define TINYGL_DEMO_SMALL_CUBE_OFFSET_X 0.0f
#define TINYGL_DEMO_SMALL_CUBE_OFFSET_Y 1.0f
#define TINYGL_DEMO_SMALL_CUBE_OFFSET_Z 0.0f

#define TINYGL_DEMO_TIMER_ID_ANIMATE 1
#define TINYGL_DEMO_TIMER_INTERVAL_MS 16

#define VK_SPACE 0x60

/************************************************************************/

typedef enum VIEW_MODE { VIEW_MODE_UNLIT_CUBE = 1, VIEW_MODE_LIT_CUBE = 2 } VIEW_MODE;

typedef struct tag_TINYGL_DEMO_STATE {
    TGLContext Context;
    unsigned char* ColorBuffer;
    I32 SurfaceWidth;
    I32 SurfaceHeight;
    I32 SurfacePitch;
    VIEW_MODE ViewMode;
    GLfloat RotationX;
    GLfloat RotationY;
} TINYGL_DEMO_STATE, *LPTINYGL_DEMO_STATE;

/************************************************************************/

static const GLfloat LightPosition[4] = {2.0f, 2.0f, 2.0f, 1.0f};
static const GLfloat LightAmbient[4] = {0.10f, 0.10f, 0.10f, 1.0f};
static const GLfloat LightDiffuse[4] = {0.95f, 0.95f, 0.95f, 1.0f};
static const GLfloat LightSpecular[4] = {0.35f, 0.35f, 0.35f, 1.0f};
static const GLfloat MaterialAmbient[4] = {0.30f, 0.30f, 0.30f, 1.0f};
static const GLfloat MaterialDiffuse[4] = {0.80f, 0.80f, 0.80f, 1.0f};
static const GLfloat MaterialSpecular[4] = {0.25f, 0.25f, 0.25f, 1.0f};
static const GLfloat MaterialEmission[4] = {0.00f, 0.00f, 0.00f, 1.0f};
static const GLfloat MaterialShininess[1] = {12.0f};

/************************************************************************/

/**
 * @brief Get TinyGL demo state attached to the window.
 * @param Window Target window.
 * @return State pointer when available.
 */
static LPTINYGL_DEMO_STATE tinyglDemoGetState(HANDLE Window) {
    return (LPTINYGL_DEMO_STATE)GetWindowProp(Window, TINYGL_DEMO_STATE_PROP);
}

/************************************************************************/

/**
 * @brief Attach TinyGL demo state to one window.
 * @param Window Target window.
 * @param State State pointer.
 */
static void tinyglDemoSetState(HANDLE Window, LPTINYGL_DEMO_STATE State) {
    (void)SetWindowProp(Window, TINYGL_DEMO_STATE_PROP, (UINT)State);
}

/************************************************************************/

/**
 * @brief Apply the current rotation to the model transform.
 * @param State Demo state.
 */
static void tinyglDemoApplyCubeTransform(LPTINYGL_DEMO_STATE State) {
    if (State == NULL) return;

    glRotatef(State->RotationX, 1.0f, 0.0f, 0.0f);
    glRotatef(State->RotationY, 0.0f, 1.0f, 0.0f);
}

/************************************************************************/

/**
 * @brief Draw one colored triangle.
 */
static void tinyglDemoDrawTriangle(
    GLfloat Red, GLfloat Green, GLfloat Blue, GLfloat X1, GLfloat Y1, GLfloat Z1, GLfloat X2, GLfloat Y2, GLfloat Z2,
    GLfloat X3, GLfloat Y3, GLfloat Z3) {
    glColor3f(Red, Green, Blue);
    glVertex3f(X1, Y1, Z1);
    glVertex3f(X2, Y2, Z2);
    glVertex3f(X3, Y3, Z3);
}

/************************************************************************/

/**
 * @brief Draw one lit triangle.
 */
static void tinyglDemoDrawLitTriangle(
    GLfloat NormalX, GLfloat NormalY, GLfloat NormalZ, GLfloat Red, GLfloat Green, GLfloat Blue, GLfloat X1, GLfloat Y1,
    GLfloat Z1, GLfloat X2, GLfloat Y2, GLfloat Z2, GLfloat X3, GLfloat Y3, GLfloat Z3) {
    glNormal3f(NormalX, NormalY, NormalZ);
    tinyglDemoDrawTriangle(Red, Green, Blue, X1, Y1, Z1, X2, Y2, Z2, X3, Y3, Z3);
}

/************************************************************************/

/**
 * @brief Draw one cube using the selected shading mode.
 * @param Mode Selected view mode.
 */
static void tinyglDemoDrawCube(VIEW_MODE Mode) {
    glBegin(GL_TRIANGLES);

    if (Mode == VIEW_MODE_LIT_CUBE) {
        tinyglDemoDrawLitTriangle(
            0.0f, 0.0f, 1.0f, 0.90f, 0.20f, 0.18f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        tinyglDemoDrawLitTriangle(
            0.0f, 0.0f, 1.0f, 0.90f, 0.20f, 0.18f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f);

        tinyglDemoDrawLitTriangle(
            0.0f, 0.0f, -1.0f, 0.18f, 0.50f, 0.92f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f);
        tinyglDemoDrawLitTriangle(
            0.0f, 0.0f, -1.0f, 0.18f, 0.50f, 0.92f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f);

        tinyglDemoDrawLitTriangle(
            -1.0f, 0.0f, 0.0f, 0.25f, 0.78f, 0.35f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f);
        tinyglDemoDrawLitTriangle(
            -1.0f, 0.0f, 0.0f, 0.25f, 0.78f, 0.35f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f);

        tinyglDemoDrawLitTriangle(
            1.0f, 0.0f, 0.0f, 0.94f, 0.80f, 0.24f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f);
        tinyglDemoDrawLitTriangle(
            1.0f, 0.0f, 0.0f, 0.94f, 0.80f, 0.24f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f);

        tinyglDemoDrawLitTriangle(
            0.0f, 1.0f, 0.0f, 0.84f, 0.36f, 0.88f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        tinyglDemoDrawLitTriangle(
            0.0f, 1.0f, 0.0f, 0.84f, 0.36f, 0.88f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f);

        tinyglDemoDrawLitTriangle(
            0.0f, -1.0f, 0.0f, 0.20f, 0.78f, 0.82f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f);
        tinyglDemoDrawLitTriangle(
            0.0f, -1.0f, 0.0f, 0.20f, 0.78f, 0.82f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    } else {
        tinyglDemoDrawTriangle(0.90f, 0.20f, 0.18f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        tinyglDemoDrawTriangle(0.90f, 0.20f, 0.18f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f);

        tinyglDemoDrawTriangle(0.18f, 0.50f, 0.92f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f);
        tinyglDemoDrawTriangle(0.18f, 0.50f, 0.92f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f);

        tinyglDemoDrawTriangle(0.25f, 0.78f, 0.35f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f);
        tinyglDemoDrawTriangle(0.25f, 0.78f, 0.35f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f);

        tinyglDemoDrawTriangle(0.94f, 0.80f, 0.24f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, 1.0f);
        tinyglDemoDrawTriangle(0.94f, 0.80f, 0.24f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f);

        tinyglDemoDrawTriangle(0.84f, 0.36f, 0.88f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        tinyglDemoDrawTriangle(0.84f, 0.36f, 0.88f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f);

        tinyglDemoDrawTriangle(0.20f, 0.78f, 0.82f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f);
        tinyglDemoDrawTriangle(0.20f, 0.78f, 0.82f, -1.0f, -1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
    }

    glEnd();
}

/************************************************************************/

/**
 * @brief Initialize or resize TinyGL surface with safe ownership transfer.
 * @param State Demo state.
 * @param Width Surface width.
 * @param Height Surface height.
 * @return TRUE on success.
 */
static BOOL tinyglDemoAllocateSurface(LPTINYGL_DEMO_STATE State, I32 Width, I32 Height) {
    size_t BufferSize;
    unsigned char* NewBuffer;
    TGL_SURFACE_DESC Surface;

    if (State == NULL) return FALSE;
    if (State->Context == NULL) return FALSE;
    if (Width <= 0 || Height <= 0) return FALSE;

    BufferSize = (size_t)Width * (size_t)Height * 4;
    NewBuffer = (unsigned char*)malloc(BufferSize);
    if (NewBuffer == NULL) {
        return FALSE;
    }

    memset(NewBuffer, 0, BufferSize);

    Surface.Pixels = NewBuffer;
    Surface.Width = Width;
    Surface.Height = Height;
    Surface.Pitch = Width * 4;
    Surface.PixelFormat = TGL_PIXEL_FORMAT_XRGB8888;

    if (tinyglSetSurface(State->Context, &Surface) != TGL_RESULT_OK) {
        free(NewBuffer);
        return FALSE;
    }

    if (State->ColorBuffer != NULL) {
        free(State->ColorBuffer);
    }

    State->ColorBuffer = NewBuffer;
    State->SurfaceWidth = Width;
    State->SurfaceHeight = Height;
    State->SurfacePitch = Width * 4;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Render one animation frame.
 * @param State Demo state.
 * @param DeltaSeconds Frame delta time.
 */
static void tinyglDemoRenderFrame(LPTINYGL_DEMO_STATE State, GLfloat DeltaSeconds) {
    GLdouble AspectRatio;
    GLdouble HalfWidth;

    if (State == NULL) return;
    if (State->SurfaceWidth <= 0 || State->SurfaceHeight <= 0) return;

    State->RotationX += DeltaSeconds * TINYGL_DEMO_ROTATION_X_SPEED;
    State->RotationY += DeltaSeconds * TINYGL_DEMO_ROTATION_Y_SPEED;

    AspectRatio = (GLdouble)State->SurfaceWidth / (GLdouble)State->SurfaceHeight;
    HalfWidth = TINYGL_DEMO_HALF_FRUSTUM_HEIGHT * AspectRatio;

    glViewport(0, 0, State->SurfaceWidth, State->SurfaceHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(
        -HalfWidth, HalfWidth, -TINYGL_DEMO_HALF_FRUSTUM_HEIGHT, TINYGL_DEMO_HALF_FRUSTUM_HEIGHT,
        TINYGL_DEMO_NEAR_PLANE, TINYGL_DEMO_FAR_PLANE);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, TINYGL_DEMO_CAMERA_DISTANCE);

    if (State->ViewMode == VIEW_MODE_LIT_CUBE) {
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);

        glLightfv(GL_LIGHT0, GL_POSITION, LightPosition);
        glLightfv(GL_LIGHT0, GL_AMBIENT, LightAmbient);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, LightDiffuse);
        glLightfv(GL_LIGHT0, GL_SPECULAR, LightSpecular);

        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, MaterialAmbient);
        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, MaterialDiffuse);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, MaterialSpecular);
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, MaterialEmission);
        glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, MaterialShininess);
    } else {
        glDisable(GL_LIGHT0);
        glDisable(GL_LIGHTING);
    }

    glPushMatrix();
    tinyglDemoApplyCubeTransform(State);
    tinyglDemoDrawCube(State->ViewMode);
    glPopMatrix();

    glPushMatrix();
    tinyglDemoApplyCubeTransform(State);
    glTranslatef(TINYGL_DEMO_SMALL_CUBE_OFFSET_X, TINYGL_DEMO_SMALL_CUBE_OFFSET_Y, TINYGL_DEMO_SMALL_CUBE_OFFSET_Z);
    glScalef(TINYGL_DEMO_SMALL_CUBE_SCALE, TINYGL_DEMO_SMALL_CUBE_SCALE, TINYGL_DEMO_SMALL_CUBE_SCALE);
    tinyglDemoDrawCube(State->ViewMode);
    glPopMatrix();

    glFlush();
}

/************************************************************************/

/**
 * @brief Copy TinyGL color buffer to window graphics surface.
 * @param State Demo state.
 * @param SurfaceInfo Target graphics surface.
 */
static void tinyglDemoCopySurfaceToGC(LPTINYGL_DEMO_STATE State, GC_SURFACE_INFO* SurfaceInfo) {
    I32 Y;
    I32 MinHeight;
    I32 RowSize;
    unsigned char* Src;
    unsigned char* Dst;

    if (State == NULL || SurfaceInfo == NULL) return;
    if (State->ColorBuffer == NULL || SurfaceInfo->MemoryBase == NULL) return;

    MinHeight = State->SurfaceHeight;
    if (MinHeight > SurfaceInfo->Height) {
        MinHeight = SurfaceInfo->Height;
    }

    RowSize = State->SurfacePitch;
    if (RowSize > SurfaceInfo->Pitch) {
        RowSize = SurfaceInfo->Pitch;
    }

    for (Y = 0; Y < MinHeight; Y++) {
        Src = State->ColorBuffer + Y * State->SurfacePitch;
        Dst = SurfaceInfo->MemoryBase + Y * SurfaceInfo->Pitch;
        memcpy(Dst, Src, RowSize);
    }
}

/************************************************************************/

/**
 * @brief Release TinyGL demo resources.
 * @param State Demo state.
 */
static void tinyglDemoDestroyState(LPTINYGL_DEMO_STATE State) {
    if (State == NULL) return;

    if (State->ColorBuffer != NULL) {
        free(State->ColorBuffer);
        State->ColorBuffer = NULL;
    }

    if (State->Context != NULL) {
        tinyglDestroyContext(State->Context);
        State->Context = NULL;
    }

    free(State);
}

/************************************************************************/

/**
 * @brief Initialize TinyGL demo state when the window is created.
 * @param Window Target window.
 * @return TRUE on success.
 */
static BOOL tinyglDemoCreate(HANDLE Window) {
    TGL_CONTEXT_DESC ContextDesc;
    RECT ClientRect;
    LPTINYGL_DEMO_STATE State;

    State = (LPTINYGL_DEMO_STATE)malloc(sizeof(TINYGL_DEMO_STATE));
    if (State == NULL) {
        return FALSE;
    }

    memset(State, 0, sizeof(TINYGL_DEMO_STATE));

    ContextDesc.MaxWidth = TINYGL_DEMO_DEFAULT_WIDTH;
    ContextDesc.MaxHeight = TINYGL_DEMO_DEFAULT_HEIGHT;
    ContextDesc.HasDepthBuffer = TRUE;
    ContextDesc.HasColorBuffer = TRUE;

    if (tinyglCreateContext(&ContextDesc, &State->Context) != TGL_RESULT_OK) {
        tinyglDemoDestroyState(State);
        return FALSE;
    }

    if (tinyglMakeCurrent(State->Context) != TGL_RESULT_OK) {
        tinyglDemoDestroyState(State);
        return FALSE;
    }

    State->ViewMode = VIEW_MODE_UNLIT_CUBE;
    State->RotationX = 0.0f;
    State->RotationY = 0.0f;

    tinyglDemoSetState(Window, State);

    if (GetWindowClientRect(Window, &ClientRect) == FALSE) {
        tinyglDemoSetState(Window, NULL);
        tinyglDemoDestroyState(State);
        return FALSE;
    }

    if (tinyglDemoAllocateSurface(State, ClientRect.X2 - ClientRect.X1 + 1, ClientRect.Y2 - ClientRect.Y1 + 1) ==
        FALSE) {
        tinyglDemoSetState(Window, NULL);
        tinyglDemoDestroyState(State);
        return FALSE;
    }

    (void)SetWindowCaption(Window, TEXT("TinyGL Demo"));
    (void)SetWindowTimer(Window, TINYGL_DEMO_TIMER_ID_ANIMATE, TINYGL_DEMO_TIMER_INTERVAL_MS);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Main window procedure for TinyGL demo.
 * @param Window Target window handle.
 * @param Message Window message.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
static U32 MainWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    LPTINYGL_DEMO_STATE State;

    UNUSED(Param2);

    switch (Message) {
        case EWM_CREATE:
            return tinyglDemoCreate(Window) ? 1 : 0;

        case EWM_DRAW: {
            HANDLE GC;
            GC_SURFACE_INFO SurfaceInfo;

            State = tinyglDemoGetState(Window);
            if (State == NULL) {
                return 1;
            }

            GC = GetWindowGC(Window);
            if (GC == NULL) {
                return 1;
            }

            SurfaceInfo.Header.Size = sizeof(SurfaceInfo);
            SurfaceInfo.Header.Version = EXOS_ABI_VERSION;
            SurfaceInfo.Header.Flags = 0;
            SurfaceInfo.GC = GC;
            SurfaceInfo.Width = 0;
            SurfaceInfo.Height = 0;
            SurfaceInfo.Pitch = 0;
            SurfaceInfo.MemoryBase = NULL;

            if (GetGCSurface(GC, &SurfaceInfo)) {
                (void)tinyglMakeCurrent(State->Context);

                if (State->SurfaceWidth != SurfaceInfo.Width || State->SurfaceHeight != SurfaceInfo.Height) {
                    (void)tinyglDemoAllocateSurface(State, SurfaceInfo.Width, SurfaceInfo.Height);
                }

                if (State->ColorBuffer != NULL && SurfaceInfo.MemoryBase != NULL) {
                    tinyglDemoRenderFrame(State, 1.0f / 60.0f);
                    tinyglDemoCopySurfaceToGC(State, &SurfaceInfo);
                }
            }

            ReleaseWindowGC(Window);
            return 1;
        }

        case EWM_TIMER:
            if (Param1 == TINYGL_DEMO_TIMER_ID_ANIMATE) {
                (void)InvalidateClientRect(Window, NULL);
            }
            return 1;

        case EWM_KEYDOWN:
            if (Param1 == VK_SPACE) {
                State = tinyglDemoGetState(Window);
                if (State == NULL) {
                    return 1;
                }

                if (State->ViewMode == VIEW_MODE_UNLIT_CUBE) {
                    State->ViewMode = VIEW_MODE_LIT_CUBE;
                } else {
                    State->ViewMode = VIEW_MODE_UNLIT_CUBE;
                }

                (void)InvalidateClientRect(Window, NULL);
            }
            return 1;

        case EWM_DELETE:
            (void)KillWindowTimer(Window, TINYGL_DEMO_TIMER_ID_ANIMATE);
            State = tinyglDemoGetState(Window);
            if (State != NULL) {
                tinyglDemoSetState(Window, NULL);
                tinyglDemoDestroyState(State);
            }
            return 1;

        default:
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }
}

/************************************************************************/

/**
 * @brief Initialize and show the TinyGL demo main window.
 * @return TRUE on success.
 */
static BOOL initApplication(void) {
    HANDLE MainWindow;

    MainWindow = CreateWindowWithClass(
        NULL, 0, NULL, MainWindowFunc, EWS_VISIBLE | EWS_CLIENT_DECORATED, 0, 100, 100, TINYGL_DEMO_DEFAULT_WIDTH,
        TINYGL_DEMO_DEFAULT_HEIGHT);
    if (MainWindow == NULL) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

int main(int argc, char** argv) {
    MESSAGE Message;

    UNUSED(argc);
    UNUSED(argv);

    if (!initApplication()) {
        return MAX_U32;
    }

    while (GetMessage(NULL, &Message, 0, 0)) {
        DispatchMessage(&Message);
    }

    return 0;
}

/************************************************************************/
