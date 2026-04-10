/*------------------------------------------------------------------------------
    InferenceEngine.h - Local LLM Inference Engine

    Manages the complete inference pipeline for MacinAI Local:
    - Arena memory allocation (single contiguous block)
    - Model weight loading from disk (.bin file)
    - Forward pass (attention, FFN, RMSNorm, SwiGLU, RoPE)
    - Token generation (greedy/temperature sampling)
    - Disk paging for layers that don't fit in RAM
    - FPU vs SANE software math path selection
    - Q8 quantized weights (per-tensor scale + int8 data)

    Supports multiple architectures via archType field:
    - LLaMA: RMSNorm, SwiGLU, RoPE (LLaMA, Mistral, Qwen, Gemma, TinyLlama)
    - GPT-2: LayerNorm, GeLU, learned pos (GPT-2, OPT, Pythia, GPT-J, Falcon)

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

#ifndef INFERENCEENGINE_H
#define INFERENCEENGINE_H

#include <Types.h>
#include <Files.h>

/*------------------------------------------------------------------------------
    Constants
------------------------------------------------------------------------------*/

#define kModelHeaderSize    128     /* .bin file header size in bytes */
#define kMaxModelLayers     80      /* Max layers (supports up to 7B models) */

/* Memory tier RAM thresholds */
#define kTierHugeRAM        (256L * 1024L * 1024L)
#define kTierLargeRAM       (128L * 1024L * 1024L)
#define kTierMediumRAM      (32L * 1024L * 1024L)

/* Layer slot counts by memory tier */
#define kLayerSlotsHuge     24      /* All layers resident, 0 disk rounds */
#define kLayerSlotsLarge    12      /* 12 resident, 2 rounds per token */
#define kLayerSlotsMedium   3       /* 3 resident, 6 rounds per token */
#define kLayerSlotsSmall    1       /* 1 resident, 18 rounds per token */

/* KV cache sequence length by tier */
#define kKVSeqLenHuge       512
#define kKVSeqLenLarge      256
#define kKVSeqLenMedium     128
#define kKVSeqLenSmall      64

/* Disk read chunk size (8KB for System 7.5.3 compatibility) */
#define kDiskChunkSize      8192L
#define kPagerChunkSize     262144L  /* 256KB for pager layer reads */


/*------------------------------------------------------------------------------
    Architecture Type

    Determines which forward pass path to use.
    Stored in ModelConfig.archType (formerly reserved[0]).

    kArchLLaMA:  RMSNorm + SwiGLU 3-matrix FFN + RoPE (no bias)
                 Models: LLaMA, Mistral, Qwen, Gemma, TinyLlama, SmolLM, StableLM
    kArchGPT2:   LayerNorm + GeLU 2-matrix FFN + learned pos embed + bias
                 Models: GPT-2, OPT, GPT-J, GPT-NeoX, Pythia, Falcon, Phi
------------------------------------------------------------------------------*/
typedef enum {
    kArchLLaMA = 0,             /* Default: LLaMA-family (current models) */
    kArchGPT2  = 1              /* GPT-2-family (LayerNorm + GeLU + learned pos) */
} ArchType;

/*------------------------------------------------------------------------------
    Chat Template Type

    Determines how prompts are formatted for the model.
    Stored in ModelConfig.chatTemplate (formerly reserved[2]).

    kChatTemplateCustom: MacinAI [BOS] System:... [SEP] User:... [SEP] Assistant:
    kChatTemplateChatML: <|im_start|>user\n...<|im_end|>\n<|im_start|>assistant\n
    kChatTemplateRaw:    No template wrapping, just pass raw text
------------------------------------------------------------------------------*/
typedef enum {
    kChatTemplateCustom = 0,    /* MacinAI training format (default) */
    kChatTemplateChatML = 1,    /* ChatML / SmolLM / Qwen format */
    kChatTemplateRaw    = 2,    /* No template, raw text input */
    kChatTemplateZephyr = 3     /* Zephyr / TinyLlama: <|user|>\n...\n</s> */
} ChatTemplate;

