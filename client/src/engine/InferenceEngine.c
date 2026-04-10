/*------------------------------------------------------------------------------
    InferenceEngine.c - Local LLM Inference Engine

    Arena allocator + model loading + forward pass + disk paging
    for MacinAI Local. ~94.5M param LlamaForCausalLM running natively
    on 68K and PPC Macs.

    Supports both float32 and Q8_0 quantized weights.
    Disk pager slides layers on/off disk for memory-constrained machines.

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

#include "InferenceEngine.h"
#include "AppGlobals.h"
#include "MathKernels.h"
#include "SafeString.h"
#include "DebugLog.h"
#include <Memory.h>
#include <Gestalt.h>
#include <Processes.h>
#include <Events.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#pragma segment Engine

/*------------------------------------------------------------------------------
    Forward declarations
------------------------------------------------------------------------------*/
static OSErr Arena_Init(ArenaAllocator *arena, long size);
static Ptr Arena_Alloc(ArenaAllocator *arena, long size);
static void Arena_Reset(ArenaAllocator *arena);
static void Arena_Free(ArenaAllocator *arena);
static OSErr Engine_AllocWeightPointers(void);
static OSErr Engine_AllocWeightPointersQ8(void);
static OSErr Engine_AllocActivationBuffers(void);
static OSErr Engine_ReadWeightsFromFile(short refNum);
static OSErr Engine_ReadWeightsFromFileQ8(short refNum);
static OSErr ReadTensor(short refNum, Ptr dest, long numBytes,
                        long *bytesLoaded);
static OSErr ReadQ8Tensor(short refNum, Q8Tensor *tensor,
                          long numElements, Ptr dataDest,
                          long *bytesLoaded);
static long Engine_ForwardPass(long token, long pos);
static MemoryTier DetectMemoryTier(long physicalRAM);
static long ComputeQ8TensorFileBytes(long numRows, long numCols);
static long ComputeQ8LayerBytes(long hiddenDim, long ffnDim, long kvDim,
                                Boolean hasBias);
static long ComputeQ8LayerBytesGPT2(long hiddenDim, long ffnDim);
static long ComputeQ8EmbedBytes(long vocabSize, long hiddenDim);
static Boolean PagerReadQ8Tensor(short refNum, signed char **bufPtr,
                                   Q8Tensor *tensor, long numElements);
static void DiskPager_SetupLayerPointers(DiskPager *pager,
                                          LayerSlot *slot,
                                          LayerWeights *lw);
Ptr Engine_ArenaAlloc(long size);

/*------------------------------------------------------------------------------
    Module Globals
------------------------------------------------------------------------------*/
static EngineState gEngine;
static Boolean gEngineInitialized = false;
static Handle gArenaHandle = nil;
void (*gEngineTokenCallback)(long, long) = nil;
#ifndef kPagerChunkSize
#define kPagerChunkSize 262144L
#endif

static char gPagerPhase[64] = "";
void (*gEnginePrefillCallback)(long, long) = nil;
static Boolean gProgressEnabled = false;

/* Prefill optimization: skip LM head for non-final prefill tokens */
static Boolean sSkipLMHead = false;

/* Repetition penalty state */
#define kRepetitionPenalty 1.05f
#define kMaxResponseTokens 256
static long *sRepTokens = nil;
static long sRepTokenCount = 0;

extern AppGlobals gApp;

/* Forward declarations for public functions */
void Engine_SetProgressCallback(void);
OSErr Engine_InitializeWithProgress(void);
void Engine_ProgressUpdate(long percent, char *message);

/*------------------------------------------------------------------------------
    Arena_Init - Allocate arena memory block
------------------------------------------------------------------------------*/
static OSErr Arena_Init(ArenaAllocator *arena, long size)
{
    arena->base = nil;
    arena->size = 0;
    arena->used = 0;
    gArenaHandle = nil;

    /* Try TempNewHandle first (outside app partition) */
    {
        OSErr tempErr;
        gArenaHandle = TempNewHandle(size + 16, &tempErr);
        if (gArenaHandle != nil)
        {
            HLockHi(gArenaHandle);
            arena->base = *gArenaHandle;
        }
    }

    /* Fallback: system heap, then app heap */
    if (arena->base == nil)
        arena->base = NewPtrSys(size + 16);
    if (arena->base == nil)
        arena->base = NewPtr(size + 16);
    if (arena->base == nil)
        return memFullErr;

    /* CRITICAL: Align arena base to 16 bytes for AltiVec vec_ld.
       TempNewHandle/NewPtr only guarantee 4-byte alignment.
       vec_ld loads from the 16-byte aligned address CONTAINING the pointer.
       If the base is misaligned, vec_ld reads shifted data, causing
       non-deterministic output that changes between reboots. */
    {
        unsigned long rawAddr;
        unsigned long alignedAddr;
        rawAddr = (unsigned long)arena->base;
        alignedAddr = (rawAddr + 15UL) & ~15UL;
        arena->size = size - (long)(alignedAddr - rawAddr);
        arena->base = (Ptr)alignedAddr;

        #if ENABLE_DEBUG_LOGGING
        {
            extern void DebugLog_WriteNum(const char *label, long value);
            DebugLog_WriteNum("Engine_Init: raw arena ptr =", (long)rawAddr);
            DebugLog_WriteNum("Engine_Init: aligned arena ptr =", (long)alignedAddr);
            DebugLog_WriteNum("Engine_Init: alignment offset =", (long)(alignedAddr - rawAddr));

        /* Arena memory is NOT zeroed here, activation buffers
           are zeroed individually after allocation (see below).
           Weight data is overwritten by file reads. */
        }
        #endif
    }
    return noErr;
}

/*------------------------------------------------------------------------------
    Arena_Alloc - Bump-pointer allocation from arena
------------------------------------------------------------------------------*/
static Ptr Arena_Alloc(ArenaAllocator *arena, long size)
{
    Ptr result;
    long aligned;

    /* Align allocation start to 16 bytes for AltiVec (vec_ld requires it).
       Also satisfies the 4-byte alignment needed by 68K and PPC scalar. */
    aligned = (arena->used + 15L) & ~15L;

    /* Round size up to 4 bytes (minimum alignment for trailing data) */
    size = (size + 3) & ~3;

    if (aligned + size > arena->size)
        return nil;

    result = arena->base + aligned;
    arena->used = aligned + size;

    return result;
}

/*------------------------------------------------------------------------------
    Arena_Reset - Reset arena (keep memory, zero offset)
------------------------------------------------------------------------------*/
static void Arena_Reset(ArenaAllocator *arena)
{
    arena->used = 0;
}

/*------------------------------------------------------------------------------
    Arena_Free - Dispose arena memory
------------------------------------------------------------------------------*/
static void Arena_Free(ArenaAllocator *arena)
{
    if (gArenaHandle != nil)
    {
        DisposeHandle(gArenaHandle);
        gArenaHandle = nil;
        arena->base = nil;
    }
    else if (arena->base != nil)
    {
        DisposePtr(arena->base);
        arena->base = nil;
    }
    arena->size = 0;
    arena->used = 0;
}

/*------------------------------------------------------------------------------
    DetectMemoryTier - Determine paging strategy from available RAM
------------------------------------------------------------------------------*/
static MemoryTier DetectMemoryTier(long physicalRAM)
{
    if (physicalRAM >= kTierHugeRAM)
        return kMemTierHuge;
    if (physicalRAM >= kTierLargeRAM)
        return kMemTierLarge;
    if (physicalRAM >= kTierMediumRAM)
        return kMemTierMedium;
    return kMemTierSmall;
}

/*------------------------------------------------------------------------------
    ComputeQ8LayerBytes - Total bytes per layer in Q8 format

    kvDim = numKVHeads * headDim. For MHA (no GQA), kvDim == hiddenDim.
    For GQA models like SmolLM, kvDim < hiddenDim.
    hasBias: if true, includes 3 float32 bias tensors (q,k,v).
------------------------------------------------------------------------------*/
/*----------------------------------------------------------------------
    ComputeQ8TensorFileBytes - Bytes on disk for one per-group Q8 tensor.

    Per-group Q8 file format:
      [4-byte num_rows] [4-byte blocks_per_row]
      [num_rows * blocks_per_row * 4-byte float scales]
      [num_rows * blocks_per_row * 32 int8 data]

    The old per-tensor format was just (4 + rows*cols), much smaller.
    This caused a 5+ MB/layer mismatch when skipping non-resident layers.
----------------------------------------------------------------------*/
static long ComputeQ8TensorFileBytes(long numRows, long numCols)
{
    long blocksPerRow;
    long totalGroups;
    long headerBytes;
    long scaleBytes;
    long dataBytes;

    blocksPerRow = (numCols + 31L) / 32L;
    totalGroups = numRows * blocksPerRow;
    headerBytes = 8L;           /* num_rows + blocks_per_row */
    scaleBytes = totalGroups * 4L;
    dataBytes = totalGroups * 32L;

    return headerBytes + scaleBytes + dataBytes;
}

static long ComputeQ8LayerBytes(long hiddenDim, long ffnDim, long kvDim,
                                Boolean hasBias)
{
    long projBytes;
    long normBytes;
    long biasBytes;

    /* Q and O projections: H*H */
    projBytes = 2L * ComputeQ8TensorFileBytes(hiddenDim, hiddenDim);

    /* K and V projections: kvDim*H (GQA) */
    projBytes += 2L * ComputeQ8TensorFileBytes(kvDim, hiddenDim);

    /* 3 FFN projections: gate/up are FFN*H, down is H*FFN */
    projBytes += 2L * ComputeQ8TensorFileBytes(ffnDim, hiddenDim);
    projBytes += ComputeQ8TensorFileBytes(hiddenDim, ffnDim);

    /* 2 norm vectors: each is H floats (always f32) */
    normBytes = 2L * hiddenDim * 4L;

    /* Attention bias: q_bias [hiddenDim] + k_bias [kvDim] + v_bias [kvDim] */
    biasBytes = 0;
    if (hasBias)
    {
        biasBytes = (hiddenDim + 2L * kvDim) * 4L;
    }

    return projBytes + normBytes + biasBytes;
}

/*------------------------------------------------------------------------------
    ComputeQ8LayerBytesGPT2 - Total bytes per GPT-2 layer in Q8 format

    GPT-2 differences from LLaMA:
    - No GQA: kvDim == hiddenDim (all projections are H*H)
    - 2 FFN projections (not 3): fc1 [ffnDim*H] + fc2 [H*ffnDim]
    - 4 norm floats: input_norm_w, input_norm_b, post_norm_w, post_norm_b
    - 6 bias terms: q,k,v,o (all H), ffn_up (ffnDim), ffn_down (H)
------------------------------------------------------------------------------*/
static long ComputeQ8LayerBytesGPT2(long hiddenDim, long ffnDim)
{
    long projBytes;
    long normBytes;
    long biasBytes;

    /* 4 attention projections: all H*H (no GQA) */
    projBytes = 4L * ComputeQ8TensorFileBytes(hiddenDim, hiddenDim);

    /* 2 FFN projections: fc1 [ffnDim*H] + fc2 [H*ffnDim] */
    projBytes += ComputeQ8TensorFileBytes(ffnDim, hiddenDim);
    projBytes += ComputeQ8TensorFileBytes(hiddenDim, ffnDim);

    /* 4 norm vectors: input_w, input_b, post_w, post_b (all H floats) */
    normBytes = 4L * hiddenDim * 4L;

    /* 6 bias terms: q[H] + k[H] + v[H] + o[H] + ffn_up[ffnDim] + ffn_down[H] */
    biasBytes = (4L * hiddenDim + ffnDim + hiddenDim) * 4L;

    return projBytes + normBytes + biasBytes;
}

/*------------------------------------------------------------------------------
    ComputeQ8EmbedBytes - Q8 embedding bytes
------------------------------------------------------------------------------*/
static long ComputeQ8EmbedBytes(long vocabSize, long hiddenDim)
{
    return ComputeQ8TensorFileBytes(vocabSize, hiddenDim);
}

/*------------------------------------------------------------------------------
    ReadTensor - Read weight tensor from file into arena

    Reads in 32KB chunks. SystemTask() called every ~256KB.
------------------------------------------------------------------------------*/
static OSErr ReadTensor(short refNum, Ptr dest, long numBytes,
                        long *bytesLoaded)
{
    OSErr err;
    long bytesRead;
    long remaining;
    long chunkSize;
    short chunkCount;

    remaining = numBytes;
    chunkCount = 0;
    while (remaining > 0)
    {
        chunkSize = remaining;
        if (chunkSize > kPagerChunkSize)
            chunkSize = 32768L;
        bytesRead = chunkSize;
        err = FSRead(refNum, &bytesRead, dest);
        if (err != noErr)
            return err;
        dest += bytesRead;
        remaining -= bytesRead;
        *bytesLoaded += bytesRead;
        chunkCount++;
        if ((chunkCount & 7) == 0)
            SystemTask();
    }
    return noErr;
}

/*------------------------------------------------------------------------------
    ReadQ8Tensor - Read a Q8 quantized tensor (scale + int8 data)

    On disk: [4-byte big-endian float scale][numElements bytes int8]
    Reads the scale into tensor->scale, data into dataDest.
    Sets tensor->data = dataDest, tensor->numElements = numElements.
------------------------------------------------------------------------------*/
static OSErr ReadQ8Tensor(short refNum, Q8Tensor *tensor,
                          long numElements, Ptr dataDest,
                          long *bytesLoaded)
{
    OSErr err;
    long readBytes;
    long numRows;
    long blocksPerRow;
    long totalGroups;
    long paddedDataSize;

    /* Read num_rows and blocks_per_row (per-group Q8 format) */
    readBytes = 4L;
    err = FSRead(refNum, &readBytes, &numRows);
    if (err != noErr)
        return err;
    *bytesLoaded += 4L;

    readBytes = 4L;
    err = FSRead(refNum, &readBytes, &blocksPerRow);
    if (err != noErr)
        return err;
    *bytesLoaded += 4L;

    /* Allocate and read per-group scales */
    totalGroups = numRows * blocksPerRow;
    tensor->numRows = numRows;
    tensor->blocksPerRow = blocksPerRow;
    tensor->scales = (float *)Arena_Alloc(&gEngine.arena, totalGroups * 4L);
    if (tensor->scales == nil)
        return memFullErr;
    readBytes = totalGroups * 4L;
    err = FSRead(refNum, &readBytes, tensor->scales);
    if (err != noErr)
        return err;
    *bytesLoaded += totalGroups * 4L;

    /* Read int8 data (padded to block boundaries) */
    tensor->data = (signed char *)dataDest;
    tensor->numElements = numElements;
    paddedDataSize = numRows * blocksPerRow * 32L;  /* 32 = BLOCK_SIZE */
    err = ReadTensor(refNum, dataDest, paddedDataSize, bytesLoaded);

    return err;
}

