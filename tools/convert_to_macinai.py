#!/usr/bin/env python3
"""
convert_to_macinai.py - Universal HuggingFace -> MacinAI .bin converter

Converts ANY decoder-only transformer from HuggingFace to the MacinAI
binary format for the C89 inference engine on vintage Macintosh hardware.

Supported architectures:
  LLaMA family (archType=0): LLaMA, Mistral, Qwen, Gemma, TinyLlama, SmolLM, StableLM
  GPT-2 family (archType=1): GPT-2, OPT, GPT-J, GPT-NeoX, Pythia, Falcon, Phi

Auto-detects architecture by inspecting the model's config and weight names.
Sets appropriate flags for bias, tied embeddings, parallel attn+FFN, etc.

Usage:
    python convert_to_macinai.py --model gpt2
    python convert_to_macinai.py --model meta-llama/Llama-3.2-1B --quantize q8
    python convert_to_macinai.py --model EleutherAI/pythia-160m --quantize q8
    python convert_to_macinai.py --model /path/to/local/model --output my_model.bin

Requires: torch, transformers, numpy

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
"""

import argparse
import json
import os
import platform
import struct
import subprocess
import sys

import numpy as np
import torch
from transformers import AutoModelForCausalLM, AutoTokenizer, AutoConfig

# --- Constants (must match InferenceEngine.h) ---
MAGIC = 0x4D434149  # 'MCAI'
FORMAT_VERSION = 2
HEADER_SIZE = 128   # 32 longs = 128 bytes

QUANT_FLOAT32 = 0
QUANT_INT8 = 1

ARCH_LLAMA = 0
ARCH_GPT2 = 1

FLAG_HAS_ATTN_BIAS = 0x00000001
FLAG_HAS_FFN_BIAS = 0x00000002
FLAG_SEPARATE_LM_HEAD = 0x00000004
FLAG_PARALLEL_ATTN_FFN = 0x00000008

FILE_TYPE = "MCAI"
FILE_CREATOR = "OAS "


# --- Architecture detection ---

# Weight name patterns by architecture family
LLAMA_PATTERNS = [
    "model.layers.{}.self_attn.q_proj.weight",
    "model.layers.{}.mlp.gate_proj.weight",
    "model.layers.{}.input_layernorm.weight",
]

GPT2_PATTERNS = [
    "transformer.h.{}.attn.c_attn.weight",     # GPT-2
    "gpt_neox.layers.{}.attention.query_key_value.weight",  # Pythia/NeoX
    "transformer.h.{}.attn.attention.q_proj.weight",  # Falcon
    "model.decoder.layers.{}.self_attn.q_proj.weight",  # OPT
]


