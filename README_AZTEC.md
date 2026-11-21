# DF Archon Aztec Explorer — technical notes

Companion to the main [`README.md`](README.md).

This tree is a **Poseidon2** port of the original Guild Waterdrop / MarrowDAO
**DF Explorer** (MiMC CUDA miner by [@yunyandeng](https://twitter.com/YUNYANDENG)).
Credit and thanks to those original developers — we only swapped the hash
pipeline for Dark Forest Aztec Season 2.

CUDA explore server for **Dark Forest Aztec Season 2**
([dfaztec-s2.dfarchon.xyz](https://dfaztec-s2.dfarchon.xyz/)).

Classic MiMC hashing is **not** used. Planet discovery matches the client:

```text
hash = Poseidon2([planetHashKey, toFr(x), toFr(y)])
planet iff hash < LOCATION_ID_UB / planetRarity
```

`toFr(n)` maps negative coords to `n + BN254_Fr_MODULUS`. Hash function is
Barretenberg BN254 Poseidon2 (same as `@aztec-labs/foundation` / Noir).

## Requirements

- NVIDIA GPU + working WSL2/Linux NVIDIA driver (`nvidia-smi`)
- CUDA toolkit (`nvcc`) — this repo was built with CUDA 13 from a user-local conda env
- GMP (`libgmp` + C++ bindings)
- CMake ≥ 3.18, g++ with C++17
- Git submodules: CGBN, workflow

## Build

```bash
cd ~/develop/DF-explorer/df-explorer
git submodule update --init --recursive

# Build workflow HTTP library
cd thirdparty/workflow && make -j"$(nproc)" && cd ../..

# Configure + build (ensure nvcc, gmp, cmake are on PATH)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_HOST_COMPILER="$(which g++)" \
  -DCMAKE_CXX_COMPILER="$(which g++)"
make -j"$(nproc)"
```

Binaries:

- `build/df-explorer` — HTTP server on port **8880**
- `build/samples/standalone` — self-test + CPU/GPU equality + rate sample

### Note on CUDA 13 + CGBN

`thirdparty/cg_shim/cooperative_groups.h` is intentionally first on the include
path so CGBN does not pull CUDA 13 libcu++ (which breaks nvcc stub generation).

If the linker fails on `libpthread.so` (Ubuntu 24.04), create:

```bash
mkdir -p thirdparty/link_shim
ln -sfn /usr/lib/x86_64-linux-gnu/libpthread.so.0 thirdparty/link_shim/libpthread.so
# then add -L$PWD/thirdparty/link_shim to the link line / CMAKE_EXE_LINKER_FLAGS
```

## Run server

```bash
cd build
./df-explorer
# listens on http://0.0.0.0:8880/explore
```

Startup runs CPU Poseidon2 self-tests (permutation + public hash vectors). Process
exits if they fail.

### API

`POST /explore` with JSON:

```json
{
  "chunkFootprint": {
    "bottomLeft": { "x": -256, "y": 0 },
    "sideLength": 256
  },
  "planetRarity": 16384,
  "planetHashKey": 420
}
```

`sideLength` must be in `[256, 2048]`.

Response:

```json
{
  "chunkFootprint": { "...": "..." },
  "planetLocations": [
    { "coords": { "x": -225, "y": 198 }, "hash": "<decimal field>" }
  ]
}
```

Perlin / biomebase are computed **in the client plugin**, not on the server.

## Remote-Explorer plugin

Files:

- [`plugins/Remote-Explorer.js`](plugins/Remote-Explorer.js) (repo copy)
- Desktop: `Plugins DF/Aztec/Remote-Explorer.js`

In the S2 client:

1. Open Plugins → paste / load `Remote-Explorer.js`
2. URL: `http://localhost:8880/explore` (or your LAN/VPN host)
3. Pick pattern → **Explore!** → optional target on map

The plugin uses `df.getConstructors().MinerManager` and posts
`chunkFootprint` / `planetRarity` / `planetHashKey` to the server.

## Correctness checklist (verified on RTX 3060 Ti)

| Check | Result |
| --- | --- |
| CPU Poseidon2 permutation test vector (Barretenberg) | OK |
| CPU `hash([1,2])`, `hash([1,2,3,4])`, `hash([0,0])` | OK vs public Aztec vectors |
| CPU == GPU explore set (64×64) | OK |
| Negative coords `toFr` / planet hash | OK vs `@aztec-labs/foundation` |
| `POST /explore` first planet vs bb.js | OK (`(-225,198)` with key 420) |
| CPU == GPU on 3 origins × {512², 1024²} | OK |
| Sample rate (1024×1024, wall clock) | ~10–14 M H/s (Fr Montgomery CIOS) |

Reproduce vectors:

```bash
node tools/gen_poseidon_vectors.mjs
# uses existing ~/develop/df/dfpunk-aztec node_modules — no pnpm install
./build/samples/standalone
```

## Classic MiMC

Original MiMC sources remain under `src/mimc*` for reference. The default
`df-explorer` target on `aztecV6` is **Poseidon2 only**.