/*------------------------------------------------------------------------------
    Engine_AllocWeightPointers - Float32 path

    Reserve arena space for all weight tensors (float32 format).
------------------------------------------------------------------------------*/
static OSErr Engine_AllocWeightPointers(void)
{
    long hiddenDim;
    long ffnDim;
    long vocabSize;
    long numLayers;
    long layerIdx;
    long embedSize;
    long qkvSize;
    long ffnGateUpSize;
    long ffnDownSize;
    long normSize;

    hiddenDim = gEngine.config.hiddenDim;
    ffnDim = gEngine.config.ffnDim;
    vocabSize = gEngine.config.vocabSize;
    numLayers = gEngine.config.numLayers;

    {
    long kvDim;
    long kvProjSize;

    kvDim = gEngine.config.numKVHeads * gEngine.config.headDim;

    embedSize = vocabSize * hiddenDim * 4L;
    qkvSize = hiddenDim * hiddenDim * 4L;       /* Q and O */
    kvProjSize = kvDim * hiddenDim * 4L;         /* K and V (GQA) */
    ffnGateUpSize = ffnDim * hiddenDim * 4L;
    ffnDownSize = hiddenDim * ffnDim * 4L;
    normSize = hiddenDim * 4L;

    /* 1. Embedding table */
    gEngine.embedTokens = (float *)Arena_Alloc(&gEngine.arena, embedSize);
    if (gEngine.embedTokens == nil)
        return memFullErr;

    /* 2. Per-layer weights (all layers, no paging in float32 mode) */
    for (layerIdx = 0; layerIdx < numLayers; layerIdx++)
    {
        gEngine.layers[layerIdx].qProj = (float *)Arena_Alloc(
            &gEngine.arena, qkvSize);
        gEngine.layers[layerIdx].kProj = (float *)Arena_Alloc(
            &gEngine.arena, kvProjSize);
        gEngine.layers[layerIdx].vProj = (float *)Arena_Alloc(
            &gEngine.arena, kvProjSize);
        gEngine.layers[layerIdx].oProj = (float *)Arena_Alloc(
            &gEngine.arena, qkvSize);
        gEngine.layers[layerIdx].gateProj = (float *)Arena_Alloc(
            &gEngine.arena, ffnGateUpSize);
        gEngine.layers[layerIdx].upProj = (float *)Arena_Alloc(
            &gEngine.arena, ffnGateUpSize);
        gEngine.layers[layerIdx].downProj = (float *)Arena_Alloc(
            &gEngine.arena, ffnDownSize);
        gEngine.layers[layerIdx].inputNorm = (float *)Arena_Alloc(
            &gEngine.arena, normSize);
        gEngine.layers[layerIdx].postAttnNorm = (float *)Arena_Alloc(
            &gEngine.arena, normSize);

        if (gEngine.layers[layerIdx].postAttnNorm == nil)
            return memFullErr;

        gEngine.layers[layerIdx].resident = true;
    }

    }  /* end kvDim scope */

    /* 3. Final RMSNorm */
    gEngine.finalNorm = (float *)Arena_Alloc(&gEngine.arena, normSize);
    if (gEngine.finalNorm == nil)
        return memFullErr;

    return noErr;
}

/*------------------------------------------------------------------------------
    Engine_AllocWeightPointersQ8 - Q8 path

    Allocates embedding + final norm in arena. Layer weights are handled
    by the disk pager (if paging) or allocated here (if all resident).
------------------------------------------------------------------------------*/
static OSErr Engine_AllocWeightPointersQ8(void)
{
    long hiddenDim;
    long vocabSize;
    long numLayers;
    long layerIdx;
    long normSize;
    long attnSize;
    long gateUpSize;
    long downSize;

    hiddenDim = gEngine.config.hiddenDim;
    vocabSize = gEngine.config.vocabSize;
    numLayers = gEngine.config.numLayers;
    normSize = hiddenDim * 4L;

    /* 1. Q8 embedding: scale is stored in q8Embed.scale */
    gEngine.q8Embed.data = (signed char *)Arena_Alloc(
        &gEngine.arena, vocabSize * hiddenDim);
    if (gEngine.q8Embed.data == nil)
        return memFullErr;
    gEngine.q8Embed.numElements = vocabSize * hiddenDim;

    /* 1b. Positional embeddings (GPT-2 only, always float32) */
    gEngine.posEmbed = nil;
    if (gEngine.config.archType == kArchGPT2)
    {
        long posSize;
        posSize = gEngine.config.maxSeqLen * hiddenDim * 4L;
        gEngine.posEmbed = (float *)Arena_Alloc(&gEngine.arena, posSize);
        if (gEngine.posEmbed == nil)
            return memFullErr;
    }

    /* 2. Per-layer weights - only allocate resident layers */
    if (gEngine.config.archType == kArchGPT2)
    {
        /* GPT-2: no gate_proj, all projections H*H, norm biases, all biases */
        attnSize = hiddenDim * hiddenDim;
        gateUpSize = gEngine.config.ffnDim * hiddenDim;
        downSize = hiddenDim * gEngine.config.ffnDim;

        for (layerIdx = 0; layerIdx < numLayers; layerIdx++)
        {
            if (layerIdx < gEngine.layersInRAM)
            {
                /* Q/K/V/O: all hiddenDim x hiddenDim (no GQA in GPT-2) */
                gEngine.layers[layerIdx].q8Q.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, attnSize);
                gEngine.layers[layerIdx].q8K.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, attnSize);
                gEngine.layers[layerIdx].q8V.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, attnSize);
                gEngine.layers[layerIdx].q8O.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, attnSize);

                /* fc1/up and fc2/down (no gate) */
                gEngine.layers[layerIdx].q8Up.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, gateUpSize);
                gEngine.layers[layerIdx].q8Down.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, downSize);

                /* No gate projection for GPT-2 */
                gEngine.layers[layerIdx].q8Gate.data = nil;
                gEngine.layers[layerIdx].q8Gate.numElements = 0;
                gEngine.layers[layerIdx].q8Gate.scales = nil; gEngine.layers[layerIdx].q8Gate.numRows = 0;

                /* LayerNorm weights AND biases (4 norm vectors) */
                gEngine.layers[layerIdx].inputNorm = (float *)Arena_Alloc(
                    &gEngine.arena, normSize);
                gEngine.layers[layerIdx].inputNormBias = (float *)Arena_Alloc(
                    &gEngine.arena, normSize);
                gEngine.layers[layerIdx].postAttnNorm = (float *)Arena_Alloc(
                    &gEngine.arena, normSize);
                gEngine.layers[layerIdx].postAttnNormBias = (float *)Arena_Alloc(
                    &gEngine.arena, normSize);

                if (gEngine.layers[layerIdx].postAttnNormBias == nil)
                    return memFullErr;

                gEngine.layers[layerIdx].q8Q.numElements = attnSize;
                gEngine.layers[layerIdx].q8K.numElements = attnSize;
                gEngine.layers[layerIdx].q8V.numElements = attnSize;
                gEngine.layers[layerIdx].q8O.numElements = attnSize;
                gEngine.layers[layerIdx].q8Up.numElements = gateUpSize;
                gEngine.layers[layerIdx].q8Down.numElements = downSize;

                /* GPT-2 always has all bias terms */
                gEngine.layers[layerIdx].qBias = (float *)Arena_Alloc(
                    &gEngine.arena, hiddenDim * 4L);
                gEngine.layers[layerIdx].kBias = (float *)Arena_Alloc(
                    &gEngine.arena, hiddenDim * 4L);
                gEngine.layers[layerIdx].vBias = (float *)Arena_Alloc(
                    &gEngine.arena, hiddenDim * 4L);
                gEngine.layers[layerIdx].oBias = (float *)Arena_Alloc(
                    &gEngine.arena, hiddenDim * 4L);
                gEngine.layers[layerIdx].ffnUpBias = (float *)Arena_Alloc(
                    &gEngine.arena, gEngine.config.ffnDim * 4L);
                gEngine.layers[layerIdx].ffnDownBias = (float *)Arena_Alloc(
                    &gEngine.arena, hiddenDim * 4L);

                if (gEngine.layers[layerIdx].ffnDownBias == nil)
                    return memFullErr;

                gEngine.layers[layerIdx].resident = true;
            }
            else
            {
                /* Paged layer: DiskPager handles allocation */
                gEngine.layers[layerIdx].resident = false;
            }
        }
    }
    else
    {
        long kvDim;
        long kvSize;

        kvDim = gEngine.config.numKVHeads * gEngine.config.headDim;
        attnSize = hiddenDim * hiddenDim;   /* Q and O projections */
        kvSize = kvDim * hiddenDim;          /* K and V projections (GQA) */
        gateUpSize = gEngine.config.ffnDim * hiddenDim;
        downSize = hiddenDim * gEngine.config.ffnDim;

        for (layerIdx = 0; layerIdx < numLayers; layerIdx++)
        {
            if (layerIdx < gEngine.layersInRAM)
            {
                /* Resident layer: allocate Q8 data in arena */
                gEngine.layers[layerIdx].q8Q.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, attnSize);
                gEngine.layers[layerIdx].q8K.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, kvSize);
                gEngine.layers[layerIdx].q8V.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, kvSize);
                gEngine.layers[layerIdx].q8O.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, attnSize);
                gEngine.layers[layerIdx].q8Gate.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, gateUpSize);
                gEngine.layers[layerIdx].q8Up.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, gateUpSize);
                gEngine.layers[layerIdx].q8Down.data = (signed char *)Arena_Alloc(
                    &gEngine.arena, downSize);

                gEngine.layers[layerIdx].inputNorm = (float *)Arena_Alloc(
                    &gEngine.arena, normSize);
                gEngine.layers[layerIdx].postAttnNorm = (float *)Arena_Alloc(
                    &gEngine.arena, normSize);

                if (gEngine.layers[layerIdx].postAttnNorm == nil)
                    return memFullErr;

                gEngine.layers[layerIdx].q8Q.numElements = attnSize;
                gEngine.layers[layerIdx].q8K.numElements = kvSize;
                gEngine.layers[layerIdx].q8V.numElements = kvSize;
                gEngine.layers[layerIdx].q8O.numElements = attnSize;
                gEngine.layers[layerIdx].q8Gate.numElements = gateUpSize;
                gEngine.layers[layerIdx].q8Up.numElements = gateUpSize;
                gEngine.layers[layerIdx].q8Down.numElements = downSize;

                /* Allocate attention bias buffers if model has QKV bias */
                if (gEngine.config.flags & kFlagHasAttnBias)
                {
                    gEngine.layers[layerIdx].qBias = (float *)Arena_Alloc(
                        &gEngine.arena, hiddenDim * 4L);
                    gEngine.layers[layerIdx].kBias = (float *)Arena_Alloc(
                        &gEngine.arena, kvDim * 4L);
                    gEngine.layers[layerIdx].vBias = (float *)Arena_Alloc(
                        &gEngine.arena, kvDim * 4L);
                    if (gEngine.layers[layerIdx].vBias == nil)
                        return memFullErr;
                }
                else
                {
                    gEngine.layers[layerIdx].qBias = nil;
                    gEngine.layers[layerIdx].kBias = nil;
                    gEngine.layers[layerIdx].vBias = nil;
                }

                gEngine.layers[layerIdx].resident = true;
            }
        else
        {
            /* Paged layer: DiskPager handles allocation */
            gEngine.layers[layerIdx].resident = false;
        }
    }
    }  /* end LLaMA branch */

    /* 3. Final norm (RMSNorm for LLaMA, LayerNorm for GPT-2) */
    gEngine.finalNorm = (float *)Arena_Alloc(&gEngine.arena, normSize);
    if (gEngine.finalNorm == nil)
        return memFullErr;

    /* 3b. Final norm bias (GPT-2 LayerNorm only) */
    gEngine.finalNormBias = nil;
    if (gEngine.config.archType == kArchGPT2)
    {
        gEngine.finalNormBias = (float *)Arena_Alloc(&gEngine.arena, normSize);
        if (gEngine.finalNormBias == nil)
            return memFullErr;
    }

    /* 4. Separate LM head (when not tied to embeddings) */
    if (gEngine.config.flags & kFlagSeparateLMHead)
    {
        long lmHeadSize;
        lmHeadSize = gEngine.config.vocabSize * hiddenDim;
        gEngine.q8LMHead.data = (signed char *)Arena_Alloc(
            &gEngine.arena, lmHeadSize);
        if (gEngine.q8LMHead.data == nil)
            return memFullErr;
        gEngine.q8LMHead.numElements = lmHeadSize;
        DebugLog_Write("Engine_AllocQ8: separate lm_head allocated");
    }

    return noErr;
}