def detect_architecture(config, state_dict):
    """Auto-detect model architecture family and build weight mapping.

    Returns (arch_type, flags, weight_map) where weight_map maps
    canonical names to actual state_dict keys.
    """
    model_type = getattr(config, "model_type", "").lower()
    keys = set(state_dict.keys())
    flags = 0

    # Check for tied vs separate lm_head
    has_lm_head = any("lm_head" in k for k in keys)
    tie_embeds = getattr(config, "tie_word_embeddings", True)
    if has_lm_head and not tie_embeds:
        flags |= FLAG_SEPARATE_LM_HEAD

    # --- LLaMA family ---
    if model_type in ("llama", "mistral", "qwen2", "gemma", "gemma2",
                       "stablelm", "phi3", "internlm2"):
        arch = ARCH_LLAMA
        wmap = build_llama_weight_map(config, state_dict)

        # Check for bias (Qwen2 has bias on QKV)
        if any(".q_proj.bias" in k for k in keys):
            flags |= FLAG_HAS_ATTN_BIAS
        if any(".gate_proj.bias" in k for k in keys):
            flags |= FLAG_HAS_FFN_BIAS

        return arch, flags, wmap

    # --- GPT-2 ---
    if model_type == "gpt2":
        arch = ARCH_GPT2
        flags |= FLAG_HAS_ATTN_BIAS | FLAG_HAS_FFN_BIAS
        wmap = build_gpt2_weight_map(config, state_dict)
        return arch, flags, wmap

    # --- GPT-NeoX / Pythia ---
    if model_type in ("gpt_neox",):
        arch = ARCH_GPT2  # Uses LayerNorm + GeLU
        wmap = build_neox_weight_map(config, state_dict)
        if any(".dense.bias" in k for k in keys):
            flags |= FLAG_HAS_ATTN_BIAS
        if any(".dense_h_to_4h.bias" in k for k in keys):
            flags |= FLAG_HAS_FFN_BIAS
        if getattr(config, "use_parallel_residual", True):
            flags |= FLAG_PARALLEL_ATTN_FFN
        return arch, flags, wmap

    # --- OPT ---
    if model_type == "opt":
        arch = ARCH_GPT2
        flags |= FLAG_HAS_ATTN_BIAS | FLAG_HAS_FFN_BIAS
        wmap = build_opt_weight_map(config, state_dict)
        return arch, flags, wmap

    # --- GPT-J ---
    if model_type == "gptj":
        arch = ARCH_GPT2
        flags |= FLAG_PARALLEL_ATTN_FFN
        wmap = build_gptj_weight_map(config, state_dict)
        if any(".attn.q_proj.bias" in k or ".attn.out_proj.bias" in k for k in keys):
            flags |= FLAG_HAS_ATTN_BIAS
        if any(".mlp.fc_in.bias" in k for k in keys):
            flags |= FLAG_HAS_FFN_BIAS
        return arch, flags, wmap

    # --- Falcon ---
    if model_type in ("falcon", "RefinedWeb", "RefinedWebModel"):
        arch = ARCH_GPT2
        wmap = build_falcon_weight_map(config, state_dict)
        if any(".dense.bias" in k for k in keys):
            flags |= FLAG_HAS_ATTN_BIAS
        if any(".dense_h_to_4h.bias" in k for k in keys):
            flags |= FLAG_HAS_FFN_BIAS
        return arch, flags, wmap

    # --- Phi-1/2 ---
    if model_type in ("phi", "phi-msft"):
        arch = ARCH_GPT2
        flags |= FLAG_HAS_ATTN_BIAS | FLAG_HAS_FFN_BIAS
        flags |= FLAG_PARALLEL_ATTN_FFN
        wmap = build_phi_weight_map(config, state_dict)
        return arch, flags, wmap

    # --- Fallback: try to auto-detect from weight names ---
    print(f"WARNING: Unknown model_type '{model_type}', attempting auto-detection...")

    # Check for LLaMA-style names
    if any("model.layers.0.self_attn.q_proj.weight" in k for k in keys):
        if any("mlp.gate_proj" in k for k in keys):
            print("  Detected: LLaMA-style (gate_proj found)")
            return ARCH_LLAMA, flags, build_llama_weight_map(config, state_dict)
        else:
            print("  Detected: LLaMA-style (2-matrix FFN)")
            return ARCH_GPT2, flags, build_llama_weight_map(config, state_dict)

    # Check for GPT-2-style names
    if any("transformer.h.0.attn" in k for k in keys):
        print("  Detected: GPT-2-style")
        flags |= FLAG_HAS_ATTN_BIAS | FLAG_HAS_FFN_BIAS
        return ARCH_GPT2, flags, build_gpt2_weight_map(config, state_dict)

    raise ValueError(
        f"Cannot detect architecture for model_type='{model_type}'. "
        f"Known types: llama, mistral, qwen2, gemma, gpt2, gpt_neox, opt, gptj, falcon, phi"
    )


# --- Weight map builders ---
# Each returns a dict mapping canonical names to state_dict keys.
# Canonical names:
#   embed, pos_embed, lm_head,
#   layer.{i}.q_proj, .k_proj, .v_proj, .o_proj,
#   layer.{i}.gate_proj (LLaMA only), .up_proj, .down_proj,
#   layer.{i}.input_norm, .input_norm_bias,
#   layer.{i}.post_norm, .post_norm_bias,
#   layer.{i}.q_bias, .k_bias, .v_bias, .o_bias,
#   layer.{i}.up_bias, .down_bias,
#   final_norm, final_norm_bias

def build_llama_weight_map(config, state_dict):
    """LLaMA / Mistral / Qwen / Gemma / TinyLlama / SmolLM."""
    wmap = {}
    n = config.num_hidden_layers

    wmap["embed"] = "model.embed_tokens.weight"
    wmap["final_norm"] = "model.norm.weight"

    if "lm_head.weight" in state_dict:
        wmap["lm_head"] = "lm_head.weight"

    for i in range(n):
        p = f"model.layers.{i}"
        wmap[f"layer.{i}.q_proj"] = f"{p}.self_attn.q_proj.weight"
        wmap[f"layer.{i}.k_proj"] = f"{p}.self_attn.k_proj.weight"
        wmap[f"layer.{i}.v_proj"] = f"{p}.self_attn.v_proj.weight"
        wmap[f"layer.{i}.o_proj"] = f"{p}.self_attn.o_proj.weight"
        wmap[f"layer.{i}.input_norm"] = f"{p}.input_layernorm.weight"
        wmap[f"layer.{i}.post_norm"] = f"{p}.post_attention_layernorm.weight"

        # gate_proj only for SwiGLU models
        gate_key = f"{p}.mlp.gate_proj.weight"
        if gate_key in state_dict:
            wmap[f"layer.{i}.gate_proj"] = gate_key

        wmap[f"layer.{i}.up_proj"] = f"{p}.mlp.up_proj.weight"
        wmap[f"layer.{i}.down_proj"] = f"{p}.mlp.down_proj.weight"

        # Optional bias terms (Qwen2 has these)
        for proj in ["q_proj", "k_proj", "v_proj", "o_proj"]:
            bias_key = f"{p}.self_attn.{proj}.bias"
            if bias_key in state_dict:
                wmap[f"layer.{i}.{proj.replace('_proj', '')}_bias"] = bias_key

        for proj, canon in [("gate_proj", "gate_bias"), ("up_proj", "up_bias"),
                            ("down_proj", "down_bias")]:
            bias_key = f"{p}.mlp.{proj}.bias"
            if bias_key in state_dict:
                wmap[f"layer.{i}.{canon}"] = bias_key

    return wmap


