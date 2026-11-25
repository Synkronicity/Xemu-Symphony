# Contributing to Xemu Symphony

`Xemu Symphony` is an independent downstream fork of Xemu dedicated to hardware-accurate Low-Level Emulation (LLE) and high-performance High-Level Emulation (HLE) of the original Xbox NVIDIA MCPX Audio Processing Unit (VP, GP, EP, DSP DMA) and Intel ICH AC'97 Audio Codec Interface (ACI).

We welcome clean, well-verified contributions that advance MCPX/DSP hardware accuracy, audio fidelity, UI clarity, and cross-platform packaging (especially macOS / ARM64 CI). Note that this fork is strictly audio-focused; general NV2A rasterizer or unrelated QEMU changes belong upstream unless they directly fix an audio-blocking bug.

For complete hardware specifications and subsystem contracts, read [`docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md`](docs/MCPX_SYMPHONY_ARCHITECTURE_AND_PORTING.md) and [`AGENTS.md`](AGENTS.md) before opening a pull request.

---

## Architectural Principles & Scope

Contributions touching the APU, AC'97, or audio pipeline must respect these boundaries:

1. **Pure ISO C Emulation Core:** All emulation cores (`hw/xbox/mcpx/apu/`, `hw/xbox/mcpx/aci.c`, `hw/audio/ac97.c`) are strict ISO C99/C11. Do not introduce Rust toolchain dependencies, FFI bridges, C++ templates, or runtime JIT compilers. C++17 is restricted to UI layers (`ui/xui/`).
2. **Default Lossless Multi-Channel Output:** Symphony mixes all 6 discrete channels internally in 32-bit float (`ch6[32][6]`: `FL, FR, FC, LFE, RL, RR` at 48 kHz) and routes through the 2:2:2 matrix (Discrete 5.1 LPCM, 5.1-to-2.0 Binaural HRTF via `hle_binaural.h`, HLE Freeverb via `hle_freeverb.h`, or 2.0 Stereo). Optional preservation modes (Pro Logic II `Lt/Rt` and user-supplied `dolby_ep.bin` AC-3) must remain strictly opt-in and never introduce `liba52` or S/PDIF dependencies into the default pipeline.
3. **Zero Proprietary Firmware Dependency:** The EP defaults to the built-in clean-room 584-word `symphony_ep_rom.h` micro-kernel and public-domain ATSC A/52A `ep_yrom.h` (`Y:$0800–$0FFF`). Never commit proprietary Microsoft/Dolby binaries (`dolby_ep.bin`, Xbox BIOSes, or XDK libraries).
4. **Verified Silicon Invariants:** Preserve 24-bit DSP word packing (`& 0x00FFFFFF`), 32K P-RAM / 64K zero-init X-RAM / 2K zero-init Y-RAM, Bit-14 (`0x4000`) zero-generator DMA behavior, `~(item_size - 1)` word-aligned circular scratch DMA, unconditional VP `use_hrtf` 4-corner routing, GP `b < 2` mixbuffer scratch isolation (`0.35f`/`0.65f` headroom), bounded subframe budgets (`GP_MAX_SUBFRAME_CYCLES = 100000`, `EP_SUBFRAME_CYCLES = 12800`), and AC'97 LPC IRQ 6 / `civ == lvi` resumption semantics.

---

## Code Style & Build Standards

* **Language & Formatting:** Strict C99/C11 for emulator cores, C++17 for `ui/xui/`. Run `clang-format` on modified C/C++ files and retain Unix LF line endings. Avoid unrelated whitespace churn.
* **Compiler Cleanliness:** Must build cleanly via Meson/Ninja (`./build.sh` or `./build.sh -p win64-cross`) with zero new warnings under GCC and Clang (`-Wall -Wextra -Werror=implicit-function-declaration`). Do not commit standalone Makefiles, CMake lists, or IDE project files.
* **Licensing:** Modifications to Xemu/QEMU files are licensed under **GPL-2.0-or-later**. Contributions to standalone clean-room headers (`symphony_ep_rom.h`, `ep_yrom.h`, `hle_binaural.h`, `hle_freeverb.h`) are dual-licensed under **MIT or GPL-2.0-or-later** so other Xbox emulator projects can adopt them cleanly.

---

## Commit Message Standards

Use concise, imperative commit headers under 72 characters followed by a technical explanation:

{subsystem}: {short imperative summary}

[Detailed explanation of hardware behavior, register/telemetry proof, or fix rationale]

Examples:

```
apu/dma: word-align circular scratch descriptors to prevent byte shearing
ac97: allow single-descriptor LVI re-arming when civ equals lvi
monitor/sdl: scale throttle watermarks to active 2.0/5.1 frame footprint
ci: add macOS arm64 release packaging workflow
```

## Testing & Verification

Synthetic unit tests do not replace retail Xbox titles. All functional PRs should document:

1.  **Clean Build:** Zero new compiler warnings under GCC or Clang.
    
2.  **Retail Game Verification:** Test against real titles exercising the modified path (e.g., _WWE Raw 2_ / _Whiplash_ for circular DMA & 5.1 scratch isolation, _Quantum Redshift_ for HLE GP handshakes & LLE headroom, _The Lord of the Rings: The Third Age_ for AC'97 streaming, _Halo 2_ / _Doom 3_ for 5.1 & binaural spatialization).
    
3.  **Frame Pacing & Channel Mapping:** Confirm locked frame pacing without `dsound.sys` watchdog stalls and verify discrete `FL, FR, FC, LFE, RL, RR` routing in both LLE and HLE modes.
    

## Use of AI Tooling

AI-assisted tooling is permitted when paired with strict human engineering discipline:

1.  **Total Technical Ownership:** You are responsible for every line submitted and must be able to explain and defend it against hardware documentation or telemetry. Unreviewed autonomous "vibecoded" diffs, hallucinated register shims, or unverified test harnesses will be closed.
    
2.  **Transparency:** State the models and workflow used in the PR description (see [`AI_DISCLOSURE_LOG.md`](https://www.google.com/search?q=AI_DISCLOSURE_LOG.md) for how this repository documents AI assistance).
    

## Governance

*   All bug reports, technical discussions, and PR reviews happen publicly on GitHub (Issues, Discussions, and Pull Requests).
    
*   Reviews optimize for hardware accuracy, measurable results in retail titles, and clean diffs. If a change works, respects the invariants above, and passes CI, it will be merged without gatekeeping.