/*------------------------------------------------------------------------------
    Engine_AllocActivationBuffers - Reserve arena space for activations

    KV cache size is tier-dependent to fit memory budget.
------------------------------------------------------------------------------*/
static OSErr Engine_AllocActivationBuffers(void)
{
    long hiddenDim;
    long ffnDim;
    long vocabSize;
    long numLayers;
    long kvCacheSize;
    long kvSeqLen;

    hiddenDim = gEngine.config.hiddenDim;
    ffnDim = gEngine.config.ffnDim;
    vocabSize = gEngine.config.vocabSize;
    numLayers = gEngine.config.numLayers;

    /* Dynamic KV sizing: maximize layers in RAM first, then use
       remaining arena for KV cache. This is critical for Q8 models
       on smaller machines (128 MB Color Classic), with fixed tiers,
       a large KV cache steals RAM from layers, forcing disk paging.
       Dynamic sizing fits all layers first, then gives KV whatever
       is left. Typical results on 128 MB with Q8:
         - All 18 layers in RAM (no disk paging!)
         - KV=200+ (enough for 28-token prompt + 172-token response) */
    {
        long arenaRemaining;
        long kvEntryBytes;
        long activBytes;
        long kvDimLocal;
        long maxKV;

        /* Estimate activation buffer bytes (allocated below) */
        kvDimLocal = gEngine.config.numKVHeads * gEngine.config.headDim;
        activBytes = hiddenDim * 4L * 5L    /* x, xb, q, k, v */
                   + ffnDim * 4L * 2L       /* ffnGate, ffnUp */
                   + hiddenDim * 4L          /* ffnOut */
                   + vocabSize * 4L          /* logits */
                   + 512L * 4L;             /* att buffer (pre-alloc max) */

        arenaRemaining = gEngine.arena.size - gEngine.arena.used - activBytes;

        /* Note: pager slot is ALREADY allocated by DiskPager_Init
           (runs before this function). arena.used already includes it.
           No need to subtract it again, that would double-count and
           shrink KV cache unnecessarily (KV=64 instead of ~230). */
        if (gEngine.layersOnDisk > 0)
        {
            extern void DebugLog_WriteNum(const char *label, long value);
            DebugLog_WriteNum("Engine: pager slot already in arena.used, remaining = ",
                               arenaRemaining);
        }

        /* Each KV position needs: 2 (key+val) * numLayers * kvDim * 4 bytes */
        kvEntryBytes = 2L * numLayers * kvDimLocal * 4L;

        if (kvEntryBytes > 0)
            maxKV = arenaRemaining / kvEntryBytes;
        else
            maxKV = 64;

        /* Clamp to [64, 512] */
        if (maxKV > 512) maxKV = 512;
        if (maxKV < 64) maxKV = 64;
        kvSeqLen = maxKV;

        {
            extern void DebugLog_WriteNum(const char *label, long value);
            DebugLog_WriteNum("Engine: dynamic kvSeqLen =", kvSeqLen);
        }
    }
    gEngine.kvSeqLen = kvSeqLen;

    /* Working buffers (small, reused each step) */
    gEngine.x = (float *)Arena_Alloc(
        &gEngine.arena, hiddenDim * 4L);
    gEngine.xb = (float *)Arena_Alloc(
        &gEngine.arena, hiddenDim * 4L);
    gEngine.q = (float *)Arena_Alloc(
        &gEngine.arena, hiddenDim * 4L);
    gEngine.k = (float *)Arena_Alloc(
        &gEngine.arena, hiddenDim * 4L);
    gEngine.v = (float *)Arena_Alloc(
        &gEngine.arena, hiddenDim * 4L);
    gEngine.att = (float *)Arena_Alloc(
        &gEngine.arena, kvSeqLen * 4L);
    gEngine.ffnGate = (float *)Arena_Alloc(
        &gEngine.arena, ffnDim * 4L);
    gEngine.ffnUp = (float *)Arena_Alloc(
        &gEngine.arena, ffnDim * 4L);
    gEngine.ffnOut = (float *)Arena_Alloc(
        &gEngine.arena, hiddenDim * 4L);
    gEngine.logits = (float *)Arena_Alloc(
        &gEngine.arena, vocabSize * 4L);

    if (gEngine.logits == nil)
        return memFullErr;

    /* CRITICAL: Zero activation buffers.
       Arena_Alloc does NOT zero memory. On real hardware (not emulator),
       uninitialized arena memory contains garbage from previous processes.
       The O projection writes to ffnOut, but the attention score computation
       reads from att[] which may contain garbage NaN values. */
    memset((Ptr)gEngine.x, 0, hiddenDim * 4L);
    memset((Ptr)gEngine.xb, 0, hiddenDim * 4L);
    memset((Ptr)gEngine.q, 0, hiddenDim * 4L);
    memset((Ptr)gEngine.k, 0, hiddenDim * 4L);
    memset((Ptr)gEngine.v, 0, hiddenDim * 4L);
    memset((Ptr)gEngine.att, 0, kvSeqLen * 4L);
    memset((Ptr)gEngine.ffnGate, 0, ffnDim * 4L);
    memset((Ptr)gEngine.ffnUp, 0, ffnDim * 4L);
    memset((Ptr)gEngine.ffnOut, 0, hiddenDim * 4L);
    memset((Ptr)gEngine.logits, 0, vocabSize * 4L);

    /* KV cache: [numLayers x kvSeqLen x kvDim] for keys and values
       Use kvDim (= numKVHeads * headDim) for GQA efficiency */
    {
        long kvDim;
        kvDim = gEngine.config.numKVHeads * gEngine.config.headDim;
        kvCacheSize = numLayers * kvSeqLen * kvDim * 4L;
    }

    gEngine.keyCache = (float *)Arena_Alloc(&gEngine.arena, kvCacheSize);
    gEngine.valueCache = (float *)Arena_Alloc(&gEngine.arena, kvCacheSize);

    if (gEngine.keyCache == nil || gEngine.valueCache == nil)
        return memFullErr;

    /* Zero KV cache */
    memset((Ptr)gEngine.keyCache, 0, kvCacheSize);
    memset((Ptr)gEngine.valueCache, 0, kvCacheSize);

    return noErr;
}

/*------------------------------------------------------------------------------
    Engine_ReadWeightsFromFile - Read float32 weight tensors

    (Original float32 path, preserved for backward compatibility)
------------------------------------------------------------------------------*/
static OSErr Engine_ReadWeightsFromFile(short refNum)
{
    OSErr err;
    long bytesLoaded;
    long layerIdx;
    short progressPercent;
    char msg[64];

    err = SetFPos(refNum, fsFromStart, gEngine.config.weightsOffset);
    if (err != noErr)
        return err;

    bytesLoaded = 0;

    /* 1. Embedding table */
    if (gProgressEnabled)
        Engine_ProgressUpdate(8, "Loading embeddings...");
    err = ReadTensor(refNum, (Ptr)(gEngine.embedTokens),
                     gEngine.config.vocabSize * gEngine.config.hiddenDim * 4L,
                     &bytesLoaded);
    if (err != noErr) return err;

    /* 2. Per-layer weights */
    for (layerIdx = 0; layerIdx < gEngine.config.numLayers; layerIdx++)
    {
        progressPercent = 10 + (short)((layerIdx * 70L) /
                         gEngine.config.numLayers);
        sprintf(msg, "Loading layer %ld / %ld...",
                layerIdx + 1, gEngine.config.numLayers);
        if (gProgressEnabled)
            Engine_ProgressUpdate(progressPercent, msg);

        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].qProj),
                         gEngine.config.hiddenDim * gEngine.config.hiddenDim * 4L,
                         &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].kProj),
                         gEngine.config.numKVHeads * gEngine.config.headDim * gEngine.config.hiddenDim * 4L,
                         &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].vProj),
                         gEngine.config.numKVHeads * gEngine.config.headDim * gEngine.config.hiddenDim * 4L,
                         &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].oProj),
                         gEngine.config.hiddenDim * gEngine.config.hiddenDim * 4L,
                         &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].gateProj),
                         gEngine.config.ffnDim * gEngine.config.hiddenDim * 4L,
                         &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].upProj),
                         gEngine.config.ffnDim * gEngine.config.hiddenDim * 4L,
                         &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].downProj),
                         gEngine.config.hiddenDim * gEngine.config.ffnDim * 4L,
                         &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].inputNorm),
                         gEngine.config.hiddenDim * 4L, &bytesLoaded);
        if (err != noErr) return err;
        err = ReadTensor(refNum, (Ptr)(gEngine.layers[layerIdx].postAttnNorm),
                         gEngine.config.hiddenDim * 4L, &bytesLoaded);
        if (err != noErr) return err;
    }

    /* 3. Final RMSNorm */
    if (gProgressEnabled)
        Engine_ProgressUpdate(82, "Loading final norm...");
    err = ReadTensor(refNum, (Ptr)(gEngine.finalNorm),
                     gEngine.config.hiddenDim * 4L, &bytesLoaded);
    if (err != noErr) return err;

    if (gProgressEnabled)
        Engine_ProgressUpdate(85, "Weights loaded.");
    return noErr;
}

/*------------------------------------------------------------------------------
    Engine_ReadWeightsFromFileQ8 - Read Q8 quantized weights

    Reads embedding + resident layers + final norm from file.
    Paged layers are loaded on demand by DiskPager.
------------------------------------------------------------------------------*/
static OSErr Engine_ReadWeightsFromFileQ8(short refNum)
{
    OSErr err;
    long bytesLoaded;
    long layerIdx;
    long hiddenDim;
    long ffnDim;
    long kvDim;
    long attnSize;
    long kvSize;
    long gateUpSize;
    long downSize;
    short progressPercent;
    char msg[64];
    Boolean hasBias;
    Boolean isGPT2;

    hiddenDim = gEngine.config.hiddenDim;
    ffnDim = gEngine.config.ffnDim;
    kvDim = gEngine.config.numKVHeads * gEngine.config.headDim;
    attnSize = hiddenDim * hiddenDim;   /* Q and O projections */
    kvSize = kvDim * hiddenDim;          /* K and V projections (GQA) */
    gateUpSize = ffnDim * hiddenDim;
    downSize = hiddenDim * ffnDim;
    hasBias = (gEngine.config.flags & kFlagHasAttnBias) != 0;
    isGPT2 = (gEngine.config.archType == kArchGPT2);

    err = SetFPos(refNum, fsFromStart, gEngine.config.weightsOffset);
    if (err != noErr)
        return err;

    bytesLoaded = 0;

    /* 1. Q8 Embedding */
    if (gProgressEnabled)
        Engine_ProgressUpdate(8, "Loading Q8 embeddings...");
    err = ReadQ8Tensor(refNum, &gEngine.q8Embed,
                       gEngine.config.vocabSize * hiddenDim,
                       (Ptr)gEngine.q8Embed.data, &bytesLoaded);
    if (err != noErr) return err;

    /* 1b. Positional embeddings (GPT-2 only, always float32) */
    if (isGPT2 && gEngine.posEmbed != nil)
    {
        long posBytes;
        posBytes = gEngine.config.maxSeqLen * hiddenDim * 4L;
        if (gProgressEnabled)
            Engine_ProgressUpdate(9, "Loading positional embeddings...");
        err = ReadTensor(refNum, (Ptr)gEngine.posEmbed,
                         posBytes, &bytesLoaded);
        if (err != noErr) return err;
        DebugLog_Write("Engine_ReadQ8: positional embeddings loaded");
    }

    /* 2. Layer weights */
    for (layerIdx = 0; layerIdx < gEngine.config.numLayers; layerIdx++)
    {
        progressPercent = 10 + (short)((layerIdx * 70L) /
                         gEngine.config.numLayers);
        sprintf(msg, "Loading layer %ld / %ld...",
                layerIdx + 1, gEngine.config.numLayers);
        if (gProgressEnabled)
            Engine_ProgressUpdate(progressPercent, msg);

        if (!gEngine.layers[layerIdx].resident)
        {
            /* Skip paged layers - seek past their data */
            long skipBytes;
            if (isGPT2)
                skipBytes = ComputeQ8LayerBytesGPT2(hiddenDim, ffnDim);
            else
                skipBytes = ComputeQ8LayerBytes(hiddenDim, ffnDim, kvDim,
                                                hasBias);
            err = SetFPos(refNum, fsFromMark, skipBytes);
            if (err != noErr) return err;
            bytesLoaded += skipBytes;
            continue;
        }

        if (isGPT2)
        {
            /* === GPT-2 layer read order (matching export_bin.py) === */

            /* a-d. Q/K/V/O projections (all H*H, no GQA) */
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8Q,
                               attnSize, (Ptr)gEngine.layers[layerIdx].q8Q.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8K,
                               attnSize, (Ptr)gEngine.layers[layerIdx].q8K.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8V,
                               attnSize, (Ptr)gEngine.layers[layerIdx].q8V.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8O,
                               attnSize, (Ptr)gEngine.layers[layerIdx].q8O.data,
                               &bytesLoaded);
            if (err != noErr) return err;

            /* e. fc1/up (Q8) */
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8Up,
                               gateUpSize, (Ptr)gEngine.layers[layerIdx].q8Up.data,
                               &bytesLoaded);
            if (err != noErr) return err;

            /* f. fc2/down (Q8) */
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8Down,
                               downSize, (Ptr)gEngine.layers[layerIdx].q8Down.data,
                               &bytesLoaded);
            if (err != noErr) return err;

            /* g. input_norm weight (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].inputNorm,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* h. input_norm bias (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].inputNormBias,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* i. post_attn_norm weight (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].postAttnNorm,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* j. post_attn_norm bias (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].postAttnNormBias,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* k. q_bias, k_bias, v_bias (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].qBias,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].kBias,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].vBias,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* l. o_bias (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].oBias,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* m. ffn_up_bias / fc1_bias (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].ffnUpBias,
                             ffnDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* n. ffn_down_bias / fc2_bias (f32) */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].ffnDownBias,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;
        }
        else
        {
            /* === LLaMA layer read order === */

            /* Read Q8 attention projections */
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8Q,
                               attnSize, (Ptr)gEngine.layers[layerIdx].q8Q.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8K,
                               kvSize, (Ptr)gEngine.layers[layerIdx].q8K.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8V,
                               kvSize, (Ptr)gEngine.layers[layerIdx].q8V.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8O,
                               attnSize, (Ptr)gEngine.layers[layerIdx].q8O.data,
                               &bytesLoaded);
            if (err != noErr) return err;

            /* Read Q8 FFN projections (3-matrix SwiGLU) */
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8Gate,
                               gateUpSize, (Ptr)gEngine.layers[layerIdx].q8Gate.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8Up,
                               gateUpSize, (Ptr)gEngine.layers[layerIdx].q8Up.data,
                               &bytesLoaded);
            if (err != noErr) return err;
            err = ReadQ8Tensor(refNum, &gEngine.layers[layerIdx].q8Down,
                               downSize, (Ptr)gEngine.layers[layerIdx].q8Down.data,
                               &bytesLoaded);
            if (err != noErr) return err;

            /* Read float32 norms */
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].inputNorm,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;
            err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].postAttnNorm,
                             hiddenDim * 4L, &bytesLoaded);
            if (err != noErr) return err;

            /* Read float32 attention bias if present */
            if (hasBias)
            {
                err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].qBias,
                                 hiddenDim * 4L, &bytesLoaded);
                if (err != noErr) return err;
                err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].kBias,
                                 kvDim * 4L, &bytesLoaded);
                if (err != noErr) return err;
                err = ReadTensor(refNum, (Ptr)gEngine.layers[layerIdx].vBias,
                                 kvDim * 4L, &bytesLoaded);
                if (err != noErr) return err;
            }
        }
    }

    /* 3. Final norm weight */
    if (gProgressEnabled)
        Engine_ProgressUpdate(82, "Loading final norm...");
    err = ReadTensor(refNum, (Ptr)(gEngine.finalNorm),
                     hiddenDim * 4L, &bytesLoaded);
    if (err != noErr) return err;

    /* 3b. Final norm bias (GPT-2 LayerNorm only) */
    if (isGPT2 && gEngine.finalNormBias != nil)
    {
        err = ReadTensor(refNum, (Ptr)(gEngine.finalNormBias),
                         hiddenDim * 4L, &bytesLoaded);
        if (err != noErr) return err;
        DebugLog_Write("Engine_ReadQ8: final norm bias loaded");
    }

    /* 4. Separate LM head (when not tied to embeddings) */
    if (gEngine.config.flags & kFlagSeparateLMHead)
    {
        if (gProgressEnabled)
            Engine_ProgressUpdate(84, "Loading lm_head...");
        err = ReadQ8Tensor(refNum, &gEngine.q8LMHead,
                           gEngine.config.vocabSize * hiddenDim,
                           (Ptr)gEngine.q8LMHead.data,
                           &bytesLoaded);
        if (err != noErr) return err;
        DebugLog_Write("Engine_ReadQ8: separate lm_head loaded");
    }

    if (gProgressEnabled)
        Engine_ProgressUpdate(85, "Q8 weights loaded.");
    return noErr;
}