def build_gpt2_weight_map(config, state_dict):
    """GPT-2 (fused QKV via c_attn, Conv1D transposed weights)."""
    wmap = {}
    n = config.num_hidden_layers if hasattr(config, "num_hidden_layers") else config.n_layer

    wmap["embed"] = "transformer.wte.weight"
    wmap["pos_embed"] = "transformer.wpe.weight"
    wmap["final_norm"] = "transformer.ln_f.weight"
    wmap["final_norm_bias"] = "transformer.ln_f.bias"

    if "lm_head.weight" in state_dict:
        wmap["lm_head"] = "lm_head.weight"

    for i in range(n):
        p = f"transformer.h.{i}"
        # GPT-2 uses fused c_attn, will be split during export
        wmap[f"layer.{i}.c_attn"] = f"{p}.attn.c_attn.weight"
        wmap[f"layer.{i}.c_attn_bias"] = f"{p}.attn.c_attn.bias"
        wmap[f"layer.{i}.o_proj"] = f"{p}.attn.c_proj.weight"
        wmap[f"layer.{i}.o_bias"] = f"{p}.attn.c_proj.bias"

        wmap[f"layer.{i}.up_proj"] = f"{p}.mlp.c_fc.weight"
        wmap[f"layer.{i}.up_bias"] = f"{p}.mlp.c_fc.bias"
        wmap[f"layer.{i}.down_proj"] = f"{p}.mlp.c_proj.weight"
        wmap[f"layer.{i}.down_bias"] = f"{p}.mlp.c_proj.bias"

        wmap[f"layer.{i}.input_norm"] = f"{p}.ln_1.weight"
        wmap[f"layer.{i}.input_norm_bias"] = f"{p}.ln_1.bias"
        wmap[f"layer.{i}.post_norm"] = f"{p}.ln_2.weight"
        wmap[f"layer.{i}.post_norm_bias"] = f"{p}.ln_2.bias"

    return wmap


def build_neox_weight_map(config, state_dict):
    """GPT-NeoX / Pythia (fused QKV via query_key_value)."""
    wmap = {}
    n = config.num_hidden_layers

    wmap["embed"] = "gpt_neox.embed_in.weight"
    wmap["final_norm"] = "gpt_neox.final_layer_norm.weight"
    wmap["final_norm_bias"] = "gpt_neox.final_layer_norm.bias"

    if "embed_out.weight" in state_dict:
        wmap["lm_head"] = "embed_out.weight"

    for i in range(n):
        p = f"gpt_neox.layers.{i}"
        # Fused QKV
        wmap[f"layer.{i}.qkv_fused"] = f"{p}.attention.query_key_value.weight"
        if f"{p}.attention.query_key_value.bias" in state_dict:
            wmap[f"layer.{i}.qkv_fused_bias"] = f"{p}.attention.query_key_value.bias"
        wmap[f"layer.{i}.o_proj"] = f"{p}.attention.dense.weight"
        if f"{p}.attention.dense.bias" in state_dict:
            wmap[f"layer.{i}.o_bias"] = f"{p}.attention.dense.bias"

        wmap[f"layer.{i}.up_proj"] = f"{p}.mlp.dense_h_to_4h.weight"
        wmap[f"layer.{i}.down_proj"] = f"{p}.mlp.dense_4h_to_h.weight"
        if f"{p}.mlp.dense_h_to_4h.bias" in state_dict:
            wmap[f"layer.{i}.up_bias"] = f"{p}.mlp.dense_h_to_4h.bias"
        if f"{p}.mlp.dense_4h_to_h.bias" in state_dict:
            wmap[f"layer.{i}.down_bias"] = f"{p}.mlp.dense_4h_to_h.bias"

        wmap[f"layer.{i}.input_norm"] = f"{p}.input_layernorm.weight"
        wmap[f"layer.{i}.input_norm_bias"] = f"{p}.input_layernorm.bias"
        wmap[f"layer.{i}.post_norm"] = f"{p}.post_attention_layernorm.weight"
        wmap[f"layer.{i}.post_norm_bias"] = f"{p}.post_attention_layernorm.bias"

    return wmap