/*------------------------------------------------------------------------------
    Model Configuration (read from .bin file header)

    Fixed 128-byte header at start of .bin file.
    All fields are long (4 bytes) to avoid 68K alignment issues.
    All multi-byte values stored big-endian (native 68K byte order).

    File layout:
      [0..127]              ModelConfig header (128 bytes)
      [vocabOffset..]       Vocab section (token strings + BPE merges)
      [weightsOffset..]     Weight tensors
------------------------------------------------------------------------------*/
typedef struct {
    long    magic;              /* 'MCAI' = 0x4D434149 */
    long    version;            /* Format version (1) */
    long    numLayers;          /* 18 transformer layers */
    long    hiddenDim;          /* 640 */
    long    numHeads;           /* 10 attention heads */
    long    numKVHeads;         /* 10 (same as numHeads for full MHA) */
    long    headDim;            /* 64 (hiddenDim / numHeads) */
    long    ffnDim;             /* 1728 (SwiGLU intermediate) */
    long    vocabSize;          /* 8205 (8192 BPE + 13 special) */
    long    maxSeqLen;          /* 1024 */
    long    ropeTheta;          /* 10000 (stored as integer, 0 for learned pos) */
    long    quantType;          /* 0=float32, 1=int8, 2=int4 */
    long    totalParams;        /* Total parameter count */
    long    fileSize;           /* Total .bin file size */
    long    vocabOffset;        /* Byte offset to vocab section */
    long    weightsOffset;      /* Byte offset to weights section */
    long    numMerges;          /* Number of BPE merge rules */
    long    archType;           /* 0=LLaMA, 1=GPT-2 (was reserved[0]) */
    long    flags;              /* Bit flags (was reserved[1]):
                                   bit 0: has bias on QKV/O projections
                                   bit 1: has bias on FFN projections
                                   bit 2: has separate lm_head (not tied)
                                   bit 3: parallel attn+FFN (GPT-J style) */
    long    chatTemplate;       /* 0=custom, 1=ChatML, 2=raw (was reserved[2]) */
    long    imStartToken;       /* <|im_start|> token ID (ChatML models) */
    long    imEndToken;         /* <|im_end|> / EOS token ID (ChatML models) */
    long    preTokenizerType;   /* 0=GPT-2 (default), 1=Qwen (single digits) */
    long    reserved[9];        /* Pad to 128 bytes (future use) */
} ModelConfig;

/* ModelConfig.flags bit definitions */
#define kFlagHasAttnBias    0x00000001L  /* QKV/O projections have bias */
#define kFlagHasFFNBias     0x00000002L  /* FFN projections have bias */
#define kFlagSeparateLMHead 0x00000004L  /* lm_head not tied to embed */
#define kFlagParallelAttnFFN 0x00000008L /* GPT-J style parallel attn+FFN */

/*------------------------------------------------------------------------------
    Quantization type and Q8 tensor
------------------------------------------------------------------------------*/

typedef enum {
    kQuantFloat32 = 0,          /* Float32 weights (testing only) */
    kQuantInt8 = 1,             /* Q8_0: per-tensor scale + int8 data */
    kQuantInt4 = 2              /* Q4_K_M (future) */
} QuantType;

typedef struct {
    signed char *data;          /* Int8 weight data (padded to block boundaries) */
    float       *scales;        /* Per-group dequantization scales */
    long        numRows;        /* Number of rows */
    long        blocksPerRow;   /* Number of scale groups per row (cols/32) */
    long        numElements;    /* Total number of elements */
} Q8Tensor;

/*------------------------------------------------------------------------------
    Memory tier - determines paging strategy and KV cache size
------------------------------------------------------------------------------*/

typedef enum {
    kMemTierHuge,               /* 256+ MB: all layers resident */
    kMemTierLarge,              /* 128-255 MB: 12 layers resident */
    kMemTierMedium,             /* 32-127 MB: 3 layers resident */
    kMemTierSmall               /* 8-31 MB: 1 layer resident */
} MemoryTier;