/*==============================================================================
    DISK PAGER IMPLEMENTATION

    Pages transformer layers between disk and arena memory using
    Mac Toolbox FSRead/SetFPos. Layers are read in 8KB chunks.
==============================================================================*/

/*------------------------------------------------------------------------------
    DiskPager_Init - Initialize the disk pager
------------------------------------------------------------------------------*/
DiskPager* DiskPager_Init(ArenaAllocator *arena, short modelRefNum,
                           MemoryTier tier, long weightsOffset,
                           long embedBytes, QuantType quantType,
                           long hiddenDim, long ffnDim,
                           short numLayers)
{
    DiskPager *pager;
    short numSlots;
    short s;
    long slotProjSize;
    long slotNormSize;
    long slotBiasSize;
    long layerSize;
    long currentOffset;
    short i;
    Boolean hasBias;

    if (arena == nil)
        return nil;

    pager = (DiskPager *)Arena_Alloc(arena, sizeof(DiskPager));
    if (pager == nil)
        return nil;
    memset(pager, 0, sizeof(DiskPager));

    hasBias = (gEngine.config.flags & kFlagHasAttnBias) != 0;

    pager->modelRefNum = modelRefNum;
    pager->tier = tier;
    pager->quantType = quantType;
    pager->hiddenDim = hiddenDim;
    pager->ffnDim = ffnDim;
    pager->kvDim = gEngine.config.numKVHeads * gEngine.config.headDim;
    pager->hasBias = hasBias;

    /* Determine number of layer slots */
    switch (tier)
    {
        case kMemTierHuge:   numSlots = kLayerSlotsHuge;   break;
        case kMemTierLarge:  numSlots = kLayerSlotsLarge;  break;
        case kMemTierMedium: numSlots = kLayerSlotsMedium; break;
        default:             numSlots = kLayerSlotsSmall;   break;
    }
    if (numSlots > numLayers)
        numSlots = numLayers;

    /* Cap slots to what fits in remaining arena (each slot = one layer of data).
       Reserve space for activation buffers after pager (~25MB headroom). */
    {
        long slotSize;
        long arenaRemaining;
        long headroom;
        short maxSlots;
        Boolean isGPT2;

        isGPT2 = (gEngine.config.archType == kArchGPT2);
        if (isGPT2)
            slotSize = ComputeQ8LayerBytesGPT2(hiddenDim, ffnDim);
        else
            slotSize = ComputeQ8LayerBytes(hiddenDim, ffnDim,
                                            gEngine.config.numKVHeads * gEngine.config.headDim,
                                            (gEngine.config.flags & kFlagHasAttnBias) != 0);
        arenaRemaining = arena->size - arena->used;
        headroom = 30L * 1024L * 1024L;  /* 30MB for KV cache + activations */
        if (arenaRemaining > headroom)
            maxSlots = (short)((arenaRemaining - headroom) / slotSize);
        else
            maxSlots = 1;
        if (maxSlots < 1) maxSlots = 1;
        if (numSlots > maxSlots) numSlots = maxSlots;
    }

    pager->numSlots = numSlots;
    pager->nextSlot = 0;

    /* Compute per-layer disk size */
    if (gEngine.config.archType == kArchGPT2)
        layerSize = ComputeQ8LayerBytesGPT2(hiddenDim, ffnDim);
    else
        layerSize = ComputeQ8LayerBytes(hiddenDim, ffnDim, pager->kvDim,
                                        hasBias);
    pager->layerSize = layerSize;

    /* Compute file offsets for each layer.
       GPT-2 has positional embeddings between embed and layers. */
    currentOffset = weightsOffset + embedBytes;
    if (gEngine.config.archType == kArchGPT2)
    {
        currentOffset += gEngine.config.maxSeqLen * hiddenDim * 4L;
    }
    for (i = 0; i < numLayers && i < kMaxModelLayers; i++)
    {
        pager->layerOffsets[i] = currentOffset;
        currentOffset += layerSize;
    }

    /* Allocate layer slots from arena */
    if (gEngine.config.archType == kArchGPT2)
    {
        /* GPT-2: 4 attn (H*H) + 2 FFN, per-group Q8
           +256 bytes for AltiVec alignment padding */
        slotProjSize = 4L * ComputeQ8TensorFileBytes(hiddenDim, hiddenDim)
                     + ComputeQ8TensorFileBytes(ffnDim, hiddenDim)
                     + ComputeQ8TensorFileBytes(hiddenDim, ffnDim)
                     + 256L;  /* alignment padding */

        /* 4 norm vectors: input_w, input_b, post_w, post_b */
        slotNormSize = 4L * hiddenDim * 4L;

        /* All bias terms: q,k,v,o [H each] + ffn_up [ffnDim] + ffn_down [H] */
        slotBiasSize = (4L * hiddenDim + ffnDim + hiddenDim) * 4L;
    }
    else
    {
        /* LLaMA: Q/O + K/V (GQA) + gate/up/down FFN
           Per-group Q8: 8-byte header + scales + data per tensor
           +256 bytes for AltiVec 16-byte alignment padding per tensor */
        slotProjSize = 2L * ComputeQ8TensorFileBytes(hiddenDim, hiddenDim)
                     + 2L * ComputeQ8TensorFileBytes(pager->kvDim, hiddenDim)
                     + 2L * ComputeQ8TensorFileBytes(ffnDim, hiddenDim)
                     + ComputeQ8TensorFileBytes(hiddenDim, ffnDim)
                     + 256L;  /* alignment padding: up to 15 bytes per tensor */
        slotNormSize = 2L * hiddenDim * 4L;

        /* Attention bias: q_bias [hiddenDim] + k_bias [kvDim] + v_bias [kvDim] */
        slotBiasSize = 0;
        if (hasBias)
        {
            slotBiasSize = (hiddenDim + 2L * pager->kvDim) * 4L;
        }
    }

    for (s = 0; s < numSlots; s++)
    {
        pager->slots[s].projData = Arena_Alloc(arena, slotProjSize);
        if (pager->slots[s].projData == nil)
            return nil;

        pager->slots[s].normData =
            (float *)Arena_Alloc(arena, slotNormSize);
        if (pager->slots[s].normData == nil)
            return nil;

        if (slotBiasSize > 0)
        {
            pager->slots[s].biasData =
                (float *)Arena_Alloc(arena, slotBiasSize);
            if (pager->slots[s].biasData == nil)
                return nil;
        }
        else
        {
            pager->slots[s].biasData = nil;
        }

        pager->slots[s].loadedLayer = -1;
    }

    return pager;
}

/*----------------------------------------------------------------------
    PagerReadQ8Tensor - Read one Q8 tensor from disk into slot buffer
    with 16-byte aligned int8 data for AltiVec.
----------------------------------------------------------------------*/
static Boolean PagerReadQ8Tensor(short refNum, signed char **bufPtr,
                                   Q8Tensor *tensor, long numElements)
{
    long numRows, blocksPerRow, totalGroups;
    long readBytes;
    long dataSize;
    signed char *ptr;
    OSErr err;

    ptr = *bufPtr;

    /* Read header from file */
    readBytes = 4L;
    err = FSRead(refNum, &readBytes, &numRows);
    if (err != noErr) { DebugLog_Write("PagerQ8: header read failed"); return false; }

    readBytes = 4L;
    err = FSRead(refNum, &readBytes, &blocksPerRow);
    if (err != noErr) { DebugLog_Write("PagerQ8: header2 read failed"); return false; }

    totalGroups = numRows * blocksPerRow;

    {
        extern void DebugLog_WriteNum(const char *, long);
        DebugLog_WriteNum("PagerQ8: numRows=", numRows);
        DebugLog_WriteNum("  blocksPerRow=", blocksPerRow);
        DebugLog_WriteNum("  totalGroups=", totalGroups);
        DebugLog_WriteNum("  bufPtr=", (long)ptr);
        DebugLog_WriteNum("  numElements=", numElements);
    }

    /* Read scales into buffer */
    tensor->numRows = numRows;
    tensor->blocksPerRow = blocksPerRow;
    tensor->scales = (float *)ptr;
    readBytes = totalGroups * 4L;
    err = FSRead(refNum, &readBytes, ptr);
    if (err != noErr) { DebugLog_Write("PagerQ8: scales read failed"); return false; }
    ptr += totalGroups * 4L;

    /* Align to 16 bytes for AltiVec vec_ld */
    {
        signed char *preAlign;
        preAlign = ptr;
        ptr = (signed char *)(((unsigned long)ptr + 15UL) & ~15UL);
        {
            extern void DebugLog_WriteNum(const char *, long);
            DebugLog_WriteNum("  aligned data ptr=", (long)ptr);
            DebugLog_WriteNum("  alignment pad=", (long)(ptr - preAlign));
        }
    }

    /* Read int8 data into aligned position */
    tensor->data = ptr;
    tensor->numElements = numElements;
    dataSize = numRows * blocksPerRow * 32L;
    {
        extern void DebugLog_WriteNum(const char *, long);
        DebugLog_WriteNum("  dataSize=", dataSize);
    }
    readBytes = dataSize;
    err = FSRead(refNum, &readBytes, ptr);
    if (err != noErr) { DebugLog_Write("PagerQ8: data read failed"); return false; }
    ptr += dataSize;

    {
        extern void DebugLog_WriteNum(const char *, long);
        DebugLog_Write("PagerQ8: tensor OK");
    }

    *bufPtr = ptr;
    SystemTask();
    return true;
}

/*------------------------------------------------------------------------------
    DiskPager_ReadLayerFromDisk - Load one layer from disk (Mac Toolbox I/O)
------------------------------------------------------------------------------*/
static Boolean DiskPager_ReadLayerFromDisk(DiskPager *pager, short layerIdx,
                                            LayerSlot *slot)
{
    OSErr err;
    long offset;
    long normDataSize;
    long biasDataSize;
    long bytesRead;
    signed char *ptr;
    long H, FFN, KV;
    Boolean isGPT2;
    Boolean hasBias;

    if (pager == nil || slot == nil)
        return false;

    offset = pager->layerOffsets[layerIdx];
    err = SetFPos(pager->modelRefNum, fsFromStart, offset);
    if (err != noErr)
        return false;

    H = pager->hiddenDim;
    FFN = pager->ffnDim;
    KV = pager->kvDim;
    isGPT2 = (gEngine.config.archType == kArchGPT2);
    hasBias = pager->hasBias;

    ptr = (signed char *)slot->projData;
    {
        extern void DebugLog_WriteNum(const char *, long);
        DebugLog_WriteNum("PagerRead: layer ", (long)layerIdx);
        DebugLog_WriteNum("  projData=", (long)ptr);
    }

    /* Read each Q8 tensor individually with 16-byte aligned int8 data */
    if (isGPT2)
    {
        /* GPT-2: Q, K, V, O (all H*H), Up (FFN*H), Down (H*FFN) */
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8Q, H * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8K, H * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8V, H * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8O, H * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8Up, FFN * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8Down, H * FFN))
            return false;
        gEngine.layers[layerIdx].q8Gate.data = nil;
        gEngine.layers[layerIdx].q8Gate.numElements = 0;
        gEngine.layers[layerIdx].q8Gate.scales = nil;
    }
    else
    {
        /* LLaMA: Q (H*H), K (KV*H), V (KV*H), O (H*H),
                  Gate (FFN*H), Up (FFN*H), Down (H*FFN) */
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8Q, H * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8K, KV * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8V, KV * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8O, H * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8Gate, FFN * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8Up, FFN * H))
            return false;
        if (!PagerReadQ8Tensor(pager->modelRefNum, &ptr,
                               &gEngine.layers[layerIdx].q8Down, H * FFN))
            return false;
    }

    /* Read norm data (always float32) */
    if (isGPT2)
        normDataSize = 4L * H * 4L;
    else
        normDataSize = 2L * H * 4L;

    {
        extern void DebugLog_WriteNum(const char *, long);
        DebugLog_WriteNum("PagerRead: norms size=", normDataSize);
        DebugLog_WriteNum("  normData ptr=", (long)slot->normData);
    }

    bytesRead = normDataSize;
    err = FSRead(pager->modelRefNum, &bytesRead, slot->normData);
    if (err != noErr) { DebugLog_Write("PagerRead: norm read FAILED"); return false; }
    DebugLog_Write("PagerRead: norms OK");

    /* Set norm pointers */
    if (isGPT2)
    {
        gEngine.layers[layerIdx].inputNorm = slot->normData;
        gEngine.layers[layerIdx].inputNormBias = slot->normData + H;
        gEngine.layers[layerIdx].postAttnNorm = slot->normData + 2L * H;
        gEngine.layers[layerIdx].postAttnNormBias = slot->normData + 3L * H;
    }
    else
    {
        gEngine.layers[layerIdx].inputNorm = slot->normData;
        gEngine.layers[layerIdx].postAttnNorm = slot->normData + H;
    }

    DebugLog_Write("PagerRead: norm pointers set");

    /* Read bias data if present */
    if (hasBias && slot->biasData != nil)
    {
        if (isGPT2)
            biasDataSize = (4L * H + FFN + H) * 4L;
        else
            biasDataSize = (H + 2L * KV) * 4L;

        bytesRead = biasDataSize;
        err = FSRead(pager->modelRefNum, &bytesRead, slot->biasData);
        if (err != noErr) return false;

        if (isGPT2)
        {
            gEngine.layers[layerIdx].qBias = slot->biasData;
            gEngine.layers[layerIdx].kBias = slot->biasData + H;
            gEngine.layers[layerIdx].vBias = slot->biasData + 2L * H;
            gEngine.layers[layerIdx].oBias = slot->biasData + 3L * H;
            gEngine.layers[layerIdx].ffnUpBias = slot->biasData + 4L * H;
            gEngine.layers[layerIdx].ffnDownBias = slot->biasData + 4L * H + FFN;
        }
        else
        {
            gEngine.layers[layerIdx].qBias = slot->biasData;
            gEngine.layers[layerIdx].kBias = slot->biasData + H;
            gEngine.layers[layerIdx].vBias = slot->biasData + H + KV;
        }
    }
    else
    {
        gEngine.layers[layerIdx].qBias = nil;
        gEngine.layers[layerIdx].kBias = nil;
        gEngine.layers[layerIdx].vBias = nil;
        gEngine.layers[layerIdx].oBias = nil;
        gEngine.layers[layerIdx].ffnUpBias = nil;
        gEngine.layers[layerIdx].ffnDownBias = nil;
    }

    slot->loadedLayer = layerIdx;
    DebugLog_WriteNum("PagerRead: COMPLETE layer ", (long)layerIdx);
    SystemTask();
    return true;
}