def build_opt_weight_map(config, state_dict):
    """OPT."""
    wmap = {}
    n = config.num_hidden_layers

    wmap["embed"] = "model.decoder.embed_tokens.weight"
    # OPT has learned pos embed with offset=2
    if "model.decoder.embed_positions.weight" in state_dict:
        wmap["pos_embed"] = "model.decoder.embed_positions.weight"
    wmap["final_norm"] = "model.decoder.final_layer_norm.weight"
    wmap["final_norm_bias"] = "model.decoder.final_layer_norm.bias"

    if "lm_head.weight" in state_dict:
        wmap["lm_head"] = "lm_head.weight"

    for i in range(n):
        p = f"model.decoder.layers.{i}"
        wmap[f"layer.{i}.q_proj"] = f"{p}.self_attn.q_proj.weight"
        wmap[f"layer.{i}.k_proj"] = f"{p}.self_attn.k_proj.weight"
        wmap[f"layer.{i}.v_proj"] = f"{p}.self_attn.v_proj.weight"
        wmap[f"layer.{i}.o_proj"] = f"{p}.self_attn.out_proj.weight"

        for proj, canon in [("q_proj", "q_bias"), ("k_proj", "k_bias"),
                            ("v_proj", "v_bias")]:
            bk = f"{p}.self_attn.{proj}.bias"
            if bk in state_dict:
                wmap[f"layer.{i}.{canon}"] = bk
        bk = f"{p}.self_attn.out_proj.bias"
        if bk in state_dict:
            wmap[f"layer.{i}.o_bias"] = bk

        wmap[f"layer.{i}.up_proj"] = f"{p}.fc1.weight"
        wmap[f"layer.{i}.down_proj"] = f"{p}.fc2.weight"
        if f"{p}.fc1.bias" in state_dict:
            wmap[f"layer.{i}.up_bias"] = f"{p}.fc1.bias"
        if f"{p}.fc2.bias" in state_dict:
            wmap[f"layer.{i}.down_bias"] = f"{p}.fc2.bias"

        wmap[f"layer.{i}.input_norm"] = f"{p}.self_attn_layer_norm.weight"
        wmap[f"layer.{i}.input_norm_bias"] = f"{p}.self_attn_layer_norm.bias"
        wmap[f"layer.{i}.post_norm"] = f"{p}.final_layer_norm.weight"
        wmap[f"layer.{i}.post_norm_bias"] = f"{p}.final_layer_norm.bias"

    return wmap


def build_gptj_weight_map(config, state_dict):
    """GPT-J (parallel attn+FFN, rotary)."""
    wmap = {}
    n = config.num_hidden_layers if hasattr(config, "num_hidden_layers") else config.n_layer

    wmap["embed"] = "transformer.wte.weight"
    wmap["final_norm"] = "transformer.ln_f.weight"
    wmap["final_norm_bias"] = "transformer.ln_f.bias"

    if "lm_head.weight" in state_dict:
        wmap["lm_head"] = "lm_head.weight"
    if "lm_head.bias" in state_dict:
        wmap["lm_head_bias"] = "lm_head.bias"

    for i in range(n):
        p = f"transformer.h.{i}"
        wmap[f"layer.{i}.q_proj"] = f"{p}.attn.q_proj.weight"
        wmap[f"layer.{i}.k_proj"] = f"{p}.attn.k_proj.weight"
        wmap[f"layer.{i}.v_proj"] = f"{p}.attn.v_proj.weight"
        wmap[f"layer.{i}.o_proj"] = f"{p}.attn.out_proj.weight"

        wmap[f"layer.{i}.up_proj"] = f"{p}.mlp.fc_in.weight"
        wmap[f"layer.{i}.down_proj"] = f"{p}.mlp.fc_out.weight"
        if f"{p}.mlp.fc_in.bias" in state_dict:
            wmap[f"layer.{i}.up_bias"] = f"{p}.mlp.fc_in.bias"
        if f"{p}.mlp.fc_out.bias" in state_dict:
            wmap[f"layer.{i}.down_bias"] = f"{p}.mlp.fc_out.bias"

        wmap[f"layer.{i}.input_norm"] = f"{p}.ln_1.weight"
        wmap[f"layer.{i}.input_norm_bias"] = f"{p}.ln_1.bias"
        # GPT-J only has one layernorm (parallel), reuse input_norm
        wmap[f"layer.{i}.post_norm"] = f"{p}.ln_1.weight"
        wmap[f"layer.{i}.post_norm_bias"] = f"{p}.ln_1.bias"

    return wmap


def build_falcon_weight_map(config, state_dict):
    """Falcon."""
    wmap = {}
    n = config.num_hidden_layers

    wmap["embed"] = "transformer.word_embeddings.weight"
    wmap["final_norm"] = "transformer.ln_f.weight"
    wmap["final_norm_bias"] = "transformer.ln_f.bias"

    if "lm_head.weight" in state_dict:
        wmap["lm_head"] = "lm_head.weight"

    for i in range(n):
        p = f"transformer.h.{i}"
        # Falcon may have fused QKV
        fused_key = f"{p}.self_attention.query_key_value.weight"
        if fused_key in state_dict:
            wmap[f"layer.{i}.qkv_fused"] = fused_key
            if f"{p}.self_attention.query_key_value.bias" in state_dict:
                wmap[f"layer.{i}.qkv_fused_bias"] = f"{p}.self_attention.query_key_value.bias"
        else:
            wmap[f"layer.{i}.q_proj"] = f"{p}.self_attention.q_proj.weight"
            wmap[f"layer.{i}.k_proj"] = f"{p}.self_attention.k_proj.weight"
            wmap[f"layer.{i}.v_proj"] = f"{p}.self_attention.v_proj.weight"

        wmap[f"layer.{i}.o_proj"] = f"{p}.self_attention.dense.weight"
        wmap[f"layer.{i}.up_proj"] = f"{p}.mlp.dense_h_to_4h.weight"
        wmap[f"layer.{i}.down_proj"] = f"{p}.mlp.dense_4h_to_h.weight"

        wmap[f"layer.{i}.input_norm"] = f"{p}.input_layernorm.weight"
        wmap[f"layer.{i}.input_norm_bias"] = f"{p}.input_layernorm.bias"
        # Falcon may use single LN
        post_key = f"{p}.post_attention_layernorm.weight"
        if post_key in state_dict:
            wmap[f"layer.{i}.post_norm"] = post_key
            wmap[f"layer.{i}.post_norm_bias"] = f"{p}.post_attention_layernorm.bias"
        else:
            wmap[f"layer.{i}.post_norm"] = f"{p}.ln_attn.weight"
            wmap[f"layer.{i}.post_norm_bias"] = f"{p}.ln_attn.bias"

    return wmap


