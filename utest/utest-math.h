/*******************************************************************************
 * utest-math.h
 *
 * IMR unit test application - primitive matrix operations
 *
 * Copyright (c) 2016 Cogent Embedded Inc. ALL RIGHTS RESERVED.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *******************************************************************************/

#ifndef __UTEST_MATH_H
#define __UTEST_MATH_H

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <math.h>
#include <stdlib.h>

/*******************************************************************************
 * Platform-specific bindings
 ******************************************************************************/

typedef float    __MATH_FLOAT;

#define __MATH_ZERO             ((__MATH_FLOAT)0.0)
#define __MATH_ONE              ((__MATH_FLOAT)1.0)
#define __MATH_RADIANS(v)       (__MATH_FLOAT)((v) * M_PI / 180)
#define __MATH_COS(v)           (__MATH_FLOAT)cos(v)
#define __MATH_SIN(v)           (__MATH_FLOAT)sin(v)
#define __MATH_TAN(v)           (__MATH_FLOAT)tan(v)
#define __MATH_FLOAT(v)         ((__MATH_FLOAT)(v))

/*******************************************************************************
 * Types definitions
 ******************************************************************************/

/* ...scalar value */
typedef __MATH_FLOAT    __scalar;

/* ...vectors */
typedef __MATH_FLOAT    __vec2[2];
typedef __MATH_FLOAT    __vec3[3];
typedef __MATH_FLOAT    __vec4[4];

/* ...column-major order matrices */
typedef __MATH_FLOAT    __mat2x2[2 * 2];
typedef __MATH_FLOAT    __mat3x3[3 * 3];
typedef __MATH_FLOAT    __mat4x4[4 * 4];

/*******************************************************************************
 * Matrix element accessor
 ******************************************************************************/

#define __M(N, m, i, j)    (m)[(j) * (N) + i]

/*******************************************************************************
 * Primitive vectors operations
 ******************************************************************************/

static inline void __vecN_zero(const int N, __MATH_FLOAT *v)
{
    memset(v, 0, sizeof(__MATH_FLOAT) * N);
}

static inline void __vec2_zero(__vec2 v)
{
    __vecN_zero(2, v);
}

static inline void __vec3_zero(__vec3 v)
{
    __vecN_zero(3, v);
}

static inline void __vec4_zero(__vec4 v)
{
    __vecN_zero(4, v);
}

/*******************************************************************************
 * Primitive matrix operations
 ******************************************************************************/

/* ...zero matrix initialization */
static inline void __matNxN_zero(const int N, __MATH_FLOAT *m)
{
    memset(m, 0, sizeof(__MATH_FLOAT) * N * N);
}

/* ...identity matrix setting */
static inline void __matNxN_identity(const int N, __MATH_FLOAT *m)
{
    int     i;

    __matNxN_zero(N, m);
    for (i = 0; i < N; i++) __M(N, m, i, i) = __MATH_ONE;
}

static inline void __mat3x3_identity(__mat3x3 m)
{
    __matNxN_identity(3, m);
}

static inline void __mat4x4_identity(__mat4x4 m)
{
    __matNxN_identity(4, m);
}

/*******************************************************************************
 * Scale matrix setting
 ******************************************************************************/

/* ...diagonal matrix setting */
static inline void __matNxN_M_diag(const int N, const int M, __MATH_FLOAT *m, const __MATH_FLOAT s)
{
    int     i;

    __matNxN_zero(N, m);
    for (i = 0; i < M; i++) __M(N, m, i, i) = s;
    for (; i < N; i++) __M(N, m, i, i) = __MATH_ONE;
}

/* ...rotation matrix setting */
static inline void __matNxN_rotate(const int N, __MATH_FLOAT *m, int i, int j, const __scalar deg)
{
    __MATH_FLOAT    rad = __MATH_RADIANS(deg);
    __MATH_FLOAT    c = __MATH_COS(rad);
    __MATH_FLOAT    s = __MATH_SIN(rad);
    
    __matNxN_identity(N, m);
    __M(N, m, i, i) = c;
    __M(N, m, j, j) = c;
    __M(N, m, i, j) = s;
    __M(N, m, j, i) = -s;
}