/*------------------------------------------------------------------------------
    DiskPager_SetupLayerPointers - Point LayerWeights into a loaded slot

    Parses the raw slot buffer and sets up Q8Tensor pointers.
------------------------------------------------------------------------------*/
static Boolean PagerReadQ8Tensor(short refNum, signed char **bufPtr,
                                   Q8Tensor *tensor, long numElements);
static void DiskPager_SetupLayerPointers(DiskPager *pager,
                                          LayerSlot *slot,
                                          LayerWeights *lw)
{
    signed char *ptr;
    long H;
    long FFN;
    long KV;
    long attnSize;
    long kvSize;
    long upSize;
    long downSize;

    H = pager->hiddenDim;
    FFN = pager->ffnDim;
    KV = pager->kvDim;
    ptr = (signed char *)slot->projData;
    attnSize = H * H;

    if (gEngine.config.archType == kArchGPT2)
    {
        /* GPT-2: 4 attn projections all H*H (no GQA) */

        /* q_proj: [scale][H*H int8] */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8Q.numRows = _nr; lw->q8Q.blocksPerRow = _bpr; lw->q8Q.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8Q.data = ptr;
        lw->q8Q.numElements = attnSize;
        ptr += attnSize;

        /* k_proj: [scale][H*H int8] */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8K.numRows = _nr; lw->q8K.blocksPerRow = _bpr; lw->q8K.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8K.data = ptr;
        lw->q8K.numElements = attnSize;
        ptr += attnSize;

        /* v_proj: [scale][H*H int8] */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8V.numRows = _nr; lw->q8V.blocksPerRow = _bpr; lw->q8V.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8V.data = ptr;
        lw->q8V.numElements = attnSize;
        ptr += attnSize;

        /* o_proj: [scale][H*H int8] */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8O.numRows = _nr; lw->q8O.blocksPerRow = _bpr; lw->q8O.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8O.data = ptr;
        lw->q8O.numElements = attnSize;
        ptr += attnSize;

        /* fc1/up_proj: [scale][FFN*H int8] */
        upSize = FFN * H;
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8Up.numRows = _nr; lw->q8Up.blocksPerRow = _bpr; lw->q8Up.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8Up.data = ptr;
        lw->q8Up.numElements = upSize;
        ptr += upSize;

        /* fc2/down_proj: [scale][H*FFN int8] */
        downSize = H * FFN;
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8Down.numRows = _nr; lw->q8Down.blocksPerRow = _bpr; lw->q8Down.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8Down.data = ptr;
        lw->q8Down.numElements = downSize;
        ptr += downSize;

        /* No gate for GPT-2 */
        lw->q8Gate.data = nil;
        lw->q8Gate.numElements = 0;
        lw->q8Gate.scales = nil; lw->q8Gate.numRows = 0;

        /* Norms in normData: input_w, input_b, post_w, post_b */
        lw->inputNorm = slot->normData;
        lw->inputNormBias = slot->normData + H;
        lw->postAttnNorm = slot->normData + 2L * H;
        lw->postAttnNormBias = slot->normData + 3L * H;

        /* All bias terms in biasData: q,k,v,o [H each], ffn_up [FFN], ffn_down [H] */
        if (slot->biasData != nil)
        {
            lw->qBias = slot->biasData;
            lw->kBias = slot->biasData + H;
            lw->vBias = slot->biasData + 2L * H;
            lw->oBias = slot->biasData + 3L * H;
            lw->ffnUpBias = slot->biasData + 4L * H;
            lw->ffnDownBias = slot->biasData + 4L * H + FFN;
        }
        else
        {
            lw->qBias = nil;
            lw->kBias = nil;
            lw->vBias = nil;
            lw->oBias = nil;
            lw->ffnUpBias = nil;
            lw->ffnDownBias = nil;
        }
    }
    else
    {
        /* LLaMA: Q/O (H*H) + K/V (kvDim*H) + gate/up/down FFN */
        long gateSize;

        kvSize = KV * H;

        /* q_proj: [scale][H*H int8] */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8Q.numRows = _nr; lw->q8Q.blocksPerRow = _bpr; lw->q8Q.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8Q.data = ptr;
        lw->q8Q.numElements = attnSize;
        ptr += attnSize;

        /* k_proj: [scale][kvDim*H int8] (GQA) */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8K.numRows = _nr; lw->q8K.blocksPerRow = _bpr; lw->q8K.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8K.data = ptr;
        lw->q8K.numElements = kvSize;
        ptr += kvSize;

        /* v_proj: [scale][kvDim*H int8] (GQA) */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8V.numRows = _nr; lw->q8V.blocksPerRow = _bpr; lw->q8V.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8V.data = ptr;
        lw->q8V.numElements = kvSize;
        ptr += kvSize;

        /* o_proj: [scale][H*H int8] */
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8O.numRows = _nr; lw->q8O.blocksPerRow = _bpr; lw->q8O.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8O.data = ptr;
        lw->q8O.numElements = attnSize;
        ptr += attnSize;

        /* gate_proj: [scale][FFN*H int8] */
        gateSize = FFN * H;
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8Gate.numRows = _nr; lw->q8Gate.blocksPerRow = _bpr; lw->q8Gate.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8Gate.data = ptr;
        lw->q8Gate.numElements = gateSize;
        ptr += gateSize;

        /* up_proj */
        upSize = FFN * H;
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8Up.numRows = _nr; lw->q8Up.blocksPerRow = _bpr; lw->q8Up.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8Up.data = ptr;
        lw->q8Up.numElements = upSize;
        ptr += upSize;

        /* down_proj: [scale][H*FFN int8] */
        downSize = H * FFN;
        { long _nr; long _bpr; long _tg; memcpy(&_nr, ptr, 4); ptr += 4; memcpy(&_bpr, ptr, 4); ptr += 4; _tg = _nr * _bpr; lw->q8Down.numRows = _nr; lw->q8Down.blocksPerRow = _bpr; lw->q8Down.scales = (float *)ptr;  /* direct pointer into slot, no Arena_Alloc leak */ ptr += _tg * 4L; }
        lw->q8Down.data = ptr;
        lw->q8Down.numElements = downSize;
        ptr += downSize;

        /* Norms are in normData buffer (float) */
        lw->inputNorm = slot->normData;
        lw->postAttnNorm = slot->normData + H;

        /* Bias pointers from biasData buffer (float) */
        if (pager->hasBias && slot->biasData != nil)
        {
            lw->qBias = slot->biasData;                   /* [hiddenDim] */
            lw->kBias = slot->biasData + H;               /* [kvDim] */
            lw->vBias = slot->biasData + H + KV;          /* [kvDim] */
        }
        else
        {
            lw->qBias = nil;
            lw->kBias = nil;
            lw->vBias = nil;
        }
    }

    /* Do NOT set lw->resident = true here!
       Paged layers must always go through EnsureResident because
       slots are round-robin, data may be evicted at any time. */
}

/*------------------------------------------------------------------------------
    DiskPager_EnsureResident - Ensure a layer is loaded in RAM
------------------------------------------------------------------------------*/
Boolean DiskPager_EnsureResident(DiskPager *pager, short layer,
                                  LayerWeights *lw)
{
    short s;
    LayerSlot *slot;

    if (pager == nil || lw == nil) return false;

    /* Check if already loaded in any slot */
    for (s = 0; s < pager->numSlots; s++)
    {
        if (pager->slots[s].loadedLayer == layer)
        {
            /* Layer already in slot, pointers were set during read.
               But lw might be a different layer's struct, so re-read. */
            if (!DiskPager_ReadLayerFromDisk(pager, layer, &pager->slots[s]))
                return false;
            return true;
        }
    }

    /* Not resident - use next slot (round-robin) */
    slot = &pager->slots[pager->nextSlot];

    /* Update status bar with both generation phase + layer paging info.
       Reads current status to preserve "Processing input (X/Y)" or
       "Generating... (N tokens)" context while showing layer activity. */
    {
        extern AppGlobals gApp;
        extern void ChatWindow_UpdateStatus(void);
        char pageMsg[128];
        char phase[64];

        /* Use saved phase from generation loop (not parsed from status text,
           which gets overwritten by the pager itself) */
        if (gPagerPhase[0] != '\0')
            SafeStringCopy(phase, gPagerPhase, sizeof(phase));
        else
            phase[0] = '\0';

        if (phase[0] != '\0')
            sprintf(pageMsg, "%s | Layer %d/%ld from disk",
                    phase, (int)(layer + 1), gEngine.config.numLayers);
        else
            sprintf(pageMsg, "Loading layer %d/%ld from disk...",
                    (int)(layer + 1), gEngine.config.numLayers);

        SafeStringCopy(gApp.statusText, pageMsg, sizeof(gApp.statusText));
        ChatWindow_UpdateStatus();
    }

    if (!DiskPager_ReadLayerFromDisk(pager, layer, slot))
        return false;

    /* Pointers set by PagerReadQ8Tensor inside ReadLayerFromDisk.
       Do NOT call SetupLayerPointers here, it overwrites aligned
       data pointers with misaligned ones (type 2 crash). */

    pager->nextSlot = (pager->nextSlot + 1) % pager->numSlots;

    return true;
}

/*------------------------------------------------------------------------------
    DiskPager_PreloadRange - Pre-load a range of layers
------------------------------------------------------------------------------*/
Boolean DiskPager_PreloadRange(DiskPager *pager,
                                short startLayer, short endLayer,
                                LayerWeights *layers)
{
    short layer;

    if (pager == nil || layers == nil) return false;

    for (layer = startLayer; layer <= endLayer; layer++)
    {
        if (!DiskPager_EnsureResident(pager, layer, &layers[layer]))
            return false;
    }

    return true;
}

/*==============================================================================
    ENGINE INITIALIZATION AND MODEL LOADING
==============================================================================*/

/*------------------------------------------------------------------------------
    Engine_Initialize
------------------------------------------------------------------------------*/
OSErr Engine_Initialize(void)
{
    long arenaSize;
    OSErr err;

    if (gEngineInitialized)
        return noErr;

    memset(&gEngine, 0, sizeof(EngineState));

    gEngine.useFPU = gApp.hardware.hasFPU;
    gEngine.memTier = DetectMemoryTier(gApp.hardware.physicalRAM);

    /* Detect AltiVec (Velocity Engine) on PowerPC G4+ */
    gEngine.useAltiVec = false;
    {
        long features;
        features = 0;
        if (Gestalt(gestaltPowerPCProcessorFeatures, &features) == noErr)
        {
            if (features & (1L << gestaltPowerPCHasVectorInstructions))
                gEngine.useAltiVec = true;
        }
    }
    DebugLog_WriteNum("Engine_Init: useAltiVec =", (long)gEngine.useAltiVec);
#if __VEC__
    MathKernels_SetAltiVec(gEngine.useAltiVec);
#endif

    arenaSize = (gApp.hardware.physicalRAM / 100L) * 88L;
    if (arenaSize < 4L * 1024L * 1024L)
        arenaSize = 4L * 1024L * 1024L;
    if (arenaSize > 900L * 1024L * 1024L)
        arenaSize = 900L * 1024L * 1024L;

    err = Arena_Init(&gEngine.arena, arenaSize);
    if (err != noErr)
    {
        arenaSize = 4L * 1024L * 1024L;
        err = Arena_Init(&gEngine.arena, arenaSize);
        if (err != noErr)
            return err;

        /* Recalculate tier based on actual arena size */
        gEngine.memTier = DetectMemoryTier(arenaSize);
    }

    gApp.model.arenaSize = arenaSize;
    gEngineInitialized = true;
    return noErr;
}

/*------------------------------------------------------------------------------
    Engine_SetProgressCallback
------------------------------------------------------------------------------*/
void Engine_SetProgressCallback(void)
{
    gProgressEnabled = true;
}