def build_phi_weight_map(config, state_dict):
    """Phi-1 / Phi-2 (parallel attn+FFN)."""
    wmap = {}
    n = config.num_hidden_layers

    wmap["embed"] = "transformer.embd.wte.weight"
    wmap["final_norm"] = "lm_head.ln.weight"
    wmap["final_norm_bias"] = "lm_head.ln.bias"

    if "lm_head.linear.weight" in state_dict:
        wmap["lm_head"] = "lm_head.linear.weight"
    if "lm_head.linear.bias" in state_dict:
        wmap["lm_head_bias"] = "lm_head.linear.bias"

    for i in range(n):
        p = f"transformer.h.{i}"
        # Phi uses fused QKV
        wmap[f"layer.{i}.c_attn"] = f"{p}.mixer.Wqkv.weight"
        if f"{p}.mixer.Wqkv.bias" in state_dict:
            wmap[f"layer.{i}.c_attn_bias"] = f"{p}.mixer.Wqkv.bias"
        wmap[f"layer.{i}.o_proj"] = f"{p}.mixer.out_proj.weight"
        if f"{p}.mixer.out_proj.bias" in state_dict:
            wmap[f"layer.{i}.o_bias"] = f"{p}.mixer.out_proj.bias"

        wmap[f"layer.{i}.up_proj"] = f"{p}.mlp.fc1.weight"
        wmap[f"layer.{i}.down_proj"] = f"{p}.mlp.fc2.weight"
        if f"{p}.mlp.fc1.bias" in state_dict:
            wmap[f"layer.{i}.up_bias"] = f"{p}.mlp.fc1.bias"
        if f"{p}.mlp.fc2.bias" in state_dict:
            wmap[f"layer.{i}.down_bias"] = f"{p}.mlp.fc2.bias"

        wmap[f"layer.{i}.input_norm"] = f"{p}.ln.weight"
        wmap[f"layer.{i}.input_norm_bias"] = f"{p}.ln.bias"
        # Phi parallel, reuse same norm
        wmap[f"layer.{i}.post_norm"] = f"{p}.ln.weight"
        wmap[f"layer.{i}.post_norm_bias"] = f"{p}.ln.bias"

    return wmap


# --- Binary writing helpers ---

def write_be_long(f, value):
    """Write 32-bit signed integer, big-endian."""
    f.write(struct.pack(">i", int(value)))


def write_be_float_tensor(f, tensor):
    """Write float32 tensor, big-endian. Returns bytes written."""
    data = tensor.detach().float().cpu().numpy().flatten()
    packed = struct.pack(">" + "f" * len(data), *data)
    f.write(packed)
    return len(data) * 4


def quantize_q8(tensor):
    """Q8_0: per-tensor scale + int8. Returns (scale, int8_array)."""
    data = tensor.detach().float().cpu().numpy().flatten()
    max_abs = float(np.max(np.abs(data)))
    if max_abs == 0:
        max_abs = 1e-10
    scale = max_abs / 127.0
    q = np.round(data / scale).clip(-128, 127).astype(np.int8)
    return scale, q


def write_q8_tensor(f, tensor):
    """Write [4-byte BE scale][int8 data]. Returns bytes written."""
    scale, q = quantize_q8(tensor)
    f.write(struct.pack(">f", scale))
    f.write(q.tobytes())
    return 4 + len(q)


def write_weight(f, tensor, is_q8, is_norm=False):
    """Write a weight tensor. Norms always float32."""
    if is_norm or not is_q8:
        return write_be_float_tensor(f, tensor)
    else:
        return write_q8_tensor(f, tensor)


def split_fused_qkv(tensor, hidden_dim, num_heads, num_kv_heads=None):
    """Split a fused QKV weight [3*H, H] into separate Q, K, V tensors.

    Handles both equal-head (GPT-2, Pythia) and GQA variants.
    For GPT-2's Conv1D, tensor may be [H, 3*H] (transposed).
    """
    if num_kv_heads is None:
        num_kv_heads = num_heads

    head_dim = hidden_dim // num_heads
    kv_dim = num_kv_heads * head_dim

    # Detect GPT-2 Conv1D format: weight is [in, out] not [out, in]
    if tensor.shape[0] == hidden_dim and tensor.shape[1] == hidden_dim + 2 * kv_dim:
        tensor = tensor.t()  # Transpose to [out, in]

    q = tensor[:hidden_dim, :]
    k = tensor[hidden_dim:hidden_dim + kv_dim, :]
    v = tensor[hidden_dim + kv_dim:hidden_dim + 2 * kv_dim, :]
    return q, k, v


