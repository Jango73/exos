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


    3D math primitives

\************************************************************************/

#ifndef MATH3D_H_INCLUDED
#define MATH3D_H_INCLUDED

/************************************************************************/

#include "exos.h"

/************************************************************************/

typedef struct tag_VECTOR3 {
    F32 X;
    F32 Y;
    F32 Z;
} VECTOR3, *LPVECTOR3;

typedef struct tag_QUAT4 {
    F32 X;
    F32 Y;
    F32 Z;
    F32 W;
} QUAT4, *LPQUAT4;

typedef struct tag_MATRIX4 {
    F32 M11;
    F32 M12;
    F32 M13;
    F32 M14;
    F32 M21;
    F32 M22;
    F32 M23;
    F32 M24;
    F32 M31;
    F32 M32;
    F32 M33;
    F32 M34;
    F32 M41;
    F32 M42;
    F32 M43;
    F32 M44;
} MATRIX4, *LPMATRIX4;

/************************************************************************/

VECTOR3 Math3DVector3(F32 X, F32 Y, F32 Z);
VECTOR3 Math3DVector3Add(VECTOR3 A, VECTOR3 B);
VECTOR3 Math3DVector3Subtract(VECTOR3 A, VECTOR3 B);
VECTOR3 Math3DVector3Scale(VECTOR3 Value, F32 Scale);
F32 Math3DVector3Dot(VECTOR3 A, VECTOR3 B);
VECTOR3 Math3DVector3Cross(VECTOR3 A, VECTOR3 B);
VECTOR3 Math3DVector3Normalize(VECTOR3 Value);

QUAT4 Math3DQuat4Identity(void);
QUAT4 Math3DQuat4Normalize(QUAT4 Value);
QUAT4 Math3DQuat4FromEulerXYZRadians(VECTOR3 EulerRadians);

MATRIX4 Math3DMatrix4Identity(void);
MATRIX4 Math3DMatrix4Multiply(MATRIX4 A, MATRIX4 B);
MATRIX4 Math3DMatrix4FromQuat4(QUAT4 Rotation);
MATRIX4 Math3DMatrix4ComposeTRS(VECTOR3 Translation, VECTOR3 EulerRadians, VECTOR3 Scale);
VECTOR3 Math3DMatrix4TransformPoint(MATRIX4 Matrix, VECTOR3 Point);

/************************************************************************/

#endif