/*******************************************************************************
 * Matrix in-place transposition
 ******************************************************************************/

static inline void __matNxN_tr(const int N, __MATH_FLOAT *a)
{
    int     i, j;
    
    for (i = 0; i < N - 1; i++)
    {
        for (j = i + 1; j < N; j++)
        {
            __MATH_FLOAT    a_ij = __M(N, a, i, j);
            
            __M(N, a, i, j) = __M(N, a, j, i);
            __M(N, a, j, i) = a_ij;
        }
    }
}

static inline void __mat2x2_tr(__mat2x2 a)
{
    __matNxN_tr(2, a);
}

static inline void __mat3x3_tr(__mat3x3 a)
{
    __matNxN_tr(3, a);
}

static inline void __mat4x4_tr(__mat4x4 a)
{
    __matNxN_tr(4, a);
}

/*******************************************************************************
 * Matrix multiplication: C = A * B
 ******************************************************************************/

static inline void __matNxN_mul(const int N, const __MATH_FLOAT *a, const __MATH_FLOAT *b, __MATH_FLOAT *c)
{
    int             i, j, k;
    __MATH_FLOAT    c_ij;
    
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < N; j++)
        {
            for (c_ij = __MATH_ZERO, k = 0; k < N; k++)
            {
                c_ij += __M(N, a, i, k) * __M(N, b, k, j);
            }
            
            __M(N, c, i, j) = c_ij;
        }
    }
}

/* ...multiplication of major MxM minor of NxN "a" by MxM matrix "b" */
static inline void __matNxN_MxM_mul(const int N, const int M, const __MATH_FLOAT *a, const __MATH_FLOAT *b, __MATH_FLOAT *c)
{
    int             i, j, k;
    __MATH_FLOAT    c_ij;
    
    for (i = 0; i < M; i++)
    {
        for (j = 0; j < M; j++)
        {
            for (c_ij = __MATH_ZERO, k = 0; k < M; k++)
            {
                c_ij += __M(N, a, i, k) * __M(M, b, k, j);
            }
            
            __M(M, c, i, j) = c_ij;
        }
    }
}

static inline void __mat2x2_mul(const __mat2x2 a, const __mat2x2 b, __mat2x2 c)
{
    __matNxN_mul(2, a, b, c);
}

static inline void __mat3x3_mul(const __mat3x3 a, const __mat3x3 b, __mat3x3 c)
{
    __matNxN_mul(3, a, b, c);
}

static inline void __mat4x4_mul(const __mat4x4 a, const __mat4x4 b, __mat4x4 c)
{
    __matNxN_mul(4, a, b, c);
}

static inline void __mat4x4_3x3_mul(const __mat4x4 a, const __mat3x3 b, __mat3x3 c)
{
    __matNxN_MxM_mul(4, 3, a, b, c);
}

/*******************************************************************************
 * Matrix right multiplication: B = M * A
 ******************************************************************************/

static inline void __matNxN_mulv(const int N, const __MATH_FLOAT *m, const __MATH_FLOAT *a, __MATH_FLOAT *b)
{
    int             i, j;
    __MATH_FLOAT    b_i;
    
    for (i = 0; i < N; i++)
    {
        for (b_i = __MATH_ZERO, j = 0; j < N; j++)
        {
            b_i += __M(N, m, i, j) * a[j];
        }

        b[i] = b_i;
    }
}

static inline void __mat2x2_mulv(const __mat2x2 a, const __vec2 b, __vec2 c)
{
    __matNxN_mulv(2, a, b, c);
}

static inline void __mat3x3_mulv(const __mat3x3 a, const __vec3 b, __vec3 c)
{
    __matNxN_mulv(3, a, b, c);
}

static inline void __mat4x4_mulv(const __mat4x4 a, const __vec4 b, __vec4 c)
{
    __matNxN_mulv(4, a, b, c);
}

