# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working in this repository.

## Project

`llhfte` — Low Latency High Frequency Trading Engine targeting NASDAQ ITCH 5.0. Written in C++23, bare-metal, no heavy dependencies. Goal: handle real HFT-grade NASDAQ feed throughput.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/llhfte
```

Debug build (ASan + UBSan enabled):

```bash
cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dbg -j$(nproc)
```

## Architecture

```
src/
  main.cpp          — entry point
  feed/             — NASDAQ ITCH 5.0 parser, raw wire decoding
  book/             — order book (price-level aggregation, per-symbol)
  strategy/         — signal generation, position management
  core/             — shared primitives (ring buffer, memory pool, clock)
  util/             — non-hot-path helpers (logging, config)
include/            — public headers if needed
build/              — cmake out-of-tree build (gitignored)
```

## Key constraints

- **No exceptions, no RTTI** — `-fno-exceptions -fno-rtti` enforced in CMake. Use return codes or `std::expected`.
- **No allocations on the hot path** — pre-allocate pools at startup; avoid `new`/`delete`/`std::vector` growth in feed/book/strategy.
- **`-march=native`** — binary is not portable; optimized for the local CPU (AVX2/AVX-512 SIMD available if present).
- **ITCH 5.0 wire format** — big-endian, packed, no alignment guarantee. Always use `__builtin_bswap*` or `std::byteswap` to read multi-byte fields. Never dereference unaligned pointers as typed structs without `#pragma pack` or `memcpy`.
- **Max 1–2 external libs** — prefer implementing from scratch. `liburing` (io_uring) is the only anticipated optional dependency for kernel-bypass UDP/TCP receive.