/*------------------------------------------------------------------------------
    Arena Allocator
    Single contiguous memory block with bump pointer.
    No fragmentation, no individual frees.
------------------------------------------------------------------------------*/
typedef struct {
    Ptr     base;               /* Start of arena memory */
    long    size;               /* Total arena size */
    long    used;               /* Current allocation offset */
} ArenaAllocator;

/*------------------------------------------------------------------------------
    Vocab Section Layout (at vocabOffset)

    For token 0 to vocabSize-1:
      [1 byte]  string length
      [N bytes] token string (no null terminator)

    Then for merge 0 to numMerges-1:
      [1 byte]  first token string length
      [N bytes] first token string
      [1 byte]  second token string length
      [N bytes] second token string
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Weight Tensor Order in .bin File

    After weightsOffset, tensors appear in this fixed order:

    === LLaMA architecture (archType=0) ===
    1. embed_tokens (Q8 or f32)  [vocabSize x hiddenDim]
    2. For each layer 0..numLayers-1:
       a. q_proj         (Q8 or f32)  [hiddenDim x hiddenDim]
       b. k_proj         (Q8 or f32)  [kvDim x hiddenDim] (kvDim=numKVHeads*headDim)
       c. v_proj         (Q8 or f32)  [kvDim x hiddenDim]
       d. o_proj         (Q8 or f32)  [hiddenDim x hiddenDim]
       e. gate_proj      (Q8 or f32)  [ffnDim x hiddenDim]
       f. up_proj        (Q8 or f32)  [ffnDim x hiddenDim]
       g. down_proj      (Q8 or f32)  [hiddenDim x ffnDim]
       h. input_norm     (always f32) [hiddenDim]
       i. post_attn_norm (always f32) [hiddenDim]
       j-o. bias terms   (always f32) [hiddenDim or ffnDim] (only if flags set)
    3. final_norm         (always f32) [hiddenDim]
    4. lm_head: TIED to embed_tokens unless kFlagSeparateLMHead set

    === GPT-2 architecture (archType=1) ===
    1. embed_tokens       (Q8 or f32) [vocabSize x hiddenDim]
    2. pos_embed          (always f32) [maxSeqLen x hiddenDim]
    3. For each layer 0..numLayers-1:
       a-d. q/k/v/o_proj (Q8 or f32)  [same as LLaMA]
       e. fc1 (=upProj)  (Q8 or f32)  [ffnDim x hiddenDim]
       f. fc2 (=downProj) (Q8 or f32) [hiddenDim x ffnDim]
       g. input_norm_w    (always f32) [hiddenDim]
       h. input_norm_b    (always f32) [hiddenDim]
       i. post_attn_norm_w (f32)       [hiddenDim]
       j. post_attn_norm_b (f32)       [hiddenDim]
       k-p. bias terms    (always f32) (if flags set)
    4. final_norm_w       (always f32) [hiddenDim]
    5. final_norm_b       (always f32) [hiddenDim]
    6. lm_head: TIED or separate per kFlagSeparateLMHead

    Q8 tensor on disk: [4-byte float scale][N bytes int8 data]
    F32 tensor on disk: [N * 4 bytes float data], big-endian
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    Layer Weights - Pointers into arena for one transformer layer

    Supports both float32 and Q8 quantized weights.
    The quantType in EngineState determines which fields are used.
    Norms are always float32 regardless of quantType.
------------------------------------------------------------------------------*/
typedef struct {
    /* Float32 weights (used when quantType == kQuantFloat32) */
    float   *qProj;             /* [hiddenDim x hiddenDim] (or [kvDim x hiddenDim] for GQA) */
    float   *kProj;             /* [hiddenDim x hiddenDim] (or [kvDim x hiddenDim] for GQA) */
    float   *vProj;             /* [hiddenDim x hiddenDim] (or [kvDim x hiddenDim] for GQA) */
    float   *oProj;             /* [hiddenDim x hiddenDim] */
    float   *gateProj;          /* [ffnDim x hiddenDim] (LLaMA: gate, GPT-2: unused) */
    float   *upProj;            /* [ffnDim x hiddenDim] (LLaMA: up,   GPT-2: fc1) */
    float   *downProj;          /* [hiddenDim x ffnDim] (LLaMA: down, GPT-2: fc2) */

    /* Q8 weights (used when quantType == kQuantInt8) */
    Q8Tensor q8Q;               /* [hiddenDim x hiddenDim] */
    Q8Tensor q8K;               /* [hiddenDim x hiddenDim] */
    Q8Tensor q8V;               /* [hiddenDim x hiddenDim] */
    Q8Tensor q8O;               /* [hiddenDim x hiddenDim] */
    Q8Tensor q8Gate;            /* [ffnDim x hiddenDim] (LLaMA only) */
    Q8Tensor q8Up;              /* [ffnDim x hiddenDim] */
    Q8Tensor q8Down;            /* [hiddenDim x ffnDim] */

    /* Bias terms (always float32, nil if model has no bias) */
    float   *qBias;             /* [hiddenDim] or nil */
    float   *kBias;             /* [hiddenDim] or nil */
    float   *vBias;             /* [hiddenDim] or nil */
    float   *oBias;             /* [hiddenDim] or nil */
    float   *ffnUpBias;         /* [ffnDim] or nil (GPT-2: fc1 bias) */
    float   *ffnDownBias;       /* [hiddenDim] or nil (GPT-2: fc2 bias) */

    /* Norms (always float32) */
    float   *inputNorm;         /* [hiddenDim] (RMSNorm weight or LayerNorm weight) */
    float   *postAttnNorm;      /* [hiddenDim] */
    float   *inputNormBias;     /* [hiddenDim] or nil (LayerNorm bias, nil for RMSNorm) */
    float   *postAttnNormBias;  /* [hiddenDim] or nil */

    /* Disk paging state */
    Boolean resident;           /* true if layer weights are in RAM */
} LayerWeights;

