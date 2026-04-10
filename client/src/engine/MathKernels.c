/*------------------------------------------------------------------------------
    MathKernels.c - Optimized Math Operations (Stub)

    Stub implementation. Phase 2 will implement optimized math with
    FPU detection and dual code paths.

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

#include "MathKernels.h"
#include <math.h>

#pragma segment Engine

/* Forward declaration for AltiVec helper (defined at end of file) */
#if __VEC__
#include <altivec.h>
static float vec_horizontal_sum(vector float vec);
#endif

/* Disable fused multiply-add (fmadd) on PPC to match x86 float32 behavior.
   PPC fmadd computes a*b+c with one rounding; x86 SSE does separate multiply
   then add with two roundings. The difference accumulates over 512-dim dot
   products and can flip close argmax decisions in the LM head. */
#ifdef __POWERPC__
#pragma fp_contract off
#endif

/*------------------------------------------------------------------------------
    AltiVec runtime dispatch flag
    Set via MathKernels_SetAltiVec() after Gestalt detection.
    When true, MatVecMul_Int8 and MatVecMul_Float auto-dispatch to SIMD paths.
------------------------------------------------------------------------------*/
static Boolean sUseAltiVec = false;
void MathKernels_SetAltiVec(Boolean enable);

void MathKernels_SetAltiVec(Boolean enable)
{
    extern void DebugLog_Write(const char *);
    sUseAltiVec = enable;
    if (enable)
    {
        DebugLog_Write("MathKernels: AltiVec ON (Int8/Float matmul)");

        /* CRITICAL: Clear the VSCR Non-Java (NJ) bit on real G4 hardware.
           When NJ=1 (the default on Mac OS 9), denormalized floats are
           flushed to zero. Neural network intermediate values include
           denormals during normalization and attention. Flushing them
           causes zero propagation -> NaN -> all-zero logits.
           SheepShaver emulator does not implement the NJ bit, which is
           why models work on emulator but fail on real hardware. */
#if __VEC__
        {
            vector unsigned int vzero;
            vzero = vec_splat_u32(0);
            vec_mtvscr(vzero);  /* Clear all VSCR bits: NJ=0, SAT=0 */
        }
        DebugLog_Write("MathKernels: VSCR NJ bit cleared (denormals enabled)");
#endif
    }
    else
        DebugLog_Write("MathKernels: AltiVec OFF (scalar path)");
}

/*------------------------------------------------------------------------------
    MatVecMul_Float - Float matrix-vector multiply
------------------------------------------------------------------------------*/
void MatVecMul_Float(const float *mat, const float *vec,
                     float *out, long rows, long cols)
{
    long i, j;
    long rowOffset;
    const float *row;

#if __VEC__
    /* AltiVec: full-speed SIMD matmul with vec_madd.
       The matmul itself is NOT the divergence source, the divergence
       comes from exp/cos/sin in softmax/RoPE (MSL vs glibc).
       Arena is 16-byte aligned for correct vec_ld behavior. */
    if (sUseAltiVec)
    {
        MatVecMul_Float_AltiVec(mat, vec, out, rows, cols);
        return;
    }
#endif

    for (i = 0; i < rows; i++)
    {
        /* volatile forces float32 storage after each op,
           preventing double-precision accumulation in FPU regs */
        volatile float sum;
        volatile float prod;

        sum = 0.0f;
        rowOffset = i * cols;
        row = mat + rowOffset;

        for (j = 0; j < cols; j++)
        {
            prod = row[j] * vec[j];
            sum += prod;
        }
        out[i] = sum;
    }
}

