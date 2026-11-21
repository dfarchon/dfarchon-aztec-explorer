#!/usr/bin/env bash
# Build Aztec S2 Poseidon2 df-explorer.
# Optional: activate a conda/micromamba env that provides nvcc + gmp first.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

git submodule update --init --recursive

if [[ ! -f thirdparty/workflow/_lib/libworkflow.a && ! -f thirdparty/workflow/_lib/libworkflow.so ]]; then
  echo "Building workflow..."
  make -C thirdparty/workflow -j"$(nproc)"
fi

mkdir -p thirdparty/link_shim
if [[ ! -e thirdparty/link_shim/libpthread.so && -e /usr/lib/x86_64-linux-gnu/libpthread.so.0 ]]; then
  ln -sfn /usr/lib/x86_64-linux-gnu/libpthread.so.0 thirdparty/link_shim/libpthread.so
fi

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_HOST_COMPILER="${CUDAHOSTCXX:-$(command -v g++)}" \
  -DCMAKE_CXX_COMPILER="${CXX:-$(command -v g++)}" \
  -DCMAKE_EXE_LINKER_FLAGS="-L${ROOT}/thirdparty/link_shim -L/usr/lib/x86_64-linux-gnu"
make -j"$(nproc)"
echo "Built: $ROOT/build/df-explorer"
echo "Run:   $ROOT/build/df-explorer"