/*------------------------------------------------------------------------------
    Disk Pager Layer Slot - Pre-allocated arena region for paged layers
------------------------------------------------------------------------------*/
typedef struct {
    Ptr     projData;           /* Q8 data for all 7 projections */
    float   *normData;          /* Float data for 2 norms */
    float   *biasData;          /* Float data for QKV bias (nil if no bias) */
    short   loadedLayer;        /* Which layer is in this slot (-1=empty) */
} LayerSlot;

/*------------------------------------------------------------------------------
    Disk Pager - Manages layer paging from disk
------------------------------------------------------------------------------*/
typedef struct {
    short       modelRefNum;    /* Open file handle */
    MemoryTier  tier;

    /* Layer slot pool */
    LayerSlot   slots[kMaxModelLayers];
    short       numSlots;       /* How many slots allocated */
    short       nextSlot;       /* Round-robin slot assignment */

    /* Per-layer disk locations */
    long        layerOffsets[kMaxModelLayers];
    long        layerSize;      /* Bytes per layer on disk */

    /* Model metadata */
    QuantType   quantType;
    long        hiddenDim;
    long        ffnDim;
    long        kvDim;          /* numKVHeads * headDim (for GQA support) */
    Boolean     hasBias;        /* Model has QKV attention bias */
} DiskPager;