/*------------------------------------------------------------------------------
    MatVecMul_Int8 - Quantized int8 matrix-vector multiply
    4-wide unrolled inner loop reduces branch/increment overhead by ~75%
------------------------------------------------------------------------------*/
void MatVecMul_Int8(const signed char *mat, const float *vec,
                    float *out, long rows, long cols,
                    const float *scales, long blocksPerRow)
{
    long i, b, j;

#if __VEC__
    if (sUseAltiVec)
    {
        if (((unsigned long)mat & 15) != 0)
        {
            /* Log misaligned access (should not happen if pager aligns correctly) */
            static short sMisalignCount = 0;
            if (sMisalignCount < 3)
            {
                extern void DebugLog_WriteNum(const char *, long);
                DebugLog_WriteNum("Int8: MISALIGNED mat, addr & 15 = ", (long)((unsigned long)mat & 15));
                sMisalignCount++;
            }
            /* Fall through to scalar */
        }
        else
        {
            MatVecMul_Int8_AltiVec(mat, vec, out, rows, cols, scales, blocksPerRow);
            return;
        }
    }
#endif

    for (i = 0; i < rows; i++)
    {
        float rowSum;
        const signed char *row;
        long scaleBase;

        rowSum = 0.0f;
        row = mat + i * blocksPerRow * 32L;
        scaleBase = i * blocksPerRow;

        for (b = 0; b < blocksPerRow; b++)
        {
            float blockSum;
            long blockStart;
            long blockEnd;

            blockSum = 0.0f;
            blockStart = b * 32L;
            blockEnd = blockStart + 32L;
            if (blockEnd > cols) blockEnd = cols;

            for (j = blockStart; j < blockEnd; j++)
            {
                blockSum += (float)row[j] * vec[j];
            }
            rowSum += blockSum * scales[scaleBase + b];
        }
        out[i] = rowSum;
    }
}

/*------------------------------------------------------------------------------
    MatVecMul_Float_Bias - Float matrix-vector multiply with bias
------------------------------------------------------------------------------*/
void MatVecMul_Float_Bias(const float *mat, const float *vec,
                          const float *bias, float *out,
                          long rows, long cols)
{
    long i, j;
    const float *row;

    for (i = 0; i < rows; i++)
    {
        volatile float sum;
        volatile float prod;

        sum = 0.0f;
        row = mat + i * cols;

        for (j = 0; j < cols; j++)
        {
            prod = row[j] * vec[j];
            sum += prod;
        }
        out[i] = sum + bias[i];
    }
}

/*------------------------------------------------------------------------------
    MatVecMul_Int8_Bias - Quantized matrix-vector multiply with bias
    4-wide unrolled inner loop
------------------------------------------------------------------------------*/
void MatVecMul_Int8_Bias(const signed char *mat, const float *vec,
                         const float *bias, float *out,
                         long rows, long cols,
                         const float *scales, long blocksPerRow)
{
    long i, j;
    long cols4;

    cols4 = cols & ~3L;

    for (i = 0; i < rows; i++)
    {
        float sum0 = 0.0f;
        float sum1 = 0.0f;
        float sum2 = 0.0f;
        float sum3 = 0.0f;
        const signed char *row;
        row = mat + i * cols;

        for (j = 0; j < cols4; j += 4)
        {
            sum0 += (float)row[j]     * vec[j];
            sum1 += (float)row[j + 1] * vec[j + 1];
            sum2 += (float)row[j + 2] * vec[j + 2];
            sum3 += (float)row[j + 3] * vec[j + 3];
        }
        for (; j < cols; j++)
        {
            sum0 += (float)row[j] * vec[j];
        }
        /* Per-group accumulation */
        {
            float rowSum;
            long bb;
            long scaleBase;
            const signed char *biasRow;

            rowSum = 0.0f;
            biasRow = mat + i * blocksPerRow * 32L;
            scaleBase = i * blocksPerRow;

            for (bb = 0; bb < blocksPerRow; bb++)
            {
                float blockSum;
                long bs;
                long be;

                blockSum = 0.0f;
                bs = bb * 32L;
                be = bs + 32L;
                if (be > cols) be = cols;

                for (j = bs; j < be; j++)
                    blockSum += (float)biasRow[j] * vec[j];

                rowSum += blockSum * scales[scaleBase + bb];
            }
            out[i] = rowSum + bias[i];
        }
    }
}

/*------------------------------------------------------------------------------
    RMSNorm - with AltiVec for sum-of-squares and scale loops
------------------------------------------------------------------------------*/
void RMSNorm(float *out, const float *x, const float *weight, long dim)
{
    long i;
    volatile float ss;
    volatile float rms;
    volatile float tmp;

    ss = 0.0f;
    for (i = 0; i < dim; i++)
    {
        tmp = x[i] * x[i];
        ss += tmp;
    }
    ss = ss / (float)dim + 1e-5f;
    rms = (float)(1.0f / sqrt((double)ss));

    for (i = 0; i < dim; i++)
    {
        tmp = x[i] * rms;
        out[i] = tmp * weight[i];
    }
}

