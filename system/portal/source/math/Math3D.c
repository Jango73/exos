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

#include "math/Math3D.h"

/************************************************************************/

#define MATH_PI_F32 3.14159265358979323846f
#define MATH_TWO_PI_F32 6.28318530717958647692f
#define MATH_HALF_PI_F32 1.57079632679489661923f
#define MATH_EPSILON_F32 0.000001f

/************************************************************************/

/**
 * @brief Normalize one F32 angle to [0, 2PI).
 * @param Radians Angle in radians.
 * @return Normalized angle.
 */
static F32 NormalizeRadiansPositiveF32(F32 Radians) {
    I32 Turns = (I32)(Radians / MATH_TWO_PI_F32);
    F32 Local = Radians - ((F32)Turns * MATH_TWO_PI_F32);

    while (Local < 0.0f) Local += MATH_TWO_PI_F32;
    while (Local >= MATH_TWO_PI_F32) Local -= MATH_TWO_PI_F32;
    return Local;
}

/************************************************************************/

/**
 * @brief Normalize one F32 angle to [-PI, PI].
 * @param Radians Angle in radians.
 * @return Normalized angle.
 */
static F32 NormalizeRadiansSignedF32(F32 Radians) {
    F32 Local = NormalizeRadiansPositiveF32(Radians);
    if (Local > MATH_PI_F32) Local -= MATH_TWO_PI_F32;
    return Local;
}

/************************************************************************/

/**
 * @brief Compute sine for one F32 angle in radians.
 * @param Radians Angle in radians.
 * @return Approximate sine.
 */
static F32 MathSinF32(F32 Radians) {
    F32 X = NormalizeRadiansSignedF32(Radians);
    F32 X2 = X * X;

    return X * (1.0f + X2 * (-0.16666667f + X2 * (0.0083333310f + X2 * (-0.0001984090f))));
}

/************************************************************************/

/**
 * @brief Compute cosine for one F32 angle in radians.
 * @param Radians Angle in radians.
 * @return Approximate cosine.
 */
static F32 MathCosF32(F32 Radians) {
    F32 X = NormalizeRadiansSignedF32(Radians);
    F32 X2 = X * X;

    return 1.0f + X2 * (-0.5f + X2 * (0.041666638f + X2 * (-0.0013888378f)));
}

/************************************************************************/

/**
 * @brief Compute square root for one F32 value.
 * @param Value Input value.
 * @return Square root or 0 for non-positive input.
 */
static F32 MathSqrtF32(F32 Value) {
    F32 Guess;
    UINT Iteration;

    if (Value <= 0.0f) {
        return 0.0f;
    }

    Guess = (Value >= 1.0f) ? Value : 1.0f;
    for (Iteration = 0; Iteration < 8; Iteration++) {
        Guess = 0.5f * (Guess + (Value / Guess));
    }

    return Guess;
}

/************************************************************************/

/**
 * @brief Build one VECTOR3.
 * @param X X component.
 * @param Y Y component.
 * @param Z Z component.
 * @return Packed vector.
 */
VECTOR3 Math3DVector3(F32 X, F32 Y, F32 Z) {
    VECTOR3 Result;

    Result.X = X;
    Result.Y = Y;
    Result.Z = Z;
    return Result;
}

/************************************************************************/

/**
 * @brief Add two vectors.
 * @param A First vector.
 * @param B Second vector.
 * @return A + B.
 */
VECTOR3 Math3DVector3Add(VECTOR3 A, VECTOR3 B) {
    return Math3DVector3(A.X + B.X, A.Y + B.Y, A.Z + B.Z);
}

/************************************************************************/

/**
 * @brief Subtract two vectors.
 * @param A First vector.
 * @param B Second vector.
 * @return A - B.
 */
VECTOR3 Math3DVector3Subtract(VECTOR3 A, VECTOR3 B) {
    return Math3DVector3(A.X - B.X, A.Y - B.Y, A.Z - B.Z);
}