/*******************************************************************************
 * Matrix multiplication by scalar: U = M * b
 ******************************************************************************/

static inline void __matNxN_muls(const int N, const __MATH_FLOAT *m, const __MATH_FLOAT b, __MATH_FLOAT *u)
{
    int             i, j;
    
    for (i = 0; i < N; i++)
    {
        for (j = 0; j < N; j++)
        {
            __M(N, u, i, j) = __M(N, m, i, j) * b;
        }
    }
}

static inline void __mat2x2_muls(const __mat2x2 a, const __scalar b, __mat2x2 c)
{
    __matNxN_muls(2, a, b, c);
}

static inline void __mat3x3_muls(const __mat3x3 a, const __scalar b, __mat3x3 c)
{
    __matNxN_muls(3, a, b, c);
}

static inline void __mat4x4_muls(const __mat4x4 a, const __scalar b, __mat4x4 c)
{
    __matNxN_muls(4, a, b, c);
}

/*******************************************************************************
 * Matrix inversion: B = inv(A) * det, det = det(A)
 ******************************************************************************/

/* ...don't need anything except for this yet */
static inline void __matNxN_min3x3_inv(const int N, const __MATH_FLOAT *a, __mat3x3 b, __scalar *det)
{
#define A   __M(N, a, 0, 0)
#define B   __M(N, a, 0, 1)
#define C   __M(N, a, 0, 2)
#define D   __M(N, a, 1, 0)
#define E   __M(N, a, 1, 1)
#define F   __M(N, a, 1, 2)
#define G   __M(N, a, 2, 0)
#define H   __M(N, a, 2, 1)
#define I   __M(N, a, 2, 2)

#define M00 __M(3, b, 0, 0)
#define M01 __M(3, b, 0, 1)
#define M02 __M(3, b, 0, 2)
#define M10 __M(3, b, 1, 0)
#define M11 __M(3, b, 1, 1)
#define M12 __M(3, b, 1, 2)
#define M20 __M(3, b, 2, 0)
#define M21 __M(3, b, 2, 1)
#define M22 __M(3, b, 2, 2)

    __MATH_FLOAT    m00 = E * I - F * H;
    __MATH_FLOAT    m01 = F * G - D * I;
    __MATH_FLOAT    m02 = D * H - E * G;
    __MATH_FLOAT    m10 = C * H - B * I;
    __MATH_FLOAT    m11 = A * I - C * G;
    __MATH_FLOAT    m12 = B * G - A * H;
    __MATH_FLOAT    m20 = B * F - C * E;
    __MATH_FLOAT    m21 = C * D - A * F;
    __MATH_FLOAT    m22 = A * E - B * D;
    __MATH_FLOAT    d = A * m00 + B * m01 + C * m02;

    if (__builtin_constant_p(det == NULL))
    {
        M00 = m00 / d, M01 = m10 / d, M02 = m20 / d;
        M10 = m01 / d, M11 = m11 / d, M12 = m21 / d;
        M20 = m02 / d, M21 = m12 / d, M22 = m22 / d;
    }
    else
    {
        (det ? *det = d : 0);
        M00 = m00, M01 = m10, M02 = m20;
        M10 = m01, M11 = m11, M12 = m21;
        M20 = m02, M21 = m12, M22 = m22;
    }

#undef M00
#undef M01
#undef M02
#undef M10
#undef M11
#undef M12
#undef M20
#undef M21
#undef M22

#undef A
#undef B
#undef C
#undef D
#undef E
#undef F
#undef G
#undef H
#undef I
}

/* ...get inverse of major minor of 4x4 matrix */
#if 0
static inline void __mat4x4_min3x3_inv(const __mat4x4 a, __mat3x3 b, __scalar *det)
{
    __matNxN_min3x3_inv(4, a, b, det);
}
#else
#define __mat4x4_min3x3_inv(a, b, det)  __matNxN_min3x3_inv(4, (a), (b), (det))
#endif

/*******************************************************************************
 * Matrix dump
 ******************************************************************************/