/*------------------------------------------------------------------------------
    LayerNorm - Standard Layer Normalization (GPT-2, OPT, Pythia, etc.)
------------------------------------------------------------------------------*/
void LayerNorm(float *out, const float *x, const float *weight,
               const float *bias, long dim)
{
    long i;
    volatile float mean;
    volatile float var;
    volatile float tmp;
    volatile float invStd;

    /* Compute mean */
    mean = 0.0f;
    for (i = 0; i < dim; i++)
    {
        mean += x[i];
    }
    mean = mean / (float)dim;

    /* Compute variance */
    var = 0.0f;
    for (i = 0; i < dim; i++)
    {
        tmp = x[i] - mean;
        var += tmp * tmp;
    }
    var = var / (float)dim + 1e-5f;
    invStd = (float)(1.0 / sqrt((double)var));

    /* Normalize, scale, and shift */
    for (i = 0; i < dim; i++)
    {
        tmp = (x[i] - mean) * invStd;
        out[i] = tmp * weight[i] + bias[i];
    }
}

/*------------------------------------------------------------------------------
    SwiGLU
------------------------------------------------------------------------------*/
void SwiGLU(float *out, const float *gate, const float *up, long dim)
{
    long i;

    /* SwiGLU stays scalar, fast exp approximations (Schraudolph) are too
       imprecise for neural network inference. 1-2% error per call accumulates
       over 24 layers × multiple tokens, causing generation to diverge.
       The exp() call is the bottleneck but correctness requires full precision. */
    for (i = 0; i < dim; i++)
    {
        volatile float expVal;
        volatile float silu;
        /* SiLU(x) = x * sigmoid(x) = x / (1 + exp(-x)) */
        expVal = (float)exp((double)(-gate[i]));
        silu = gate[i] / (1.0f + expVal);
        out[i] = silu * up[i];
    }
}

/*------------------------------------------------------------------------------
    GeLU - Gaussian Error Linear Unit (GPT-2, OPT, GPT-J, Pythia, etc.)

    Uses the tanh approximation:
    0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
    Matches GPT-2 / HuggingFace implementation.
------------------------------------------------------------------------------*/
void GeLU(float *out, const float *x, long dim)
{
    long i;
    volatile float val;
    volatile float cube;
    volatile float inner;
    volatile float tanhVal;

    /* sqrt(2/pi) = 0.7978845608... */
    for (i = 0; i < dim; i++)
    {
        val = x[i];
        cube = val * val * val;
        inner = 0.7978845608f * (val + 0.044715f * cube);
        tanhVal = (float)tanh((double)inner);
        out[i] = 0.5f * val * (1.0f + tanhVal);
    }
}

/*------------------------------------------------------------------------------
    Softmax
------------------------------------------------------------------------------*/
void Softmax(float *x, long size)
{
    long i;
    float maxVal;
    volatile float sum;
    volatile float expVal;

    maxVal = x[0];

    /* Find max for numerical stability */
    for (i = 1; i < size; i++)
    {
        if (x[i] > maxVal)
            maxVal = x[i];
    }

    /* Exponentiate and sum */
    sum = 0.0f;
    for (i = 0; i < size; i++)
    {
        expVal = (float)exp((double)(x[i] - maxVal));
        x[i] = expVal;
        sum += expVal;
    }

    /* Normalize */
    for (i = 0; i < size; i++)
    {
        x[i] /= sum;
    }
}

