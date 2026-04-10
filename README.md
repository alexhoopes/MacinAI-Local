# MacinAI Local

![MacinAI Local running on a 2002 PowerBook G4 Titanium under Mac OS 9.2.2](screenshots/01_chat.png)

![MacinAI showing the generated AppleScript in a confirmation dialog before running it](screenshots/02_confirmation.png)

![Budget spreadsheet created in AppleWorks 6 by the AppleScript MacinAI generated](screenshots/03_result.png)

An LLM inference engine for classic Mac OS, written in C89.

This runs HuggingFace-compatible transformer models natively on a Macintosh, with no internet connection, no relay server, and no cloud. It targets System 7.5.3 through Mac OS 9.2.2 on 68030, 68040, PowerPC G3, and PowerPC G4 hardware. G5 and Classic layer support coming soon. Every token is computed directly on the Mac's CPU.

It is not a port of llama2.c, llama.cpp, or any other existing inference codebase. The forward pass, the BPE tokenizer, the arena allocator, the disk pager, and the AltiVec SIMD kernels were all written from scratch against Mac Toolbox APIs in CodeWarrior Pro 5.

By Alex Hoopes / OldAppleStuff

## What it runs

The engine supports two architecture families:

* LLaMA-style models (RMSNorm, SwiGLU, RoPE). That covers LLaMA, Mistral, Qwen, Gemma, TinyLlama, SmolLM, and StableLM.
* GPT-2-style models (LayerNorm, GeLU, learned positional embeddings). That covers GPT-2, OPT, Pythia, GPT-J, GPT-NeoX, Falcon, and Phi.

A Python script in `tools/convert_to_macinai.py` converts any HuggingFace model in one of those families into the MacinAI `.bin` format that the C engine loads. The format has a 128-byte header followed by a vocab section and the weight tensors, all big-endian so 68K machines can read it natively without byte-swapping.

Float32 and Q8_0 per-group quantization are both supported.

On a 1GHz PowerBook G4 Titanium with 1GB of RAM running Mac OS 9.2.2, these are measured numbers from real hardware:

```
MacinAI Tool 94M    2.66 tok/s      Q8, 107 MB, custom tool model
GPT-2 124M          1.45 tok/s      Q8, 141 MB
SmolLM 360M         0.85 tok/s      Q8, 394 MB
Qwen 2.5 0.5B       0.63 tok/s      Q8, 532 MB
TinyLlama 1.1B      0.10 tok/s      Q8, 1.18 GB, disk paging
```

The 94M MacinAI Tool model is a custom LLaMA-family transformer trained on Macintosh documentation for AppleScript tool-use. Its weights are not in this repo (see below), but the engine runs it with no MacinAI-specific code paths.

The disk pager makes it possible to run models larger than available RAM by streaming layer weights from disk during the forward pass. Painfully slow, but it works.

## Repo layout

```
client/      Mac OS 9 client application source (C89)
installer/   Two-disc CD installer application source
tools/       convert_to_macinai.py, HuggingFace to .bin converter
```

Source files in this repo use standard LF line endings for GitHub compatibility. If you transfer them to a classic Mac for building, your transfer method (BinHex, MacBinary, Fetch, etc.) will handle the line ending conversion, or you can convert manually.

## What is not in this repo

The training pipeline, raw weights, and training data for the custom 94M MacinAI Tool model are not in this repo, and aren't being released right now. The model was trained on Inside Macintosh, MacWorld archives, and vintage Usenet.

What is available for download: compiled binaries of the client and installer, CD-ROM ISOs, and the converted MacinAI `.bin` model files (including the 94M Tool model in ready-to-run form). Those all live at <https://oldapplestuff.com/repo/Custom_Software/MacinAI-Local/>.

This is a source release. The engine also runs with any HuggingFace model you convert yourself using the tool in `tools/`.

## Converting a HuggingFace model

The converter in `tools/` runs on a modern machine with Python, torch, and transformers installed:

```
pip install torch transformers
python tools/convert_to_macinai.py --model gpt2 --output gpt2.bin
python tools/convert_to_macinai.py --model meta-llama/Llama-3.2-1B --quantize q8 --output llama3-1b.bin
```

The script auto-detects the architecture family, reads the weights, applies Q8_0 quantization if requested, and writes a `.bin` file the C engine can load. The engine picks up any `.bin` file placed in the application folder or in a `Models` subfolder next to the built app.

## Known limitations

Inference is slow on vintage hardware. On a G4 you get between roughly 0.1 and 2.7 tokens per second depending on the model size. That is the hardware, not the engine.

The disk pager eats I/O bandwidth on large models. TinyLlama 1.1B takes about ten seconds per token on a flash IDE drive, which is the fastest storage the Ti PowerBook supports.

The 68K code path compiles but is not fully tuned yet. On a 68040 without AltiVec you should expect significantly slower inference than the G4 numbers above.

There are a handful of stubbed functions in the client UI (the output scrollbar no-ops, settings persistence is incomplete). They are labelled in the source.

## Other vintage Macintosh projects

* **MacinCord**, a Discord client for vintage Macs.
  * Website: <https://oldapplestuff.com/macincord/>
  * Source: <https://github.com/alexhoopes/macincord-relay>

## Support

This is not an expectation of mine, but since nice people exist on the internet, if you'd like to support the work I have a Ko-fi at <https://ko-fi.com/alexhoopes>. Thank you!

## Author
Alex Hoopes

## License

GPL v3. See `LICENSE`.