/*------------------------------------------------------------------------------
    Engine_InitializeWithProgress
------------------------------------------------------------------------------*/
OSErr Engine_InitializeWithProgress(void)
{
    long arenaSize;

    if (gEngineInitialized)
        return noErr;

    memset(&gEngine, 0, sizeof(EngineState));
    gEngine.useFPU = gApp.hardware.hasFPU;
    gEngine.memTier = DetectMemoryTier(gApp.hardware.physicalRAM);

    /* Detect AltiVec (Velocity Engine) on PowerPC G4+ */
    gEngine.useAltiVec = false;
    {
        long features;
        features = 0;
        if (Gestalt(gestaltPowerPCProcessorFeatures, &features) == noErr)
        {
            if (features & (1L << gestaltPowerPCHasVectorInstructions))
                gEngine.useAltiVec = true;
        }
    }
    DebugLog_WriteNum("Engine_Init: useAltiVec =", (long)gEngine.useAltiVec);
#if __VEC__
    MathKernels_SetAltiVec(gEngine.useAltiVec);

    /* AltiVec dispatch enabled for both Int8 and Float matmul */
#endif

    arenaSize = (gApp.hardware.physicalRAM / 100L) * 88L;
    DebugLog_WriteNum("Engine_Init: physicalRAM =", gApp.hardware.physicalRAM);
    DebugLog_WriteNum("Engine_Init: calculated arenaSize =", arenaSize);
    if (arenaSize < 4L * 1024L * 1024L)
        arenaSize = 4L * 1024L * 1024L;
    if (arenaSize > 900L * 1024L * 1024L)
        arenaSize = 900L * 1024L * 1024L;
    DebugLog_WriteNum("Engine_Init: clamped arenaSize =", arenaSize);
    DebugLog_WriteNum("Engine_Init: memTier =", (long)gEngine.memTier);

    if (gProgressEnabled)
        Engine_ProgressUpdate(2, "Allocating arena memory...");

    gEngine.arena.base = nil;
    gEngine.arena.size = 0;
    gEngine.arena.used = 0;
    gArenaHandle = nil;

    {
        OSErr tempErr;
        gArenaHandle = TempNewHandle(arenaSize + 16, &tempErr);
        if (gArenaHandle != nil)
        {
            HLockHi(gArenaHandle);
            gEngine.arena.base = *gArenaHandle;
            DebugLog_WriteNum("Engine_Init: TempNewHandle OK, size =", arenaSize);
        }
        else
        {
            DebugLog_WriteNum("Engine_Init: TempNewHandle failed, err =", (long)tempErr);
        }
    }

    if (gEngine.arena.base == nil)
    {
        gEngine.arena.base = NewPtrSys(arenaSize + 16);
        DebugLog_WriteNum("Engine_Init: NewPtrSys result =", (long)gEngine.arena.base);
    }

    if (gEngine.arena.base == nil)
    {
        gEngine.arena.base = NewPtr(arenaSize + 16);
        DebugLog_WriteNum("Engine_Init: NewPtr result =", (long)gEngine.arena.base);
    }

    /* CRITICAL: Align arena base to 16 bytes for AltiVec.
       This is the Engine_InitializeWithProgress path, must match
       Arena_Init's alignment logic. */
    if (gEngine.arena.base != nil)
    {
        unsigned long rawAddr;
        unsigned long alignedAddr;
        rawAddr = (unsigned long)gEngine.arena.base;
        alignedAddr = (rawAddr + 15UL) & ~15UL;
        gEngine.arena.size = arenaSize - (long)(alignedAddr - rawAddr);
        gEngine.arena.base = (Ptr)alignedAddr;
        DebugLog_WriteNum("Engine_Init: aligned arena ptr =", (long)alignedAddr);
        DebugLog_WriteNum("Engine_Init: alignment offset =", (long)(alignedAddr - rawAddr));

        /* Arena memory is NOT zeroed here, activation buffers
           are zeroed individually after allocation (see below).
           Weight data is overwritten by file reads. */
    }

    if (gEngine.arena.base == nil)
    {
        /* Step down through decreasing arena sizes until one succeeds */
        OSErr tempErr;
        static const long fallbackSizes[] = {
            256L * 1024L * 1024L,
            192L * 1024L * 1024L,
            128L * 1024L * 1024L,
            64L * 1024L * 1024L,
            32L * 1024L * 1024L,
            0
        };
        short fi;

        DebugLog_Write("Engine_Init: full size failed, trying fallbacks");
        for (fi = 0; fallbackSizes[fi] != 0; fi++)
        {
            arenaSize = fallbackSizes[fi];
            gArenaHandle = TempNewHandle(arenaSize, &tempErr);
            if (gArenaHandle != nil)
            {
                HLockHi(gArenaHandle);
                gEngine.arena.base = *gArenaHandle;
                DebugLog_WriteNum("Engine_Init: fallback OK, size =", arenaSize);
                break;
            }
        }

        if (gEngine.arena.base == nil)
        {
            DebugLog_Write("Engine_Init: ALL allocations failed!");
            return memFullErr;
        }

        /* Recalculate tier based on actual arena, not physical RAM.
           Without this, Huge tier KV cache (45MB) + weights won't fit. */
        gEngine.memTier = DetectMemoryTier(arenaSize);
        DebugLog_WriteNum("Engine_Init: adjusted memTier =", (long)gEngine.memTier);
    }

    gEngine.arena.size = arenaSize;
    gApp.model.arenaSize = arenaSize;
    gEngineInitialized = true;

    if (gProgressEnabled)
        Engine_ProgressUpdate(5, "Arena ready.");

    return noErr;
}

/*------------------------------------------------------------------------------
    Engine_LoadModel - Load model weights from .bin file

    Supports both float32 and Q8 quantized models.
    For Q8 on memory-constrained machines, initializes disk pager.
------------------------------------------------------------------------------*/
OSErr Engine_LoadModel(const FSSpec *modelFile)
{
    OSErr err;
    short refNum;
    long headerBytes;
    char headerBuf[128];
    long *headerLongs;
    Boolean isQ8;
    short numLayerSlots;

    refNum = 0;

    if (!gEngineInitialized)
        return -1;

    DebugLog_Write("Engine_LoadModel: opening model file");
    gEngine.modelSpec = *modelFile;
    gApp.model.modelLoaded = false;

    err = FSpOpenDF(modelFile, fsRdPerm, &refNum);
    if (err != noErr)
    {
        DebugLog_WriteNum("Engine_LoadModel: FSpOpenDF FAILED =", (long)err);
        return err;
    }
    DebugLog_Write("Engine_LoadModel: file opened OK");

    /* Read 128-byte header */
    headerBytes = kModelHeaderSize;
    err = FSRead(refNum, &headerBytes, headerBuf);
    if (err != noErr || headerBytes != kModelHeaderSize)
    {
        DebugLog_WriteNum("Engine_LoadModel: header FSRead FAILED, err =", (long)err);
        DebugLog_WriteNum("Engine_LoadModel: headerBytes read =", headerBytes);
        FSClose(refNum);
        return err != noErr ? err : -2;
    }

    /* Parse header (big-endian, native on 68K) */
    headerLongs = (long *)headerBuf;
    gEngine.config.magic = headerLongs[0];
    gEngine.config.version = headerLongs[1];
    gEngine.config.numLayers = headerLongs[2];
    gEngine.config.hiddenDim = headerLongs[3];
    gEngine.config.numHeads = headerLongs[4];
    gEngine.config.numKVHeads = headerLongs[5];
    gEngine.config.headDim = headerLongs[6];
    gEngine.config.ffnDim = headerLongs[7];
    gEngine.config.vocabSize = headerLongs[8];
    gEngine.config.maxSeqLen = headerLongs[9];
    gEngine.config.ropeTheta = headerLongs[10];
    gEngine.config.quantType = headerLongs[11];
    gEngine.config.totalParams = headerLongs[12];
    gEngine.config.fileSize = headerLongs[13];
    gEngine.config.vocabOffset = headerLongs[14];
    gEngine.config.weightsOffset = headerLongs[15];
    gEngine.config.numMerges = headerLongs[16];
    gEngine.config.archType = headerLongs[17];
    gEngine.config.flags = headerLongs[18];
    gEngine.config.chatTemplate = headerLongs[19];
    gEngine.config.imStartToken = headerLongs[20];
    gEngine.config.imEndToken = headerLongs[21];

    DebugLog_WriteNum("Engine_LoadModel: archType =", gEngine.config.archType);
    DebugLog_WriteNum("Engine_LoadModel: chatTemplate =", gEngine.config.chatTemplate);
    DebugLog_WriteNum2("Engine_LoadModel: layers/dim =",
                      gEngine.config.numLayers, gEngine.config.hiddenDim);

    /* Validate header */
    if (gEngine.config.magic != 0x4D434149L)
    {
        DebugLog_WriteNum("Engine_LoadModel: BAD MAGIC =", gEngine.config.magic);
        FSClose(refNum);
        return -3;
    }
    if (gEngine.config.version < 1 || gEngine.config.version > 2)
    {
        DebugLog_WriteNum("Engine_LoadModel: BAD VERSION =", gEngine.config.version);
        FSClose(refNum);
        return -4;
    }
    if (gEngine.config.numLayers > kMaxModelLayers)
    {
        DebugLog_WriteNum("Engine_LoadModel: TOO MANY LAYERS =", gEngine.config.numLayers);
        FSClose(refNum);
        return -5;
    }
    DebugLog_Write("Engine_LoadModel: header validation passed");

    isQ8 = (gEngine.config.quantType == kQuantInt8);
    gEngine.quantType = (QuantType)gEngine.config.quantType;
    DebugLog_WriteNum("Engine_LoadModel: quantType =", gEngine.config.quantType);

    /* Determine how many layers fit in RAM based on actual sizes */
    {
        long arenaTotal;
        long fixedOverhead;
        long perLayerBytes;
        long kvDim;
        long kvCacheBytes;
        long activBytes;
        long availForLayers;
        long maxLayersFit;

        arenaTotal = gEngine.arena.size;
        kvDim = gEngine.config.numKVHeads * gEngine.config.headDim;

        /* Fixed overhead: embedding + final norm + lm_head + activations + KV cache */
        fixedOverhead = 0;
        if (isQ8)
        {
            fixedOverhead += ComputeQ8EmbedBytes(gEngine.config.vocabSize, gEngine.config.hiddenDim); /* Q8 embed */
            if (gEngine.config.flags & kFlagSeparateLMHead)
                fixedOverhead += ComputeQ8TensorFileBytes(gEngine.config.vocabSize, gEngine.config.hiddenDim); /* Q8 lm_head */
        }
        else
        {
            fixedOverhead += gEngine.config.vocabSize * gEngine.config.hiddenDim * 4L; /* f32 embed */
        }
        fixedOverhead += gEngine.config.hiddenDim * 4L; /* final norm */

        /* GPT-2: positional embeddings + final norm bias */
        if (gEngine.config.archType == kArchGPT2)
        {
            fixedOverhead += gEngine.config.maxSeqLen * gEngine.config.hiddenDim * 4L;
            fixedOverhead += gEngine.config.hiddenDim * 4L; /* final norm bias */
        }

        /* KV cache */
        {
            long kvSeqLen;
            /* Use minimum KV for overhead calculation, actual KV is
               dynamically sized in Engine_AllocActivationBuffers to
               maximize layers in RAM. Use Small (64) here so the
               maxLayersFit calculation is optimistic about layer count,
               then KV gets whatever arena remains after all layers load. */
            kvSeqLen = kKVSeqLenSmall;
            kvCacheBytes = gEngine.config.numLayers * kvSeqLen * kvDim * 4L * 2L;
        }

        /* Activation buffers (rough estimate) */
        activBytes = gEngine.config.hiddenDim * 4L * 6L     /* x, xb, q, k, v, ffnOut */
                   + gEngine.config.ffnDim * 4L * 2L        /* ffnGate, ffnUp */
                   + gEngine.config.vocabSize * 4L           /* logits */
                   + 512L * 4L;                              /* att buffer */

        fixedOverhead += kvCacheBytes + activBytes;

        /* Per-layer size (Q8) */
        if (isQ8)
        {
            if (gEngine.config.archType == kArchGPT2)
                perLayerBytes = ComputeQ8LayerBytesGPT2(gEngine.config.hiddenDim,
                                                         gEngine.config.ffnDim);
            else
                perLayerBytes = ComputeQ8LayerBytes(gEngine.config.hiddenDim,
                                                     gEngine.config.ffnDim,
                                                     kvDim,
                                                     (gEngine.config.flags & kFlagHasAttnBias) != 0);
        }
        else
            perLayerBytes = gEngine.config.hiddenDim * gEngine.config.hiddenDim * 4L * 4L
                          + gEngine.config.ffnDim * gEngine.config.hiddenDim * 4L * 3L
                          + gEngine.config.hiddenDim * 4L * 2L;

        availForLayers = arenaTotal - fixedOverhead;
        if (availForLayers < perLayerBytes)
            availForLayers = perLayerBytes; /* at least 1 layer */

        maxLayersFit = availForLayers / perLayerBytes;
        if (maxLayersFit > gEngine.config.numLayers)
            maxLayersFit = gEngine.config.numLayers;

        /* If paging is needed, reserve space for pager slots (2 slots minimum).
           Each slot holds one layer's worth of data for round-robin paging. */
        if (maxLayersFit < gEngine.config.numLayers)
        {
            long pagerSlots;
            pagerSlots = 2;  /* minimum for reasonable paging performance */
            while (maxLayersFit + pagerSlots > availForLayers / perLayerBytes
                   && maxLayersFit > 1)
            {
                maxLayersFit--;
            }
        }

        if (maxLayersFit < 1)
            maxLayersFit = 1;

        numLayerSlots = (short)maxLayersFit;

        DebugLog_WriteNum("Engine_LoadModel: fixedOverhead =", fixedOverhead);
        DebugLog_WriteNum("Engine_LoadModel: perLayerBytes =", perLayerBytes);
        DebugLog_WriteNum("Engine_LoadModel: maxLayersFit =", maxLayersFit);
    }

    if (numLayerSlots >= (short)gEngine.config.numLayers)
    {
        gEngine.layersInRAM = (short)gEngine.config.numLayers;
        gEngine.layersOnDisk = 0;
    }
    else
    {
        gEngine.layersInRAM = numLayerSlots;
        gEngine.layersOnDisk = (short)gEngine.config.numLayers - numLayerSlots;
    }

    DebugLog_WriteNum2("Engine_LoadModel: layersInRAM/onDisk =",
                      (long)gEngine.layersInRAM, (long)gEngine.layersOnDisk);

    /* Reset arena */
    Arena_Reset(&gEngine.arena);

    if (gProgressEnabled)
        Engine_ProgressUpdate(6, "Allocating weight buffers...");

    DebugLog_Write("Engine_LoadModel: allocating weight buffers...");
    if (isQ8)
    {
        err = Engine_AllocWeightPointersQ8();
    }
    else
    {
        err = Engine_AllocWeightPointers();
    }
    if (err != noErr)
    {
        DebugLog_WriteNum("Engine_LoadModel: weight alloc FAILED =", (long)err);
        FSClose(refNum);
        return err;
    }
    DebugLog_Write("Engine_LoadModel: weight alloc OK");

    /* Read weights */
    DebugLog_Write("Engine_LoadModel: reading weights from file...");
    if (isQ8)
    {
        err = Engine_ReadWeightsFromFileQ8(refNum);
    }
    else
    {
        err = Engine_ReadWeightsFromFile(refNum);
    }
    if (err != noErr)
    {
        DebugLog_WriteNum("Engine_LoadModel: weight read FAILED =", (long)err);
        FSClose(refNum);
        return err;
    }
    DebugLog_Write("Engine_LoadModel: weights loaded OK");

    /* Initialize disk pager if needed */
    gEngine.pager = nil;
    if (isQ8 && gEngine.layersOnDisk > 0)
    {
        long embedBytes;

        if (gProgressEnabled)
            Engine_ProgressUpdate(86, "Initializing disk pager...");

        embedBytes = ComputeQ8EmbedBytes(gEngine.config.vocabSize,
                                          gEngine.config.hiddenDim);

        gEngine.pager = DiskPager_Init(
            &gEngine.arena, refNum,
            gEngine.memTier, gEngine.config.weightsOffset,
            embedBytes, gEngine.quantType,
            gEngine.config.hiddenDim, gEngine.config.ffnDim,
            (short)gEngine.config.numLayers);

        if (gEngine.pager == nil)
        {
            DebugLog_Write("Engine_LoadModel: DiskPager init failed!");
            FSClose(refNum);
            return memFullErr;
        }
        DebugLog_WriteNum("Engine_LoadModel: DiskPager slots =",
                          (long)gEngine.pager->numSlots);
    }

    /* Allocate activation buffers */
    if (gProgressEnabled)
        Engine_ProgressUpdate(88, "Allocating activation buffers...");
    err = Engine_AllocActivationBuffers();
    if (err != noErr)
    {
        DebugLog_WriteNum("Engine_LoadModel: activation alloc FAILED =", (long)err);
        FSClose(refNum);
        return err;
    }
    DebugLog_Write("Engine_LoadModel: activation buffers OK");

    /* Keep file open for disk paging */
    gEngine.modelRefNum = refNum;
    gEngine.modelOpen = true;
    gEngine.seqLen = 0;
    gEngine.loaded = true;

    gApp.model.modelLoaded = true;
    gApp.model.layersInRAM = gEngine.layersInRAM;
    gApp.model.layersOnDisk = gEngine.layersOnDisk;

    DebugLog_WriteNum2("Engine_LoadModel: arena used/total =",
                      gEngine.arena.used, gEngine.arena.size);

    if (gProgressEnabled)
        Engine_ProgressUpdate(90, "Model ready.");

    return noErr;
}