/*------------------------------------------------------------------------------
    ApplyRoPE - Rotary Position Embedding
------------------------------------------------------------------------------*/
void ApplyRoPE(float *q, float *k, long headDim, long pos, float theta)
{
    long i;
    long halfDim;
    volatile float freq;
    volatile float angle;
    volatile float cosA;
    volatile float sinA;
    float q0, q1, k0, k1;

    halfDim = headDim / 2;

    for (i = 0; i < halfDim; i++)
    {
        freq = (float)(1.0 / pow((double)theta, (double)(2 * i) / (double)headDim));
        angle = (float)pos * freq;
        cosA = (float)cos((double)angle);
        sinA = (float)sin((double)angle);

        /* HuggingFace "halved" convention: pairs are (x[i], x[i + halfDim]) */
        q0 = q[i];
        q1 = q[i + halfDim];
        q[i]            = q0 * cosA - q1 * sinA;
        q[i + halfDim]  = q1 * cosA + q0 * sinA;

        /* K may be NULL for GQA extra Q heads (K already rotated by paired head) */
        if (k != 0L)
        {
            k0 = k[i];
            k1 = k[i + halfDim];
            k[i]            = k0 * cosA - k1 * sinA;
            k[i + halfDim]  = k1 * cosA + k0 * sinA;
        }
    }
}

/*------------------------------------------------------------------------------
    ApplyLearnedPosEmbed - Add learned positional embedding at position
------------------------------------------------------------------------------*/
void ApplyLearnedPosEmbed(float *out, const float *posTable,
                          long pos, long dim)
{
    long i;
    const float *row;

    row = posTable + pos * dim;
    for (i = 0; i < dim; i++)
    {
        out[i] += row[i];
    }
}

/*------------------------------------------------------------------------------
    VecAdd - with AltiVec dispatch for dim divisible by 4
------------------------------------------------------------------------------*/
void VecAdd(float *out, const float *add, long dim)
{
    long i;

    for (i = 0; i < dim; i++)
        out[i] += add[i];
}

/*------------------------------------------------------------------------------
    VecDot - AltiVec-accelerated dot product (used for attention Q*K scores)
------------------------------------------------------------------------------*/
float VecDot(const float *a, const float *b, long dim)
{
    long i;

    {
        long dim4;
        float sum0 = 0.0f;
        float sum1 = 0.0f;
        float sum2 = 0.0f;
        float sum3 = 0.0f;

        dim4 = dim & ~3L;
        for (i = 0; i < dim4; i += 4)
        {
            sum0 += a[i]     * b[i];
            sum1 += a[i + 1] * b[i + 1];
            sum2 += a[i + 2] * b[i + 2];
            sum3 += a[i + 3] * b[i + 3];
        }
        for (; i < dim; i++)
        {
            sum0 += a[i] * b[i];
        }
        return sum0 + sum1 + sum2 + sum3;
    }
}

/*------------------------------------------------------------------------------
    VecScale
------------------------------------------------------------------------------*/
void VecScale(float *out, float scale, long dim)
{
    long i;
    for (i = 0; i < dim; i++)
        out[i] *= scale;
}

/*==============================================================================
    AltiVec (Velocity Engine) SIMD Implementations

    These provide ~2.5-3x speedup for matrix-vector multiplies on PowerPC G4+.
    All model hidden dimensions are divisible by 16, so no remainder handling
    is needed (640, 896, 960, 2048 all divide evenly by 16).

    AltiVec processes:
    - 16 int8 elements per iteration (MatVecMul_Int8_AltiVec)
    -  4 float elements per iteration (MatVecMul_Float_AltiVec)

    All data must be 16-byte aligned (guaranteed by Arena_Alloc).
==============================================================================*/

#if __VEC__

/* CodeWarrior: ensure AltiVec code generation is enabled */
#pragma altivec_model on

/*------------------------------------------------------------------------------
    vec_horizontal_sum - Sum 4 floats in a vector register to a scalar

    AltiVec has no horizontal add instruction. We shift and add twice:
      [a b c d] + [c d a b] = [a+c b+d c+a d+b]
      then [a+c b+d ...] + [b+d a+c ...] = [a+b+c+d ...]
    Extract element 0 for the scalar result.
------------------------------------------------------------------------------*/
static float vec_horizontal_sum(vector float vec)
{
    vector float t1;
    vector float t2;
    /* Use union for safe element extraction (avoids vec_ste alignment issues) */
    union {
        vector float vf;
        float f[4];
    } u;

    /* Rotate by 2 elements and add: [a+c, b+d, c+a, d+b] */
    t1 = vec_add(vec, vec_sld(vec, vec, 8));
    /* Rotate by 1 element and add: [a+b+c+d, ...] */
    t2 = vec_add(t1, vec_sld(t1, t1, 4));

    u.vf = t2;
    return u.f[0];
}

