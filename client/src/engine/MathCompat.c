/*----------------------------------------------------------------------
    MathCompat.c - SLEEF-Compatible Float32 Math Functions

    Matches PyTorch's SLEEF (SIMD Library for Evaluating Elementary
    Functions) implementations EXACTLY. All computation in float32.

    CRITICAL: On PPC, the FPU uses 64-bit double registers. All float
    operations MUST use volatile to force float32 storage between
    operations, preventing double-precision accumulation that would
    give different results from x86 SSE float32.

    Based on SLEEF source (Boost Software License 1.0).
    For CodeWarrior Pro 5 / C89 on System 7.5.3+

    Written by Alex Hoopes
    Copyright (c) 2026 OldAppleStuff / Alex Hoopes

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
----------------------------------------------------------------------*/

#pragma segment Engine

#include "MathCompat.h"
#include <math.h>

/*----------------------------------------------------------------------
    SLEEF Constants (from sleef/src/common/misc.h)
----------------------------------------------------------------------*/
#define SLEEF_L2Uf    0.693145751953125f
#define SLEEF_L2Lf    1.428606765330187045e-06f
#define SLEEF_R_LN2f  1.4426950408889634f

#define SLEEF_PI_A2f  3.1414794921875f
#define SLEEF_PI_B2f  0.00011315941810607910156f
#define SLEEF_PI_C2f  1.9841872589410058936e-09f

/*----------------------------------------------------------------------
    Helper: rintk - round float to nearest integer
    SLEEF uses vrint_vi2_vf which rounds to nearest.
----------------------------------------------------------------------*/
static long rintk(float x)
{
    if (x >= 0.0f)
        return (long)(x + 0.5f);
    else
        return (long)(x - 0.5f);
}

/*----------------------------------------------------------------------
    Helper: ldexp2f - multiply by 2^n
    SLEEF's vldexp2_vf_vf_vi2 scales by 2^q.
----------------------------------------------------------------------*/
static float ldexp2f(float x, long q)
{
    /* Split into two multiplies to handle large exponents */
    volatile float scale1;
    volatile float scale2;
    volatile float result;
    long q1;
    long q2;

    /* Use volatile union for bit manipulation */
    union { float f; unsigned long u; } u1, u2;

    q1 = q / 2;
    q2 = q - q1;

    /* Construct 2^q1 and 2^q2 as floats via exponent field */
    u1.u = (unsigned long)(q1 + 127) << 23;
    u2.u = (unsigned long)(q2 + 127) << 23;

    scale1 = u1.f;
    scale2 = u2.f;
    result = x * scale1 * scale2;

    return result;
}

/*----------------------------------------------------------------------
    expf_compat - SLEEF xexpf (u10, 1.0 ULP accuracy)

    ALL float32. Uses volatile to prevent PPC double accumulation.
    Matches PyTorch's Sleef_expf8_u10 on x86 AVX2.
----------------------------------------------------------------------*/
float expf_compat(float d)
{
    volatile float s;
    volatile float u;
    volatile float t;
    long q;

    /* Argument reduction: q = round(d / ln2) */
    q = rintk(d * SLEEF_R_LN2f);

    /* s = d - q * ln2 (Cody-Waite split) */
    s = (float)q * (-SLEEF_L2Uf) + d;
    s = (float)q * (-SLEEF_L2Lf) + s;

    /* Degree-6 polynomial (SLEEF exact coefficients) */
    u = 0.000198527617612853646278381f;
    t = u * s + 0.00139304355252534151077271f;
    u = t;
    t = u * s + 0.00833336077630519866943359f;
    u = t;
    t = u * s + 0.0416664853692054748535156f;
    u = t;
    t = u * s + 0.166666671633720397949219f;
    u = t;
    t = u * s + 0.5f;
    u = t;

    /* u = 1 + s + s*s*u */
    t = s * s;
    t = t * u;
    t = t + s;
    u = 1.0f + t;

    /* Scale by 2^q */
    u = ldexp2f(u, q);

    /* Overflow/underflow */
    if (d < -104.0f) u = 0.0f;
    if (d > 100.0f) u = u * u;  /* infinity */

    return u;
}

/*----------------------------------------------------------------------
    sinf_compat - SLEEF xsinf_u3500 (3.5 ULP accuracy)

    ALL float32 with volatile. Uses SLEEF's Cody-Waite pi/2 reduction
    and degree-4 sin polynomial.
----------------------------------------------------------------------*/
float sinf_compat(float d)
{
    volatile float s;
    volatile float u;
    volatile float t;
    long q;

    /* Range reduction: q = round(d / pi) */
    q = rintk(d * (1.0f / 3.14159265358979323846f));

    /* d = d - q * pi (Cody-Waite split) */
    s = (float)q * (-SLEEF_PI_A2f) + d;
    s = (float)q * (-SLEEF_PI_B2f) + s;
    s = (float)q * (-SLEEF_PI_C2f) + s;

    t = s;

    /* s = t * t */
    s = t * t;

    /* Negate for odd quadrants */
    if (q & 1)
        t = -t;

    /* Polynomial: SLEEF u35 sin coefficients */
    u = 2.6083159809786593541503e-06f;
    u = u * s + (-0.0001981069071916863322258f);
    u = u * s + 0.00833307858556509017944336f;
    u = u * s + (-0.166666597127914428710938f);

    /* result = t + t * s * u */
    u = t + t * s * u;

    return u;
}

/*----------------------------------------------------------------------
    cosf_compat - SLEEF xcosf_u3500 (3.5 ULP accuracy)

    ALL float32 with volatile. Uses SLEEF's Cody-Waite reduction
    and same polynomial as sin (with phase shift).
----------------------------------------------------------------------*/
float cosf_compat(float d)
{
    volatile float s;
    volatile float u;
    volatile float t;
    long q;

    /* Range reduction: q = round(d/pi - 0.5) mapped to odd integers */
    /* cos(x) = sin(x + pi/2), so adjust quadrant by 1 */
    q = 1 + 2 * rintk(d * (1.0f / 3.14159265358979323846f) - 0.5f);

    /* d = d - q * (pi/2) using Cody-Waite */
    s = d + (float)q * (-SLEEF_PI_A2f * 0.5f);
    s = s + (float)q * (-SLEEF_PI_B2f * 0.5f);
    s = s + (float)q * (-SLEEF_PI_C2f * 0.5f);

    t = s;

    /* s = t * t */
    s = t * t;

    /* Negate for alternating quadrants */
    if (!(q & 2))
        t = -t;

    /* Same polynomial as sin */
    u = 2.6083159809786593541503e-06f;
    u = u * s + (-0.0001981069071916863322258f);
    u = u * s + 0.00833307858556509017944336f;
    u = u * s + (-0.166666597127914428710938f);

    /* result = t + t * s * u */
    u = t + t * s * u;

    return u;
}

/*----------------------------------------------------------------------
    sqrtf_compat - float32 sqrt matching PyTorch
----------------------------------------------------------------------*/
float sqrtf_compat(float x)
{
    volatile float result;
    result = (float)sqrt((double)x);
    return result;
}