/*==============================================================================
    FORWARD PASS
==============================================================================*/

/*------------------------------------------------------------------------------
    Engine_ForwardPass - Run one token through the transformer

    Supports both float32 and Q8 weight paths.
    For Q8 with disk paging, calls DiskPager_EnsureResident per layer.
------------------------------------------------------------------------------*/
static long Engine_ForwardPass(long token, long pos)
{
    long layerIdx;
    long headIdx;
    long hiddenDim;
    long headDim;
    long numHeads;
    long numKVHeads;
    long kvDim;
    long ffnDim;
    long vocabSize;
    long kvSeqLen;
    long numLayers;
    float ropeTheta;
    float *keyCacheLayer;
    float *valCacheLayer;
    float maxLogit;
    long maxIdx;
    long i;
    Boolean isQ8;
    ArchType archType;
    long flags;
    Boolean hasBias;
    Boolean hasFFNBias;
    Boolean isParallel;

    hiddenDim = gEngine.config.hiddenDim;
    headDim = gEngine.config.headDim;
    numHeads = gEngine.config.numHeads;
    numKVHeads = gEngine.config.numKVHeads;
    kvDim = numKVHeads * headDim;
    ffnDim = gEngine.config.ffnDim;
    vocabSize = gEngine.config.vocabSize;
    kvSeqLen = gEngine.kvSeqLen;
    numLayers = gEngine.config.numLayers;
    ropeTheta = (float)gEngine.config.ropeTheta;
    isQ8 = (gEngine.quantType == kQuantInt8);
    archType = (ArchType)gEngine.config.archType;
    flags = gEngine.config.flags;
    hasBias = (flags & kFlagHasAttnBias) != 0;
    hasFFNBias = (flags & kFlagHasFFNBias) != 0;
    isParallel = (flags & kFlagParallelAttnFFN) != 0;

    /* 1. Token embedding */
    if (token < 0 || token >= vocabSize)
    {
        DebugLog_WriteNum("FwdPass: BAD token ID =", token);
        return 0;
    }

    /* Clamp position to KV cache size */
    if (pos >= kvSeqLen)
    {
        DebugLog_WriteNum("FwdPass: pos exceeds KV cache, pos =", pos);
        return 0;
    }

    if (isQ8)
    {
        /* Q8 embedding: dequantize row on the fly */
        signed char *row;

        row = gEngine.q8Embed.data + token * gEngine.q8Embed.blocksPerRow * 32L;
        /* Per-group dequantization for embedding lookup */
        {
            long eb;
            long scaleBase;
            scaleBase = token * gEngine.q8Embed.blocksPerRow;
            for (eb = 0; eb < gEngine.q8Embed.blocksPerRow; eb++)
            {
                long bs;
                long be;
                float bscale;
                bs = eb * 32L;
                be = bs + 32L;
                if (be > hiddenDim) be = hiddenDim;
                bscale = gEngine.q8Embed.scales[scaleBase + eb];
                for (i = bs; i < be; i++)
                    gEngine.x[i] = (float)row[i] * bscale;
            }
        }
    }
    else
    {
        memcpy(gEngine.x, &gEngine.embedTokens[token * hiddenDim],
               hiddenDim * 4L);
    }

    /* Diagnostic removed */

    /* 1b. Add learned positional embedding (GPT-2 family) */
    if (archType == kArchGPT2 && gEngine.posEmbed != nil)
    {
        ApplyLearnedPosEmbed(gEngine.x, gEngine.posEmbed, pos, hiddenDim);
    }

    /* 2. Transformer layers */
    for (layerIdx = 0; layerIdx < numLayers; layerIdx++)
    {
        /* Ensure layer weights are resident (disk pager) */
        if (isQ8 && gEngine.pager != nil &&
            !gEngine.layers[layerIdx].resident)
        {
            if (!DiskPager_EnsureResident(gEngine.pager, (short)layerIdx,
                                           &gEngine.layers[layerIdx]))
            {
                DebugLog_WriteNum("FwdPass: pager failed layer =", layerIdx);
                return 0;
            }
        }

        /* 2a. Pre-attention norm */
        if (archType == kArchGPT2)
        {
            LayerNorm(gEngine.xb, gEngine.x,
                      gEngine.layers[layerIdx].inputNorm,
                      gEngine.layers[layerIdx].inputNormBias, hiddenDim);
        }
        else
        {
            RMSNorm(gEngine.xb, gEngine.x,
                    gEngine.layers[layerIdx].inputNorm, hiddenDim);
        }

        /* Diagnostic removed */

        /* 2b. Q/K/V projections */
        if (isQ8)
        {
            MatVecMul_Int8(gEngine.layers[layerIdx].q8Q.data,
                           gEngine.xb, gEngine.q,
                           hiddenDim, hiddenDim,
                           gEngine.layers[layerIdx].q8Q.scales,
                           gEngine.layers[layerIdx].q8Q.blocksPerRow);
            MatVecMul_Int8(gEngine.layers[layerIdx].q8K.data,
                           gEngine.xb, gEngine.k,
                           kvDim, hiddenDim,
                           gEngine.layers[layerIdx].q8K.scales,
                           gEngine.layers[layerIdx].q8K.blocksPerRow);
            MatVecMul_Int8(gEngine.layers[layerIdx].q8V.data,
                           gEngine.xb, gEngine.v,
                           kvDim, hiddenDim,
                           gEngine.layers[layerIdx].q8V.scales,
                           gEngine.layers[layerIdx].q8V.blocksPerRow);
        }
        else
        {
            MatVecMul_Float(gEngine.layers[layerIdx].qProj,
                            gEngine.xb, gEngine.q,
                            hiddenDim, hiddenDim);


            MatVecMul_Float(gEngine.layers[layerIdx].kProj,
                            gEngine.xb, gEngine.k,
                            kvDim, hiddenDim);
            MatVecMul_Float(gEngine.layers[layerIdx].vProj,
                            gEngine.xb, gEngine.v,
                            kvDim, hiddenDim);
        }

        /* Add QKV bias if present */
        if (hasBias && gEngine.layers[layerIdx].qBias != nil)
        {
            VecAdd(gEngine.q, gEngine.layers[layerIdx].qBias, hiddenDim);
            VecAdd(gEngine.k, gEngine.layers[layerIdx].kBias, kvDim);
            VecAdd(gEngine.v, gEngine.layers[layerIdx].vBias, kvDim);
        }

        /* 2c. Position encoding */
        if (archType == kArchLLaMA)
        {
            /* RoPE: apply to first numKVHeads Q+K pairs (1:1 mapping) */
            for (headIdx = 0; headIdx < numKVHeads; headIdx++)
            {
                ApplyRoPE(&gEngine.q[headIdx * headDim],
                          &gEngine.k[headIdx * headDim],
                          headDim, pos, ropeTheta);
            }
            /* Remaining Q heads: RoPE on Q only (K already done above).
               Pass nil for K, ApplyRoPE skips K rotation when nil. */
            for (headIdx = numKVHeads; headIdx < numHeads; headIdx++)
            {
                ApplyRoPE(&gEngine.q[headIdx * headDim],
                          nil, headDim, pos, ropeTheta);
            }
        }
        /* GPT-2: no per-layer pos encoding (added once after embedding) */

        /* 2d. Store K and V in cache (using kvDim for GQA support) */
        keyCacheLayer = &gEngine.keyCache[
            layerIdx * kvSeqLen * kvDim + pos * kvDim];
        valCacheLayer = &gEngine.valueCache[
            layerIdx * kvSeqLen * kvDim + pos * kvDim];
        memcpy(keyCacheLayer, gEngine.k, kvDim * 4L);
        memcpy(valCacheLayer, gEngine.v, kvDim * 4L);

        /* 2e. Multi-head attention (with GQA/MQA support) */
        memset(gEngine.xb, 0, hiddenDim * 4L);

        for (headIdx = 0; headIdx < numHeads; headIdx++)
        {
            float *qHead;
            float *attOut;
            float invSqrtD;
            long p;
            long kvHeadIdx;

            qHead = &gEngine.q[headIdx * headDim];
            attOut = &gEngine.xb[headIdx * headDim];
            kvHeadIdx = headIdx * numKVHeads / numHeads;

            for (p = 0; p <= pos; p++)
            {
                float *kCached;
                kCached = &gEngine.keyCache[
                    layerIdx * kvSeqLen * kvDim +
                    p * kvDim + kvHeadIdx * headDim];
                gEngine.att[p] = VecDot(qHead, kCached, headDim);
            }

            invSqrtD = (float)(1.0 / sqrt((double)headDim));
            VecScale(gEngine.att, invSqrtD, (pos + 1));
            Softmax(gEngine.att, (pos + 1));

            for (p = 0; p <= pos; p++)
            {
                float *vCached;
                long d;

                vCached = &gEngine.valueCache[
                    layerIdx * kvSeqLen * kvDim +
                    p * kvDim + kvHeadIdx * headDim];

                for (d = 0; d < headDim; d++)
                {
                    attOut[d] += gEngine.att[p] * vCached[d];
                }
            }
        }

        /* 2f. Output projection */
        if (isQ8)
        {
            MatVecMul_Int8(gEngine.layers[layerIdx].q8O.data,
                           gEngine.xb, gEngine.ffnOut,
                           hiddenDim, hiddenDim,
                           gEngine.layers[layerIdx].q8O.scales,
                           gEngine.layers[layerIdx].q8O.blocksPerRow);
        }
        else
        {
            MatVecMul_Float(gEngine.layers[layerIdx].oProj,
                            gEngine.xb, gEngine.ffnOut,
                            hiddenDim, hiddenDim);
        }
        if (hasBias && gEngine.layers[layerIdx].oBias != nil)
        {
            VecAdd(gEngine.ffnOut, gEngine.layers[layerIdx].oBias, hiddenDim);
        }

        /* 2g. Residual connection (attention output) */
        VecAdd(gEngine.x, gEngine.ffnOut, hiddenDim);

        /* 2h. Post-attention norm */
        if (archType == kArchGPT2)
        {
            LayerNorm(gEngine.xb, gEngine.x,
                      gEngine.layers[layerIdx].postAttnNorm,
                      gEngine.layers[layerIdx].postAttnNormBias, hiddenDim);
        }
        else
        {
            RMSNorm(gEngine.xb, gEngine.x,
                    gEngine.layers[layerIdx].postAttnNorm, hiddenDim);
        }

        /* 2i. FFN (architecture-dependent) */
        if (archType == kArchGPT2)
        {
            /* GPT-2 style: fc1 -> GeLU -> fc2 (2-matrix FFN) */
            if (isQ8)
            {
                MatVecMul_Int8(gEngine.layers[layerIdx].q8Up.data,
                               gEngine.xb, gEngine.ffnGate,
                               ffnDim, hiddenDim,
                               gEngine.layers[layerIdx].q8Up.scales,
                               gEngine.layers[layerIdx].q8Up.blocksPerRow);
            }
            else
            {
                MatVecMul_Float(gEngine.layers[layerIdx].upProj,
                                gEngine.xb, gEngine.ffnGate,
                                ffnDim, hiddenDim);
            }
            if (hasFFNBias && gEngine.layers[layerIdx].ffnUpBias != nil)
            {
                VecAdd(gEngine.ffnGate, gEngine.layers[layerIdx].ffnUpBias, ffnDim);
            }
            GeLU(gEngine.ffnGate, gEngine.ffnGate, ffnDim);

            if (isQ8)
            {
                MatVecMul_Int8(gEngine.layers[layerIdx].q8Down.data,
                               gEngine.ffnGate, gEngine.ffnOut,
                               hiddenDim, ffnDim,
                               gEngine.layers[layerIdx].q8Down.scales,
                               gEngine.layers[layerIdx].q8Down.blocksPerRow);
            }
            else
            {
                MatVecMul_Float(gEngine.layers[layerIdx].downProj,
                                gEngine.ffnGate, gEngine.ffnOut,
                                hiddenDim, ffnDim);
            }
            if (hasFFNBias && gEngine.layers[layerIdx].ffnDownBias != nil)
            {
                VecAdd(gEngine.ffnOut, gEngine.layers[layerIdx].ffnDownBias, hiddenDim);
            }
        }
        else
        {
            /* LLaMA style: SwiGLU 3-matrix FFN (gate + up -> silu -> down) */
            if (isQ8)
            {
                MatVecMul_Int8(gEngine.layers[layerIdx].q8Gate.data,
                               gEngine.xb, gEngine.ffnGate,
                               ffnDim, hiddenDim,
                               gEngine.layers[layerIdx].q8Gate.scales,
                               gEngine.layers[layerIdx].q8Gate.blocksPerRow);
                MatVecMul_Int8(gEngine.layers[layerIdx].q8Up.data,
                               gEngine.xb, gEngine.ffnUp,
                               ffnDim, hiddenDim,
                               gEngine.layers[layerIdx].q8Up.scales,
                               gEngine.layers[layerIdx].q8Up.blocksPerRow);
            }
            else
            {
                MatVecMul_Float(gEngine.layers[layerIdx].gateProj,
                                gEngine.xb, gEngine.ffnGate,
                                ffnDim, hiddenDim);
                MatVecMul_Float(gEngine.layers[layerIdx].upProj,
                                gEngine.xb, gEngine.ffnUp,
                                ffnDim, hiddenDim);
            }
            SwiGLU(gEngine.ffnGate, gEngine.ffnGate, gEngine.ffnUp, ffnDim);

            if (isQ8)
            {
                MatVecMul_Int8(gEngine.layers[layerIdx].q8Down.data,
                               gEngine.ffnGate, gEngine.ffnOut,
                               hiddenDim, ffnDim,
                               gEngine.layers[layerIdx].q8Down.scales,
                               gEngine.layers[layerIdx].q8Down.blocksPerRow);
            }
            else
            {
                MatVecMul_Float(gEngine.layers[layerIdx].downProj,
                                gEngine.ffnGate, gEngine.ffnOut,
                                hiddenDim, ffnDim);
            }
        }

        /* 2j. Residual connection (FFN output) */
        VecAdd(gEngine.x, gEngine.ffnOut, hiddenDim);

        /* Diagnostic removed */

        if ((layerIdx & 3) == 3)
            SystemTask();
    }

    /* 3. Final norm */
    if (archType == kArchGPT2)
    {
        LayerNorm(gEngine.x, gEngine.x, gEngine.finalNorm,
                  gEngine.finalNormBias, hiddenDim);
    }
    else
    {
        RMSNorm(gEngine.x, gEngine.x, gEngine.finalNorm, hiddenDim);
    }

    /* 4. LM head: logits = weights * x
       During prefill, skip for non-final tokens (result is discarded) */
    if (sSkipLMHead)
        return 0;

    if (gEngine.lmHead != nil)
    {
        /* Separate (untied) LM head */
        MatVecMul_Float(gEngine.lmHead, gEngine.x,
                        gEngine.logits,
                        vocabSize, hiddenDim);
    }
    else if (gEngine.q8LMHead.data != nil)
    {
        /* Separate Q8 LM head */
        MatVecMul_Int8(gEngine.q8LMHead.data, gEngine.x,
                       gEngine.logits,
                       vocabSize, hiddenDim,
                       gEngine.q8LMHead.scales,
                       gEngine.q8LMHead.blocksPerRow);
    }
    else if (isQ8)
    {
        /* Tied to Q8 embeddings */
        MatVecMul_Int8(gEngine.q8Embed.data, gEngine.x,
                       gEngine.logits,
                       vocabSize, hiddenDim,
                       gEngine.q8Embed.scales,
                       gEngine.q8Embed.blocksPerRow);
    }
    else
    {
        /* Tied to float32 embeddings */
        MatVecMul_Float(gEngine.embedTokens, gEngine.x,
                        gEngine.logits,
                        vocabSize, hiddenDim);
    }

    /* Repetition penalty */
    if (sRepTokens != nil && sRepTokenCount > 0)
    {
        long ri;
        for (ri = 0; ri < sRepTokenCount; ri++)
        {
            long repTok;
            repTok = sRepTokens[ri];
            if (repTok >= 0 && repTok < vocabSize)
            {
                if (gEngine.logits[repTok] > 0.0f)
                    gEngine.logits[repTok] /= kRepetitionPenalty;
                else
                    gEngine.logits[repTok] *= kRepetitionPenalty;
            }
        }
    }

    /* Greedy decode: argmax */
    maxLogit = gEngine.logits[0];
    maxIdx = 0;
    for (i = 1; i < vocabSize; i++)
    {
        if (gEngine.logits[i] > maxLogit)
        {
            maxLogit = gEngine.logits[i];
            maxIdx = i;
        }
    }

    /* Debug: log top-2 for first few tokens */
    if (sRepTokenCount >= 0 && sRepTokenCount < 5)
    {
        float secondLogit;
        secondLogit = -1e30f;
        for (i = 0; i < vocabSize; i++)
        {
            if (i != maxIdx && gEngine.logits[i] > secondLogit)
                secondLogit = gEngine.logits[i];
        }
        /* Log argmax result: which token won and by how much.
           margin*1000 > 500 = confident, < 100 = could flip on different hardware */
        DebugLog_WriteNum2("FwdPass: top1 id/margin*1000=", maxIdx,
                          (long)((maxLogit - secondLogit) * 1000.0f));
    }

    return maxIdx;
}