/*------------------------------------------------------------------------------
    MatVecMul_Int8_AltiVec - Per-group Q8 AltiVec matmul

    Per-group quantization: each block of 32 int8 values shares one float
    scale factor. The dequantized dot product for one row is:
        sum over blocks b: (sum of int8[j] * activation[j] for j in block) * scale[b]

    Inner loop processes 32 elements per block in 2 iterations of 16:
    1. Load 16 int8, unpack to 4x float, FMA with activations
    2. After 32 elements, multiply block accumulator by per-group scale
    3. Add scaled block to row accumulator

    One vec_horizontal_sum per row (not per block) for efficiency.
    Prefetch hints for weight data streaming from main memory.
------------------------------------------------------------------------------*/
void MatVecMul_Int8_AltiVec(const signed char *mat, const float *vec,
                             float *out, long rows, long cols,
                             const float *scales, long blocksPerRow)
{
    long i, b, j;
    vector float vZero;

    /* Use vec_splat + cast for reliable zero init (CodeWarrior safe) */
    {
        vector unsigned int vzi = vec_splat_u32(0);
        vZero = (vector float)vzi;
    }

    for (i = 0; i < rows; i++)
    {
        const signed char *row;
        float rowSum;
        long scaleBase;
        long blockStart;

        /* Use blocksPerRow * 32 for correct row stride (matches scalar path) */
        row = mat + i * blocksPerRow * 32L;
        scaleBase = i * blocksPerRow;
        rowSum = 0.0f;

        for (b = 0; b < blocksPerRow; b++)
        {
            vector float blockAcc;

            blockAcc = vZero;
            blockStart = b * 32L;

            /* Process 32 int8 elements in 2 iterations of 16 */
            for (j = blockStart; j < blockStart + 32; j += 16)
            {
                vector signed char vBytes;
                vector signed short vShortHi, vShortLo;
                vector signed int vIntA, vIntB, vIntC, vIntD;
                vector float vFloatA, vFloatB, vFloatC, vFloatD;
                vector float vActA, vActB, vActC, vActD;

                /* Prefetch next block of weights */
                vec_dstt(row + j + 32, 0x00040101, 1);

                /* Load 16 int8 weights */
                vBytes = vec_ld(0, row + j);

                /* Unpack int8 -> int16 -> int32 -> float */
                vShortHi = vec_unpackh(vBytes);
                vShortLo = vec_unpackl(vBytes);
                vIntA = vec_unpackh(vShortHi);
                vIntB = vec_unpackl(vShortHi);
                vIntC = vec_unpackh(vShortLo);
                vIntD = vec_unpackl(vShortLo);
                vFloatA = vec_ctf(vIntA, 0);
                vFloatB = vec_ctf(vIntB, 0);
                vFloatC = vec_ctf(vIntC, 0);
                vFloatD = vec_ctf(vIntD, 0);

                /* Load 16 float activations */
                vActA = vec_ld(0, vec + j);
                vActB = vec_ld(0, vec + j + 4);
                vActC = vec_ld(0, vec + j + 8);
                vActD = vec_ld(0, vec + j + 12);

                /* Accumulate: int8 * activation (raw, no scale yet) */
                blockAcc = vec_madd(vFloatA, vActA, blockAcc);
                blockAcc = vec_madd(vFloatB, vActB, blockAcc);
                blockAcc = vec_madd(vFloatC, vActC, blockAcc);
                blockAcc = vec_madd(vFloatD, vActD, blockAcc);
            }

            /* Scalar scale application: avoid vec_madd for scale multiply.
               Real G4 vec_madd produces wrong results for certain inputs.
               Scalar horizontal_sum + multiply is safe and the overhead
               is negligible (~0.1ms per matmul for 20 blocks × 640 rows). */
            rowSum += vec_horizontal_sum(blockAcc) * scales[scaleBase + b];
        }

        out[i] = rowSum;

        /* NaN guard: fall back to scalar for affected rows */
        if (out[i] != out[i])
        {
            float scalarSum;
            long bb;
            scalarSum = 0.0f;
            for (bb = 0; bb < blocksPerRow; bb++)
            {
                float blockSum;
                long bs, be;
                blockSum = 0.0f;
                bs = bb * 32L;
                be = bs + 32L;
                if (be > cols) be = cols;
                for (j = bs; j < be; j++)
                    blockSum += (float)row[j] * vec[j];
                scalarSum += blockSum * scales[scaleBase + bb];
            }
            out[i] = scalarSum;
        }
    }
}