/************************************************************************/

/**
 * @brief Multiply one vector by one scalar.
 * @param Value Source vector.
 * @param Scale Scalar multiplier.
 * @return Scaled vector.
 */
VECTOR3 Math3DVector3Scale(VECTOR3 Value, F32 Scale) {
    return Math3DVector3(Value.X * Scale, Value.Y * Scale, Value.Z * Scale);
}

/************************************************************************/

/**
 * @brief Dot product between two vectors.
 * @param A First vector.
 * @param B Second vector.
 * @return Dot product.
 */
F32 Math3DVector3Dot(VECTOR3 A, VECTOR3 B) {
    return (A.X * B.X) + (A.Y * B.Y) + (A.Z * B.Z);
}

/************************************************************************/

/**
 * @brief Cross product between two vectors.
 * @param A First vector.
 * @param B Second vector.
 * @return Cross product.
 */
VECTOR3 Math3DVector3Cross(VECTOR3 A, VECTOR3 B) {
    return Math3DVector3(
        (A.Y * B.Z) - (A.Z * B.Y),
        (A.Z * B.X) - (A.X * B.Z),
        (A.X * B.Y) - (A.Y * B.X));
}

/************************************************************************/

/**
 * @brief Normalize one vector.
 * @param Value Source vector.
 * @return Unit vector or zero vector.
 */
VECTOR3 Math3DVector3Normalize(VECTOR3 Value) {
    F32 LengthSquared;
    F32 Length;

    LengthSquared = Math3DVector3Dot(Value, Value);
    if (LengthSquared <= MATH_EPSILON_F32) {
        return Math3DVector3(0.0f, 0.0f, 0.0f);
    }

    Length = MathSqrtF32(LengthSquared);
    if (Length <= MATH_EPSILON_F32) {
        return Math3DVector3(0.0f, 0.0f, 0.0f);
    }

    return Math3DVector3Scale(Value, 1.0f / Length);
}

/************************************************************************/

/**
 * @brief Return identity quaternion.
 * @return (0,0,0,1).
 */
QUAT4 Math3DQuat4Identity(void) {
    QUAT4 Result;

    Result.X = 0.0f;
    Result.Y = 0.0f;
    Result.Z = 0.0f;
    Result.W = 1.0f;
    return Result;
}

/************************************************************************/

/**
 * @brief Normalize one quaternion.
 * @param Value Source quaternion.
 * @return Unit quaternion or identity.
 */
QUAT4 Math3DQuat4Normalize(QUAT4 Value) {
    F32 LengthSquared;
    F32 Length;
    QUAT4 Result;

    LengthSquared = (Value.X * Value.X) + (Value.Y * Value.Y) + (Value.Z * Value.Z) + (Value.W * Value.W);
    if (LengthSquared <= MATH_EPSILON_F32) {
        return Math3DQuat4Identity();
    }

    Length = MathSqrtF32(LengthSquared);
    if (Length <= MATH_EPSILON_F32) {
        return Math3DQuat4Identity();
    }

    Result.X = Value.X / Length;
    Result.Y = Value.Y / Length;
    Result.Z = Value.Z / Length;
    Result.W = Value.W / Length;
    return Result;
}

/************************************************************************/

/**
 * @brief Build one quaternion from XYZ Euler angles (radians).
 * @param EulerRadians Rotation angles around X, then Y, then Z.
 * @return Unit quaternion.
 */