/*------------------------------------------------------------------------------
    Engine State
------------------------------------------------------------------------------*/
typedef struct {
    /* Model file */
    FSSpec          modelSpec;      /* Model weight file */
    short           modelRefNum;    /* Open file ref for disk paging */
    Boolean         modelOpen;      /* File currently open */

    /* Configuration */
    ModelConfig     config;         /* Model parameters */
    QuantType       quantType;      /* Shorthand for config.quantType */
    MemoryTier      memTier;        /* Memory tier for paging */

    /* Float32 weight pointers (into arena) */
    float           *embedTokens;   /* [vocabSize x hiddenDim] */
    LayerWeights    layers[kMaxModelLayers];
    float           *finalNorm;     /* [hiddenDim] */
    float           *finalNormBias; /* [hiddenDim] or nil (LayerNorm only) */

    /* Q8 embedding (when quantType == kQuantInt8) */
    Q8Tensor        q8Embed;        /* [vocabSize x hiddenDim] */

    /* Learned positional embeddings (GPT-2 family, nil for RoPE models) */
    float           *posEmbed;      /* [maxSeqLen x hiddenDim] or nil */

    /* Separate LM head (when not tied to embeddings) */
    float           *lmHead;        /* [vocabSize x hiddenDim] or nil */
    Q8Tensor        q8LMHead;       /* Q8 version, or .data=nil if tied */

    /* Activation buffers (into arena, always float32) */
    float           *x;             /* Current hidden state [hiddenDim] */
    float           *xb;            /* Secondary buffer [hiddenDim] */
    float           *q;             /* Query [hiddenDim] */
    float           *k;             /* Key [hiddenDim] */
    float           *v;             /* Value [hiddenDim] */
    float           *att;           /* Attention scores [maxSeqLen] */
    float           *ffnGate;       /* FFN gate output [ffnDim] */
    float           *ffnUp;         /* FFN up output [ffnDim] */
    float           *ffnOut;        /* FFN down output [hiddenDim] */
    float           *logits;        /* Output logits [vocabSize] */

    /* KV cache (always float32) */
    float           *keyCache;      /* [numLayers x kvSeqLen x hiddenDim] */
    float           *valueCache;    /* [numLayers x kvSeqLen x hiddenDim] */
    long            kvSeqLen;       /* KV cache sequence length (tier-dependent) */

    /* Memory management */
    ArenaAllocator  arena;          /* Main arena for weights + activations */

    /* Disk pager */
    DiskPager       *pager;         /* nil if all layers resident */

    /* Layer paging info */
    short           layersInRAM;    /* Layers resident in memory */
    short           layersOnDisk;   /* Layers paged from disk */

    /* Hardware path */
    Boolean         useFPU;         /* Use FPU hardware path */
    Boolean         useAltiVec;     /* Use AltiVec SIMD path (G4+) */

    /* Generation state */
    long            seqLen;         /* Current sequence position */

    /* State */
    Boolean         loaded;         /* Model fully loaded and ready */
    Boolean         generating;     /* Currently generating tokens */
} EngineState;

/*------------------------------------------------------------------------------
    Public API
------------------------------------------------------------------------------*/

/* Initialize engine (detect hardware, allocate arena) */
OSErr Engine_Initialize(void);

/* Set progress callback before calling Engine_Initialize */
void Engine_SetProgressCallback(void);
OSErr Engine_InitializeWithProgress(void);
extern void Engine_ProgressUpdate(long percent, char *message);

/* Load model weights from file */
OSErr Engine_LoadModel(const FSSpec *modelFile);

/* Generate response from input tokens */
long Engine_Generate(const long *inputTokens, long inputLen,
                    long *outputTokens, long maxOutput);

/* Token streaming callback */
extern void (*gEngineTokenCallback)(long, long);

/* Prefill progress callback: (current token index, total input tokens) */
extern void (*gEnginePrefillCallback)(long, long);

/* Check if engine is ready for inference */
Boolean Engine_IsReady(void);

/* Check if currently generating */
Boolean Engine_IsGenerating(void);

/* Stop generation in progress */
void Engine_StopGeneration(void);

/* Get model info string for status display */
void Engine_GetStatusString(char *buffer, short maxLen);

/* Allocate from engine arena (for tokenizer use after model loaded) */
Ptr Engine_ArenaAlloc(long size);

/* Clean up engine (free arena, close files) */
void Engine_Cleanup(void);

/*------------------------------------------------------------------------------
    Disk Pager API (used internally by Engine, also available directly)
------------------------------------------------------------------------------*/

/* Initialize disk pager for layer paging */
DiskPager* DiskPager_Init(ArenaAllocator *arena, short modelRefNum,
                           MemoryTier tier, long weightsOffset,
                           long embedBytes, QuantType quantType,
                           long hiddenDim, long ffnDim,
                           short numLayers);

/* Ensure a layer's weights are in RAM (loads from disk if needed) */
Boolean DiskPager_EnsureResident(DiskPager *pager, short layer,
                                  LayerWeights *lw);

/* Pre-load a range of layers */
Boolean DiskPager_PreloadRange(DiskPager *pager,
                                short startLayer, short endLayer,
                                LayerWeights *layers);

#endif /* INFERENCEENGINE_H */