/*------------------------------------------------------------------------------
    MatVecMul_Float_AltiVec - AltiVec float matrix-vector multiply

    4-wide unrolled: processes 16 floats per inner iteration using
    4 independent accumulators. This eliminates the dependency chain
    where each vec_madd waits for the previous result, allowing the
    G4 vector pipeline to execute multiple FMAs in parallel.

    Also uses dcbt (data cache block touch) prefetch hints to pre-load
    the next cache line of weights while processing the current one.
    The 361MB model far exceeds L2 cache, so every weight row is a
    cache miss without prefetch.

    All hidden dims in our models (640, 896, 1024, 2048) are divisible
    by 16, so no remainder handling is needed.
------------------------------------------------------------------------------*/
void MatVecMul_Float_AltiVec(const float *mat, const float *vec,
                              float *out, long rows, long cols)
{
    long i, j;
    vector float vZero;
    vector float vMat1, vMat2, vMat3, vMat4;
    vector float vVec1, vVec2, vVec3, vVec4;
    vector float acc1, acc2, acc3, acc4;
    const float *row;
    long rowOffset;

    /* Use vec_splat + cast for reliable zero init (CodeWarrior safe) */
    {
        vector unsigned int vzi = vec_splat_u32(0);
        vZero = (vector float)vzi;
    }

    for (i = 0; i < rows; i++)
    {
        rowOffset = i * cols;
        row = mat + rowOffset;

        /* 4 independent accumulators: no dependency chain between them,
           so the G4 can pipeline all 4 vec_madd operations */
        acc1 = vZero;
        acc2 = vZero;
        acc3 = vZero;
        acc4 = vZero;

        for (j = 0; j < cols; j += 16)
        {
            /* Prefetch next cache line of weights (128 bytes ahead).
               dcbt has no effect if data is already cached. */
            vec_dstt(row + j + 32, 0x00040101, 0);

            /* Load 4x4 = 16 floats from weight row */
            vMat1 = vec_ld(0, row + j);
            vMat2 = vec_ld(0, row + j + 4);
            vMat3 = vec_ld(0, row + j + 8);
            vMat4 = vec_ld(0, row + j + 12);

            /* Load 4x4 = 16 floats from activation vector */
            vVec1 = vec_ld(0, vec + j);
            vVec2 = vec_ld(0, vec + j + 4);
            vVec3 = vec_ld(0, vec + j + 8);
            vVec4 = vec_ld(0, vec + j + 12);

            /* 4 independent FMAs, no dependency chain */
            acc1 = vec_madd(vMat1, vVec1, acc1);
            acc2 = vec_madd(vMat2, vVec2, acc2);
            acc3 = vec_madd(vMat3, vVec3, acc3);
            acc4 = vec_madd(vMat4, vVec4, acc4);
        }

        /* Combine 4 accumulators and horizontal sum */
        acc1 = vec_add(acc1, acc2);
        acc3 = vec_add(acc3, acc4);
        acc1 = vec_add(acc1, acc3);
        out[i] = vec_horizontal_sum(acc1);

        /* NaN guard: real G4 vec_madd produces NaN for certain
           weight/activation combinations (e.g., Q row 257 on 7450).
           SheepShaver emulator does not reproduce this.
           Recompute affected rows with scalar, costs ~0.15% speed
           since typically only 1 out of 640 rows is affected. */
        if (out[i] != out[i])
        {
            volatile float sum;
            volatile float prod;
            sum = 0.0f;
            for (j = 0; j < cols; j++)
            {
                prod = row[j] * vec[j];
                sum += prod;
            }
            out[i] = sum;
        }
    }
}

#endif /* __VEC__ */