QUAT4 Math3DQuat4FromEulerXYZRadians(VECTOR3 EulerRadians) {
    F32 HalfX;
    F32 HalfY;
    F32 HalfZ;
    F32 Sx;
    F32 Cx;
    F32 Sy;
    F32 Cy;
    F32 Sz;
    F32 Cz;
    QUAT4 Result;

    HalfX = EulerRadians.X * 0.5f;
    HalfY = EulerRadians.Y * 0.5f;
    HalfZ = EulerRadians.Z * 0.5f;

    Sx = MathSinF32(HalfX);
    Cx = MathCosF32(HalfX);
    Sy = MathSinF32(HalfY);
    Cy = MathCosF32(HalfY);
    Sz = MathSinF32(HalfZ);
    Cz = MathCosF32(HalfZ);

    Result.W = (Cx * Cy * Cz) + (Sx * Sy * Sz);
    Result.X = (Sx * Cy * Cz) - (Cx * Sy * Sz);
    Result.Y = (Cx * Sy * Cz) + (Sx * Cy * Sz);
    Result.Z = (Cx * Cy * Sz) - (Sx * Sy * Cz);

    return Math3DQuat4Normalize(Result);
}

/************************************************************************/

/**
 * @brief Return identity 4x4 matrix.
 * @return Identity matrix.
 */
MATRIX4 Math3DMatrix4Identity(void) {
    MATRIX4 Result;

    Result.M11 = 1.0f;
    Result.M12 = 0.0f;
    Result.M13 = 0.0f;
    Result.M14 = 0.0f;

    Result.M21 = 0.0f;
    Result.M22 = 1.0f;
    Result.M23 = 0.0f;
    Result.M24 = 0.0f;

    Result.M31 = 0.0f;
    Result.M32 = 0.0f;
    Result.M33 = 1.0f;
    Result.M34 = 0.0f;

    Result.M41 = 0.0f;
    Result.M42 = 0.0f;
    Result.M43 = 0.0f;
    Result.M44 = 1.0f;

    return Result;
}

/************************************************************************/

/**
 * @brief Multiply two 4x4 matrices.
 * @param A Left matrix.
 * @param B Right matrix.
 * @return A * B.
 */
MATRIX4 Math3DMatrix4Multiply(MATRIX4 A, MATRIX4 B) {
    MATRIX4 Result;

    Result.M11 = A.M11 * B.M11 + A.M12 * B.M21 + A.M13 * B.M31 + A.M14 * B.M41;
    Result.M12 = A.M11 * B.M12 + A.M12 * B.M22 + A.M13 * B.M32 + A.M14 * B.M42;
    Result.M13 = A.M11 * B.M13 + A.M12 * B.M23 + A.M13 * B.M33 + A.M14 * B.M43;
    Result.M14 = A.M11 * B.M14 + A.M12 * B.M24 + A.M13 * B.M34 + A.M14 * B.M44;

    Result.M21 = A.M21 * B.M11 + A.M22 * B.M21 + A.M23 * B.M31 + A.M24 * B.M41;
    Result.M22 = A.M21 * B.M12 + A.M22 * B.M22 + A.M23 * B.M32 + A.M24 * B.M42;
    Result.M23 = A.M21 * B.M13 + A.M22 * B.M23 + A.M23 * B.M33 + A.M24 * B.M43;
    Result.M24 = A.M21 * B.M14 + A.M22 * B.M24 + A.M23 * B.M34 + A.M24 * B.M44;

    Result.M31 = A.M31 * B.M11 + A.M32 * B.M21 + A.M33 * B.M31 + A.M34 * B.M41;
    Result.M32 = A.M31 * B.M12 + A.M32 * B.M22 + A.M33 * B.M32 + A.M34 * B.M42;
    Result.M33 = A.M31 * B.M13 + A.M32 * B.M23 + A.M33 * B.M33 + A.M34 * B.M43;
    Result.M34 = A.M31 * B.M14 + A.M32 * B.M24 + A.M33 * B.M34 + A.M34 * B.M44;

    Result.M41 = A.M41 * B.M11 + A.M42 * B.M21 + A.M43 * B.M31 + A.M44 * B.M41;
    Result.M42 = A.M41 * B.M12 + A.M42 * B.M22 + A.M43 * B.M32 + A.M44 * B.M42;
    Result.M43 = A.M41 * B.M13 + A.M42 * B.M23 + A.M43 * B.M33 + A.M44 * B.M43;
    Result.M44 = A.M41 * B.M14 + A.M42 * B.M24 + A.M43 * B.M34 + A.M44 * B.M44;

    return Result;
}