def split_fused_qkv_bias(bias, hidden_dim, num_heads, num_kv_heads=None):
    """Split fused QKV bias [3*H] into Q, K, V."""
    if num_kv_heads is None:
        num_kv_heads = num_heads
    head_dim = hidden_dim // num_heads
    kv_dim = num_kv_heads * head_dim

    q = bias[:hidden_dim]
    k = bias[hidden_dim:hidden_dim + kv_dim]
    v = bias[hidden_dim + kv_dim:hidden_dim + 2 * kv_dim]
    return q, k, v


def maybe_transpose(tensor, state_dict_key, model_type):
    """GPT-2 uses Conv1D with [in, out] layout, transpose to [out, in]."""
    if model_type == "gpt2" and len(tensor.shape) == 2:
        # GPT-2 Conv1D stores [in_features, out_features]
        # We need [out_features, in_features] for mat-vec multiply
        if "c_attn" in state_dict_key or "c_proj" in state_dict_key or "c_fc" in state_dict_key:
            return tensor.t()
    return tensor


def write_vocab_section(f, tokenizer_dir_or_name):
    """Write vocab section. Returns (bytes_written, vocab_size, num_merges)."""
    try:
        tokenizer = AutoTokenizer.from_pretrained(tokenizer_dir_or_name)
    except Exception as e:
        print(f"  WARNING: Could not load tokenizer: {e}")
        print("  Writing empty vocab section (model will need external tokenizer)")
        return 0, 0, 0

    vocab = tokenizer.get_vocab()
    id_to_token = {v: k for k, v in vocab.items()}
    vocab_size = len(vocab)
    bytes_written = 0

    for tid in range(vocab_size):
        token_str = id_to_token.get(tid, "")
        token_bytes = token_str.encode("utf-8", errors="replace")[:255]
        f.write(struct.pack("B", len(token_bytes)))
        f.write(token_bytes)
        bytes_written += 1 + len(token_bytes)

    # Load merges
    tokenizer_json = os.path.join(
        tokenizer_dir_or_name if os.path.isdir(tokenizer_dir_or_name)
        else "", "tokenizer.json"
    )
    num_merges = 0

    # Try to get merges from the tokenizer object
    try:
        if hasattr(tokenizer, 'backend_tokenizer'):
            model = tokenizer.backend_tokenizer.model
            if hasattr(model, 'get_merges'):
                merges = model.get_merges()
            else:
                merges = []
        elif os.path.exists(tokenizer_json):
            with open(tokenizer_json) as tj:
                tok_data = json.load(tj)
            merges = tok_data.get("model", {}).get("merges", [])
        else:
            merges = []
    except Exception:
        merges = []

    num_merges = len(merges)
    for merge in merges:
        if isinstance(merge, (list, tuple)):
            parts = list(merge)
        else:
            parts = str(merge).split(" ", 1)
        if len(parts) != 2:
            f.write(b"\x00\x00")
            bytes_written += 2
            continue
        for part in parts:
            pb = str(part).encode("utf-8", errors="replace")[:255]
            f.write(struct.pack("B", len(pb)))
            f.write(pb)
            bytes_written += 1 + len(pb)

    return bytes_written, vocab_size, num_merges


def set_mac_type_creator(filepath):
    """Set Mac file type/creator codes."""
    if platform.system() == "Darwin":
        try:
            subprocess.run(
                ["SetFile", "-t", FILE_TYPE, "-c", FILE_CREATOR, filepath],
                check=True, capture_output=True,
            )
            print(f"  File type set: type='{FILE_TYPE}' creator='{FILE_CREATOR}'")
            return
        except (FileNotFoundError, subprocess.CalledProcessError):
            pass
    print(f"  NOTE: After transferring to Mac, run:")
    print(f"    SetFile -t {FILE_TYPE} -c '{FILE_CREATOR}' {os.path.basename(filepath)}")


# --- Main export ---

