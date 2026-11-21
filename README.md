<div align="center">
	<img width="600" src="README/DF-EXPLORER-S.png" alt="DF EXPLORER">
	<br />
	<br />
</div>

<p align="center">
	<b>DF Archon Aztec Explorer</b><br />
	CUDA planet explorer for Dark Forest on Aztec (Poseidon2)
</p>

## Credits & lineage

This project is a port of the original **DF Explorer** CUDA miner.

Huge thanks to the people who built and shared the foundations we stand on:

- **Original DF Explorer** — [@yunyandeng](https://twitter.com/YUNYANDENG), [Guild Waterdrop](https://buidl.mirror.xyz/0I9oYGGKOxDrb2m8OjubyxjU4wYbs9Zp49c0IO2OwUc) / [MarrowDAO](https://twitter.com/marrowdao)
- Upstream repo: [guild-w/df-explorer](https://github.com/guild-w/df-explorer)
- **Dark Forest** — [@darkforest_eth](https://twitter.com/darkforest_eth) and the DF community
- **Aztec / Barretenberg Poseidon2** — cryptography used by the S2 client (`@aztec-labs/foundation` / bb.js)

The original explorer mined planets with **MiMC** for classic Dark Forest rounds.  
This fork keeps that CUDA explore architecture (HTTP `/explore` + Remote Explorer plugin) and replaces the hash pipeline with **BN254 Poseidon2** so it matches **Dark Forest Aztec Season 2**.

```text
hash = Poseidon2([planetHashKey, toFr(x), toFr(y)])
planet iff hash < LOCATION_ID_UB / planetRarity
```

`toFr(n)` maps negative coords to `n + BN254_Fr_MODULUS`.

---

## What you get

| Piece | Role |
| --- | --- |
| `df-explorer` | CUDA HTTP server (`POST /explore`) |
| `plugins/Remote-Explorer.js` | Client plugin for dfpunk-aztec / S2 |
| `samples/standalone` | Self-tests + rate sample |
| `samples/cpu_gpu_eq` | CPU ↔ GPU equality on large chunks |

Typical GPU rate on an RTX 3060 Ti class card: **~10–14 M hashes/sec** (fast Fr Montgomery path).

Target client: [dfaztec-s2.dfarchon.xyz](https://dfaztec-s2.dfarchon.xyz/)

---

## Requirements

- NVIDIA GPU + driver (`nvidia-smi`)
- CUDA toolkit (`nvcc`), CMake ≥ 3.18, g++ (C++17)
- GMP (`libgmp-dev`)
- Git submodules: CGBN, workflow

## Build

```bash
git clone --recurse-submodules <this-repo-url>
cd dfarchon-aztec-explorer

# HTTP library
cd thirdparty/workflow && make -j"$(nproc)" && cd ../..

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_HOST_COMPILER="$(which g++)" \
  -DCMAKE_CXX_COMPILER="$(which g++)"
make -j"$(nproc)"
```

Helper script (same steps): `scripts/build_aztec.sh`

### CUDA 13 / linker notes

- `thirdparty/cg_shim/cooperative_groups.h` is on the include path so older CGBN headers stay usable under newer CUDA.
- On some Ubuntu 24.04 setups you may need a `libpthread.so` link shim under `thirdparty/link_shim` (see `README_AZTEC.md`).

## Run

```bash
cd build
./df-explorer          # http://0.0.0.0:8880/explore
./samples/standalone   # correctness + rate
./samples/cpu_gpu_eq   # 512² / 1024² CPU vs GPU
```

### Plugin

1. Load [`plugins/Remote-Explorer.js`](plugins/Remote-Explorer.js) in the S2 client
2. Set URL to `http://localhost:8880/explore` (or your host)
3. Pick pattern → **Explore!**

Hash rate in the UI is shown with **G / M / K** units.

### API (unchanged shape)

`POST /explore`

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

`sideLength` ∈ `[256, 2048]`. Perlin / biome are still computed in the client.

---

## More detail

See [`README_AZTEC.md`](README_AZTEC.md) for the correctness checklist, vector tooling, and build edge cases.

Classic MiMC sources remain under `src/mimc*` for historical reference; the default binary is **Poseidon2 only**.

---

<p align="center">
  Built with respect for Guild Waterdrop, MarrowDAO, and everyone who shipped DF Explorer.<br />
  Ported for Aztec S2 by DF Archon.
</p>
