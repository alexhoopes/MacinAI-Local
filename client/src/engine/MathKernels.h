/*------------------------------------------------------------------------------
    MathKernels.h - Optimized Math Operations for Inference

    Provides matrix-vector multiplication, softmax, RMSNorm, SwiGLU,
    and RoPE operations. Two code paths:
    - FPU path: Uses 68881/68882 hardware floating point
    - SANE path: Software emulation for Macs without FPU

    All operations work on arena-allocated memory (no Handle locking).

    For CodeWarrior Pro 5 on System 7.5.3+

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
------------------------------------------------------------------------------*/

#ifndef MATHKERNELS_H
#define MATHKERNELS_H

#include <Types.h>

/*------------------------------------------------------------------------------
    Fixed-point type for int8 quantized inference
    Using 16.16 fixed point for intermediate calculations
------------------------------------------------------------------------------*/
typedef long Fixed32;   /* 16.16 fixed point */

#define FIXED_SHIFT     16
#define FIXED_ONE       (1L << FIXED_SHIFT)
#define FLOAT_TO_FIXED(f)   ((Fixed32)((f) * FIXED_ONE))
#define FIXED_TO_FLOAT(x)   ((float)(x) / (float)FIXED_ONE)
/* FIXED_MUL uses long long (64-bit), PPC only, not available on 68K */
#ifdef __POWERPC__
#define FIXED_MUL(a,b)      ((Fixed32)(((long long)(a) * (long long)(b)) >> FIXED_SHIFT))
#endif

/*------------------------------------------------------------------------------
    Matrix-Vector Operations
------------------------------------------------------------------------------*/

/* Matrix-vector multiply: out = mat * vec */
/* mat: [rows x cols] row-major, vec: [cols], out: [rows] */
void MatVecMul_Float(const float *mat, const float *vec,
                     float *out, long rows, long cols);

/* Quantized matrix-vector multiply (int8 weights, float activations) */
void MatVecMul_Int8(const signed char *mat, const float *vec,
                    float *out, long rows, long cols,
                    const float *scales, long blocksPerRow);

/* Matrix-vector multiply with bias: out = mat * vec + bias */
void MatVecMul_Float_Bias(const float *mat, const float *vec,
                          const float *bias, float *out,
                          long rows, long cols);

/* Quantized matrix-vector multiply with bias */
void MatVecMul_Int8_Bias(const signed char *mat, const float *vec,
                         const float *bias, float *out,
                         long rows, long cols,
                         const float *scales, long blocksPerRow);

/*------------------------------------------------------------------------------
    Normalization
------------------------------------------------------------------------------*/

/* RMSNorm: out = (x / rms(x)) * weight */
void RMSNorm(float *out, const float *x, const float *weight, long dim);

/* LayerNorm: out = ((x - mean) / sqrt(var + eps)) * weight + bias */
/* Used by GPT-2, OPT, Pythia, GPT-J, Falcon, etc. */
void LayerNorm(float *out, const float *x, const float *weight,
               const float *bias, long dim);

/*------------------------------------------------------------------------------
    Activation Functions
------------------------------------------------------------------------------*/

/* SwiGLU: out = silu(gate) * up */
/* gate and up are pre-multiplied W_gate*x and W_up*x */
void SwiGLU(float *out, const float *gate, const float *up, long dim);

/* GeLU: out[i] = x[i] * 0.5 * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3))) */
/* Used by GPT-2, OPT, GPT-J, Pythia, Falcon, etc. */
void GeLU(float *out, const float *x, long dim);

/* Softmax in-place */
void Softmax(float *x, long size);

/*------------------------------------------------------------------------------
    Positional Encoding
------------------------------------------------------------------------------*/

/* Apply RoPE to query and key vectors at given position */
void ApplyRoPE(float *q, float *k, long headDim, long pos, float theta);

/* Apply learned positional embeddings: out[i] += posEmbed[pos * dim + i] */
/* Used by GPT-2, OPT (has offset). Table is [maxSeqLen x hiddenDim] */
void ApplyLearnedPosEmbed(float *out, const float *posTable,
                          long pos, long dim);

/*------------------------------------------------------------------------------
    Utility
------------------------------------------------------------------------------*/

/* Element-wise add: out += add */
void VecAdd(float *out, const float *add, long dim);

/* Dot product */
float VecDot(const float *a, const float *b, long dim);

/* Scale vector: out *= scale */
void VecScale(float *out, float scale, long dim);

/*------------------------------------------------------------------------------
    AltiVec Runtime Dispatch
------------------------------------------------------------------------------*/

/* Enable/disable AltiVec dispatch in MatVecMul functions.
   Call after detecting AltiVec with Gestalt. When enabled, MatVecMul_Int8
   and MatVecMul_Float auto-dispatch to AltiVec SIMD paths. */
void MathKernels_SetAltiVec(Boolean enable);

#if __VEC__
/*------------------------------------------------------------------------------
    AltiVec (Velocity Engine) SIMD Implementations

    These are called automatically via MatVecMul_Int8/Float when AltiVec
    is enabled. They can also be called directly.

    Requirements:
    - PowerPC G4 (7400/7410/7450) or later
    - All pointers must be 16-byte aligned (Arena_Alloc guarantees this)
    - cols must be divisible by 16 for Int8, 4 for Float
------------------------------------------------------------------------------*/

void MatVecMul_Int8_AltiVec(const signed char *mat, const float *vec,
                             float *out, long rows, long cols,
                    const float *scales, long blocksPerRow);
void MatVecMul_Float_AltiVec(const float *mat, const float *vec,
                              float *out, long rows, long cols);
#endif /* __VEC__ */

#endif /* MATHKERNELS_H */