def convert(model_name, output_path, quantize="q8"):
    """Convert any HuggingFace model to MacinAI .bin format."""

    print(f"Loading model: {model_name}")
    config = AutoConfig.from_pretrained(model_name, trust_remote_code=True)
    model = AutoModelForCausalLM.from_pretrained(
        model_name, torch_dtype=torch.float32, trust_remote_code=True,
    )
    model.eval()
    state_dict = model.state_dict()

    model_type = getattr(config, "model_type", "unknown")
    print(f"  Model type: {model_type}")

    # Detect architecture
    arch_type, flags, wmap = detect_architecture(config, state_dict)
    print(f"  Architecture: {'LLaMA' if arch_type == ARCH_LLAMA else 'GPT-2'}")
    print(f"  Flags: 0x{flags:08x}")
    if flags & FLAG_HAS_ATTN_BIAS:
        print("    - Has attention bias")
    if flags & FLAG_HAS_FFN_BIAS:
        print("    - Has FFN bias")
    if flags & FLAG_SEPARATE_LM_HEAD:
        print("    - Separate LM head (not tied)")
    if flags & FLAG_PARALLEL_ATTN_FFN:
        print("    - Parallel attention+FFN")

    # Extract dimensions
    num_layers = config.num_hidden_layers
    hidden_dim = config.hidden_size
    num_heads = config.num_attention_heads
    num_kv_heads = getattr(config, "num_key_value_heads", num_heads)
    head_dim = hidden_dim // num_heads
    kv_dim = num_kv_heads * head_dim
    ffn_dim = config.intermediate_size
    vocab_size = config.vocab_size
    max_seq_len = getattr(config, "max_position_embeddings", 1024)
    rope_theta = int(getattr(config, "rope_theta", 10000)) if arch_type == ARCH_LLAMA else 0
    total_params = sum(p.numel() for p in model.parameters())

    is_q8 = (quantize == "q8")
    quant_type = QUANT_INT8 if is_q8 else QUANT_FLOAT32

    print(f"\n  Layers: {num_layers}, Hidden: {hidden_dim}, Heads: {num_heads}/{num_kv_heads}")
    print(f"  FFN: {ffn_dim}, Vocab: {vocab_size}, MaxSeq: {max_seq_len}")
    print(f"  Params: {total_params:,}, Quantize: {'Q8' if is_q8 else 'F32'}")

    if num_layers > 80:
        print(f"  WARNING: {num_layers} layers exceeds kMaxModelLayers=80")

    # Helper to get a tensor from weight map
    def get_weight(canonical_name):
        key = wmap.get(canonical_name)
        if key is None or key not in state_dict:
            return None
        t = state_dict[key]
        return maybe_transpose(t, key, model_type)

    # --- Write .bin file ---
    print(f"\nWriting {output_path}...")

    with open(output_path, "wb") as f:
        # Placeholder header
        f.write(b"\x00" * HEADER_SIZE)

        # Vocab section
        vocab_offset = HEADER_SIZE
        print("  Writing vocab...")
        vocab_bytes, actual_vocab, num_merges = write_vocab_section(f, model_name)
        if actual_vocab and actual_vocab != vocab_size:
            print(f"  WARNING: tokenizer vocab ({actual_vocab}) != model vocab ({vocab_size})")
        print(f"  Vocab: {vocab_bytes:,} bytes, {num_merges} merges")

        # Weights section
        weights_offset = HEADER_SIZE + vocab_bytes
        total_weight_bytes = 0

        # 1. Token embeddings
        print("  Writing embeddings...")
        embed = get_weight("embed")
        if embed is not None:
            total_weight_bytes += write_weight(f, embed, is_q8)

        # 2. Position embeddings (GPT-2 family only)
        if arch_type == ARCH_GPT2:
            pos_embed = get_weight("pos_embed")
            if pos_embed is not None:
                print(f"  Writing position embeddings [{pos_embed.shape}]...")
                total_weight_bytes += write_be_float_tensor(f, pos_embed)

        # 3. Layer weights
        for i in range(num_layers):
            if i % 10 == 0:
                print(f"  Writing layer {i}/{num_layers}...")

            # Handle fused QKV (GPT-2, Pythia, Falcon)
            fused_qkv = get_weight(f"layer.{i}.c_attn") or get_weight(f"layer.{i}.qkv_fused")
            if fused_qkv is not None:
                q, k, v = split_fused_qkv(fused_qkv, hidden_dim, num_heads, num_kv_heads)
                total_weight_bytes += write_weight(f, q, is_q8)
                total_weight_bytes += write_weight(f, k, is_q8)
                total_weight_bytes += write_weight(f, v, is_q8)
            else:
                # Separate Q, K, V projections
                for proj in ["q_proj", "k_proj", "v_proj"]:
                    w = get_weight(f"layer.{i}.{proj}")
                    if w is not None:
                        total_weight_bytes += write_weight(f, w, is_q8)

            # O projection
            o = get_weight(f"layer.{i}.o_proj")
            if o is not None:
                total_weight_bytes += write_weight(f, maybe_transpose(
                    state_dict.get(wmap.get(f"layer.{i}.o_proj", ""), torch.zeros(1)),
                    wmap.get(f"layer.{i}.o_proj", ""), model_type
                ) if o is None else o, is_q8)

            # FFN projections
            if arch_type == ARCH_LLAMA:
                # gate_proj (SwiGLU only)
                gate = get_weight(f"layer.{i}.gate_proj")
                if gate is not None:
                    total_weight_bytes += write_weight(f, gate, is_q8)

            up = get_weight(f"layer.{i}.up_proj")
            if up is not None:
                total_weight_bytes += write_weight(f, up, is_q8)

            down = get_weight(f"layer.{i}.down_proj")
            if down is not None:
                total_weight_bytes += write_weight(f, down, is_q8)

            # Norm weights (always f32)
            inp_norm = get_weight(f"layer.{i}.input_norm")
            if inp_norm is not None:
                total_weight_bytes += write_be_float_tensor(f, inp_norm)

            post_norm = get_weight(f"layer.{i}.post_norm")
            if post_norm is not None:
                total_weight_bytes += write_be_float_tensor(f, post_norm)

            # Norm biases (GPT-2 family)
            if arch_type == ARCH_GPT2:
                inp_nb = get_weight(f"layer.{i}.input_norm_bias")
                if inp_nb is not None:
                    total_weight_bytes += write_be_float_tensor(f, inp_nb)
                post_nb = get_weight(f"layer.{i}.post_norm_bias")
                if post_nb is not None:
                    total_weight_bytes += write_be_float_tensor(f, post_nb)

            # Attention bias terms
            if flags & FLAG_HAS_ATTN_BIAS:
                # Handle fused QKV bias
                fused_bias = get_weight(f"layer.{i}.c_attn_bias") or get_weight(f"layer.{i}.qkv_fused_bias")
                if fused_bias is not None:
                    qb, kb, vb = split_fused_qkv_bias(fused_bias, hidden_dim, num_heads, num_kv_heads)
                    total_weight_bytes += write_be_float_tensor(f, qb)
                    total_weight_bytes += write_be_float_tensor(f, kb)
                    total_weight_bytes += write_be_float_tensor(f, vb)
                else:
                    for bn in ["q_bias", "k_bias", "v_bias"]:
                        b = get_weight(f"layer.{i}.{bn}")
                        if b is not None:
                            total_weight_bytes += write_be_float_tensor(f, b)

                ob = get_weight(f"layer.{i}.o_bias")
                if ob is not None:
                    total_weight_bytes += write_be_float_tensor(f, ob)

            # FFN bias terms
            if flags & FLAG_HAS_FFN_BIAS:
                ub = get_weight(f"layer.{i}.up_bias")
                if ub is not None:
                    total_weight_bytes += write_be_float_tensor(f, ub)
                db = get_weight(f"layer.{i}.down_bias")
                if db is not None:
                    total_weight_bytes += write_be_float_tensor(f, db)

        # 4. Final norm
        print("  Writing final norm...")
        fn = get_weight("final_norm")
        if fn is not None:
            total_weight_bytes += write_be_float_tensor(f, fn)
        fnb = get_weight("final_norm_bias")
        if fnb is not None:
            total_weight_bytes += write_be_float_tensor(f, fnb)

        # 5. Separate LM head (if not tied)
        if flags & FLAG_SEPARATE_LM_HEAD:
            lm = get_weight("lm_head")
            if lm is not None:
                print("  Writing separate LM head...")
                total_weight_bytes += write_weight(f, lm, is_q8)

        file_size = f.tell()

        # Rewrite header with correct values
        f.seek(0)
        write_be_long(f, MAGIC)
        write_be_long(f, FORMAT_VERSION)
        write_be_long(f, num_layers)
        write_be_long(f, hidden_dim)
        write_be_long(f, num_heads)
        write_be_long(f, num_kv_heads)
        write_be_long(f, head_dim)
        write_be_long(f, ffn_dim)
        write_be_long(f, vocab_size)
        write_be_long(f, max_seq_len)
        write_be_long(f, rope_theta)
        write_be_long(f, quant_type)
        write_be_long(f, total_params)
        write_be_long(f, file_size)
        write_be_long(f, vocab_offset)
        write_be_long(f, weights_offset)
        write_be_long(f, num_merges)
        write_be_long(f, arch_type)
        write_be_long(f, flags)
        # Remaining reserved fields (13 longs)
        for _ in range(13):
            write_be_long(f, 0)

    # Summary
    file_mb = file_size / (1024 * 1024)
    print(f"\nDone! {output_path}")
    print(f"  Size: {file_mb:.1f} MB ({file_size:,} bytes)")
    print(f"  Header: {HEADER_SIZE} bytes")
    print(f"  Vocab: {vocab_bytes:,} bytes")
    print(f"  Weights: {total_weight_bytes:,} bytes")
    print(f"  Architecture: {'LLaMA' if arch_type == ARCH_LLAMA else 'GPT-2'}")
    print(f"  Quantization: {'Q8_0' if is_q8 else 'Float32'}")

    set_mac_type_creator(output_path)

    return output_path


def main():
    parser = argparse.ArgumentParser(
        description="Convert any HuggingFace model to MacinAI .bin format"
    )
    parser.add_argument(
        "--model", required=True,
        help="HuggingFace model name or local path (e.g. gpt2, meta-llama/Llama-3.2-1B)"
    )
    parser.add_argument(
        "--output", "-o", default=None,
        help="Output .bin file path (default: auto-generated from model name)"
    )
    parser.add_argument(
        "--quantize", "-q", choices=["f32", "q8"], default="q8",
        help="Quantization: f32 (float32) or q8 (int8, default)"
    )

    args = parser.parse_args()

    if args.output is None:
        safe_name = args.model.replace("/", "_").replace("\\", "_")
        q_suffix = "Q8" if args.quantize == "q8" else "F32"
        args.output = f"macinai_{safe_name}_{q_suffix}.bin"

    convert(args.model, args.output, args.quantize)


if __name__ == "__main__":
    main()