static inline void __matNxN_dump(const int N, const __MATH_FLOAT *m, const char *tag)
{
    int     i, j;
    char    buffer[256], *p;
    
    for (i = 0; i < N; i++)
    {
        p = buffer + sprintf(buffer, "%s[%d]: ", tag, i);
        
        for (j = 0; j < N; j++)
            p += sprintf(p, "%f ", *m++);

        TRACE(1, _b("%s"), buffer);
    }
}

static inline void __mat2x2_dump(const __mat2x2 m, const char *tag)
{
    __matNxN_dump(2, m, tag);
}

static inline void __mat3x3_dump(const __mat3x3 m, const char *tag)
{
    __matNxN_dump(3, m, tag);
}

static inline void __mat4x4_dump(const __mat4x4 m, const char *tag)
{
    __matNxN_dump(4, m, tag);
}

/*******************************************************************************
 * Projective transformation
 ******************************************************************************/

static inline void __proj3_mul(const __mat4x4 m, const __vec3 a, __vec3 b, const __scalar s)
{
    __vec4      A, B;
    __scalar    z;
    
    /* ...prepare 4-dimensional vector */
    memcpy(A, a, sizeof(__vec3));
    A[3] = __MATH_ONE;

    /* ...multiply vector by matrix */
    __mat4x4_mulv(m, A, B);

    /* ...apply scaling factor to "z" coordinate */
    z = B[2] / s;

    /* ...store projection coordinates */
    b[0] = B[0] / z, b[1] = B[1] / z, b[2] = z;
}

/*******************************************************************************
 * Basic matrix transformations
 ******************************************************************************/

static inline void __mat4x4_rotation(__mat4x4 m, const __vec3 r, const __scalar a)
{
    __mat4x4    t1, t2;

    /* ...set rotation around X: m = Rx */
    __matNxN_rotate(4, m, 1, 2, r[0]);
    //__mat4x4_dump(m, "m");
    
    /* ...set rotation around Y: t1 = Ry */
    __matNxN_rotate(4, t1, 0, 2, r[1]);
    //__mat4x4_dump(t1, "t1");

    /* ...multiply: t2 = m * t1 = Rx * Ry */
    __mat4x4_mul(m, t1, t2);
    //__mat4x4_dump(t2, "t2");

    /* ...set rotation around Z: m = Rz */
    __matNxN_rotate(4, m, 0, 1, r[2]);
    //__mat4x4_dump(m, "m");

    /* ...multiply: t1 = t2 * m = Rx * Ry * Rz */
    __mat4x4_mul(t2, m, t1);
    //__mat4x4_dump(t1, "t1");

    /* ...set scaling matrix: t2 = diag(a,1) */
    __matNxN_M_diag(4, 3, t2, a);
    //__mat4x4_dump(t2, "t2");

    /* ...multiply: m = t2 * t1 = S * Rx * Ry * Rz */
    __mat4x4_mul(t2, t1, m);
    //__mat4x4_dump(m, "m");
}

/* ...translation matrix */
static inline void __mat4x4_translation(__mat4x4 m, __scalar x0, __scalar y0, __scalar z0)
{
    __mat4x4_identity(m);
    __M(4, m, 0, 3) = x0;
    __M(4, m, 1, 3) = y0;
    __M(4, m, 2, 3) = z0;
}

/*******************************************************************************
 * Perspective projection matrix generation
 ******************************************************************************/

static inline void __mat4x4_perspective(__mat4x4 m, __scalar fov, __scalar ratio, __scalar z0, __scalar z1)
{
    __MATH_FLOAT    t = __MATH_TAN(fov / 2);

    __matNxN_zero(4, m);
    
    __M(4, m, 0, 0) = 1.0 / (ratio * t);
    __M(4, m, 1, 1) = 1.0 / t;
    __M(4, m, 2, 2) = (z1 + z0) / (z0 - z1);
    __M(4, m, 2, 3) = 2.0 * (z0 * z1) / (z0 - z1);
    __M(4, m, 3, 2) = -1.0;
}

#endif  /* __UTEST_MATH_H */