/*==============================================================================
    TOKEN GENERATION
==============================================================================*/

/*------------------------------------------------------------------------------
    Engine_Generate - Generate tokens from input
------------------------------------------------------------------------------*/
long Engine_Generate(const long *inputTokens, long inputLen,
                    long *outputTokens, long maxOutput)
{
    long i;
    long nextToken;
    long numGenerated;

    if (!gEngine.loaded)
        return -1;

    gEngine.generating = true;
    gEngine.seqLen = 0;
    numGenerated = 0;

    /* === GENERATION LOG === */
    DebugLog_Write("========================================");
    DebugLog_Write("ENGINE GENERATE: new request");
    DebugLog_Write("========================================");
    DebugLog_WriteNum("  input tokens    = ", inputLen);
    DebugLog_WriteNum("  max output      = ", maxOutput);
    DebugLog_WriteNum("  KV cache size   = ", gEngine.kvSeqLen);
    DebugLog_WriteNum("  model layers    = ", gEngine.config.numLayers);
    DebugLog_WriteNum("  hidden dim      = ", gEngine.config.hiddenDim);
    DebugLog_WriteNum("  quantType       = ", (long)gEngine.quantType);
    DebugLog_WriteNum("  useAltiVec      = ", (long)gEngine.useAltiVec);

    /* Log input token IDs */
    {
        long logIdx;
        char tokBuf[32];
        DebugLog_Write("  input token IDs:");
        for (logIdx = 0; logIdx < inputLen && logIdx < 40; logIdx++)
        {
            sprintf(tokBuf, "    [%ld] = %ld", logIdx, inputTokens[logIdx]);
            DebugLog_Write(tokBuf);
        }
        if (inputLen > 40)
            DebugLog_Write("    ... (truncated)");
    }

    /* Decode and log the input prompt as text */
    {
        char promptText[512];
        long promptLen;
        extern long Tokenizer_Decode(const long *, long, char *, long);
        promptLen = Tokenizer_Decode(inputTokens, inputLen,
                                      promptText, sizeof(promptText) - 1);
        promptText[promptLen] = '\0';
        DebugLog_Write("  input prompt text:");
        DebugLog_Write(promptText);
    }
    DebugLog_Write("----------------------------------------");

    /* Prefill: process all input tokens
       Skip LM head for non-final tokens (saves ~6% per token) */
    {
        long prefillStartTick;
        prefillStartTick = TickCount();
        DebugLog_WriteNum("PREFILL: processing ", inputLen);
        DebugLog_Write("  each input token runs through all transformer layers");
        DebugLog_Write("  LM head skipped for non-final tokens (6%% speedup)");
    for (i = 0; i < inputLen; i++)
    {
        sSkipLMHead = (i < inputLen - 1);
        /* Update phase for disk pager status display */
        sprintf(gPagerPhase, "Prefill %ld/%ld", i + 1, inputLen);
        nextToken = Engine_ForwardPass(inputTokens[i], gEngine.seqLen);
        gEngine.seqLen++;

        /* Report prefill progress to UI */
        if (gEnginePrefillCallback != nil)
            gEnginePrefillCallback(i + 1, inputLen);

        /* Allow cancel + UI updates during prefill */
        {
            EventRecord prefillEvent;
            SystemTask();
            if (EventAvail(keyDownMask, &prefillEvent))
            {
                if ((prefillEvent.message & charCodeMask) == 0x1B)
                {
                    GetNextEvent(keyDownMask, &prefillEvent);
                    gEngine.generating = false;
                    DebugLog_Write("Engine_Generate: cancelled during prefill");
                }
            }
        }

        if (!gEngine.generating)
            break;
    }
    sSkipLMHead = false;
    {
        long prefillTicks;
        long prefillMS;
        prefillTicks = TickCount() - prefillStartTick;
        prefillMS = (prefillTicks * 1000L) / 60L;
        DebugLog_Write("PREFILL COMPLETE");
        DebugLog_WriteNum("  ticks elapsed   = ", prefillTicks);
        DebugLog_WriteNum("  milliseconds    = ", prefillMS);
        if (inputLen > 0)
            DebugLog_WriteNum("  ms/token        = ", prefillMS / inputLen);
    }
    }  /* close prefillStartTick block */
    DebugLog_Write("----------------------------------------");
    DebugLog_Write("GENERATION: autoregressive token-by-token");
    DebugLog_Write("  each token = full forward pass + argmax");

    sRepTokens = outputTokens;
    sRepTokenCount = 0;

    /* Autoregressive generation */
    {
        Boolean sawSep;
        short tokensAfterSep;
        Boolean isExternal;
        long eosToken;

        sawSep = false;
        tokensAfterSep = 0;
        isExternal = (gEngine.config.chatTemplate != kChatTemplateCustom);

        /* Determine EOS token: use model-specific imEndToken for ChatML,
           fallback to kTokenEOS (2) for custom models */
        eosToken = 2;  /* kTokenEOS default */
        if (isExternal && gEngine.config.imEndToken > 0)
            eosToken = gEngine.config.imEndToken;

        while (gEngine.generating && numGenerated < maxOutput)
        {
            if (gEngine.seqLen >= gEngine.kvSeqLen)
                break;

            /* Stop at EOS before adding to output */
            if (nextToken == eosToken)
                break;

            outputTokens[numGenerated] = nextToken;
            numGenerated++;
            sRepTokenCount = numGenerated;

            /* Update phase for disk pager status */
            sprintf(gPagerPhase, "Gen %ld tok", numGenerated);

            /* Log each generated token with its text */
            if (numGenerated <= 30 || (numGenerated % 50) == 0)
            {
                char genBuf[128];
                char tokText[64];
                long tokTextLen;
                extern long Tokenizer_Decode(const long *, long, char *, long);

                /* Decode single token to text */
                tokTextLen = Tokenizer_Decode(&nextToken, 1,
                                               tokText, sizeof(tokText) - 1);
                tokText[tokTextLen] = '\0';

                sprintf(genBuf, "  tok[%ld] = %ld  \"%s\"",
                        numGenerated - 1, nextToken, tokText);
                DebugLog_Write(genBuf);
            }

            if (gEngineTokenCallback != nil)
                gEngineTokenCallback(nextToken, numGenerated);

            /* SEP/CMD safety logic only applies to custom MacinAI models */
            if (!isExternal)
            {
                if (nextToken == 4)  /* kTokenSEP */
                {
                    sawSep = true;
                    tokensAfterSep = 0;
                }

                if (sawSep)
                {
                    tokensAfterSep++;
                    if (tokensAfterSep > 256)
                    {
                        DebugLog_WriteNum("Engine_Generate: forcing EOS, tokensAfterSep =", (long)tokensAfterSep);
                        outputTokens[numGenerated] = 2;
                        numGenerated++;
                        if (gEngineTokenCallback != nil)
                            gEngineTokenCallback(2, numGenerated);
                        break;
                    }
                }

                if (!sawSep && numGenerated > kMaxResponseTokens)
                {
                    DebugLog_WriteNum("Engine_Generate: forcing stop, numGenerated =", numGenerated);
                    outputTokens[numGenerated] = 4;  /* [SEP] */
                    numGenerated++;
                    outputTokens[numGenerated] = 5;  /* [CMD:NONE] */
                    numGenerated++;
                    outputTokens[numGenerated] = 2;  /* [EOS] */
                    numGenerated++;
                    if (gEngineTokenCallback != nil)
                        gEngineTokenCallback(2, numGenerated);
                    break;
                }
            }

            nextToken = Engine_ForwardPass(nextToken, gEngine.seqLen);
            gEngine.seqLen++;

            SystemTask();

            /* Check for escape to cancel generation */
            {
                EventRecord cancelEvent;
                if (EventAvail(keyDownMask, &cancelEvent))
                {
                    if ((cancelEvent.message & charCodeMask) == 0x1B)
                    {
                        GetNextEvent(keyDownMask, &cancelEvent);
                        gEngine.generating = false;
                        DebugLog_Write("Engine_Generate: cancelled by user");
                    }
                }
            }
        }
    }

    sRepTokens = nil;
    sRepTokenCount = 0;

    /* === GENERATION SUMMARY === */
    DebugLog_Write("========================================");
    DebugLog_Write("GENERATION COMPLETE");
    DebugLog_Write("========================================");
    DebugLog_WriteNum("  tokens generated = ", numGenerated);
    DebugLog_WriteNum("  final seq length = ", gEngine.seqLen);

    /* Decode and log the full generated response */
    if (numGenerated > 0)
    {
        char responseBuf[2048];
        long responseLen;
        extern long Tokenizer_Decode(const long *, long, char *, long);
        responseLen = Tokenizer_Decode(outputTokens, numGenerated,
                                        responseBuf, sizeof(responseBuf) - 1);
        responseBuf[responseLen] = '\0';
        DebugLog_Write("  full response text:");
        DebugLog_Write("  ----");
        DebugLog_Write(responseBuf);
        DebugLog_Write("  ----");
    }
    DebugLog_Write("========================================");
    gEngine.generating = false;
    return numGenerated;
}

/*==============================================================================
    STATUS AND CLEANUP
==============================================================================*/

Boolean Engine_IsReady(void)
{
    return gEngine.loaded;
}

Boolean Engine_IsGenerating(void)
{
    return gEngine.generating;
}

void Engine_StopGeneration(void)
{
    gEngine.generating = false;
}

void Engine_GetStatusString(char *buffer, short maxLen)
{
    if (!gEngineInitialized)
    {
        SafeStringCopy(buffer, "Engine not initialized", maxLen);
        return;
    }

    if (!gEngine.loaded)
    {
        sprintf(buffer, "Arena: %ldKB | No model loaded",
                gEngine.arena.size / 1024L);
    }
    else
    {
        sprintf(buffer, "%ld layers (%ld RAM, %ld disk) | %s%s%s | Arena: %ldKB (%ldKB used)",
                (long)gEngine.config.numLayers,
                (long)gEngine.layersInRAM,
                (long)gEngine.layersOnDisk,
                gEngine.quantType == kQuantInt8 ? "Q8" : "f32",
                gEngine.useFPU ? " FPU" : "",
                gEngine.useAltiVec ? " AltiVec" : "",
                gEngine.arena.size / 1024L,
                gEngine.arena.used / 1024L);
    }
}

Ptr Engine_ArenaAlloc(long size)
{
    if (!gEngineInitialized || gEngine.arena.base == nil)
        return nil;
    return Arena_Alloc(&gEngine.arena, size);
}

void Engine_Cleanup(void)
{
    if (gEngine.modelOpen && gEngine.modelRefNum != 0)
    {
        FSClose(gEngine.modelRefNum);
        gEngine.modelOpen = false;
    }

    Arena_Free(&gEngine.arena);

    gEngine.pager = nil;
    gEngineInitialized = false;
    gEngine.loaded = false;
}