/************************************************************************/

/**
 * @brief Build one rotation matrix from one quaternion.
 * @param Rotation Unit quaternion.
 * @return Rotation matrix.
 */
MATRIX4 Math3DMatrix4FromQuat4(QUAT4 Rotation) {
    F32 X2;
    F32 Y2;
    F32 Z2;
    F32 XY;
    F32 XZ;
    F32 YZ;
    F32 WX;
    F32 WY;
    F32 WZ;
    MATRIX4 Result;

    Rotation = Math3DQuat4Normalize(Rotation);

    X2 = Rotation.X + Rotation.X;
    Y2 = Rotation.Y + Rotation.Y;
    Z2 = Rotation.Z + Rotation.Z;

    XY = Rotation.X * Y2;
    XZ = Rotation.X * Z2;
    YZ = Rotation.Y * Z2;
    WX = Rotation.W * X2;
    WY = Rotation.W * Y2;
    WZ = Rotation.W * Z2;

    Result = Math3DMatrix4Identity();

    Result.M11 = 1.0f - (Rotation.Y * Y2 + Rotation.Z * Z2);
    Result.M12 = XY - WZ;
    Result.M13 = XZ + WY;

    Result.M21 = XY + WZ;
    Result.M22 = 1.0f - (Rotation.X * X2 + Rotation.Z * Z2);
    Result.M23 = YZ - WX;

    Result.M31 = XZ - WY;
    Result.M32 = YZ + WX;
    Result.M33 = 1.0f - (Rotation.X * X2 + Rotation.Y * Y2);

    return Result;
}

/************************************************************************/

/**
 * @brief Compose one transform matrix from translate/euler/scale.
 * @param Translation Translation vector.
 * @param EulerRadians Rotation euler vector in radians.
 * @param Scale Scale vector.
 * @return Composed TRS matrix.
 */
MATRIX4 Math3DMatrix4ComposeTRS(VECTOR3 Translation, VECTOR3 EulerRadians, VECTOR3 Scale) {
    QUAT4 Rotation;
    MATRIX4 Result;

    Rotation = Math3DQuat4FromEulerXYZRadians(EulerRadians);
    Result = Math3DMatrix4FromQuat4(Rotation);

    Result.M11 *= Scale.X;
    Result.M21 *= Scale.X;
    Result.M31 *= Scale.X;

    Result.M12 *= Scale.Y;
    Result.M22 *= Scale.Y;
    Result.M32 *= Scale.Y;

    Result.M13 *= Scale.Z;
    Result.M23 *= Scale.Z;
    Result.M33 *= Scale.Z;

    Result.M14 = Translation.X;
    Result.M24 = Translation.Y;
    Result.M34 = Translation.Z;

    return Result;
}

/************************************************************************/

/**
 * @brief Transform one point with one 4x4 matrix.
 * @param Matrix Source matrix.
 * @param Point Source point.
 * @return Transformed point.
 */
VECTOR3 Math3DMatrix4TransformPoint(MATRIX4 Matrix, VECTOR3 Point) {
    VECTOR3 Result;

    Result.X = Matrix.M11 * Point.X + Matrix.M12 * Point.Y + Matrix.M13 * Point.Z + Matrix.M14;
    Result.Y = Matrix.M21 * Point.X + Matrix.M22 * Point.Y + Matrix.M23 * Point.Z + Matrix.M24;
    Result.Z = Matrix.M31 * Point.X + Matrix.M32 * Point.Y + Matrix.M33 * Point.Z + Matrix.M34;

    return Result;
}

/************************************************************************/
