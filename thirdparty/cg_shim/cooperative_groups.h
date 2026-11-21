#pragma once
// Minimal shim so CGBN does not pull CUDA 13 libcu++ (breaks nvcc stubs).
namespace cooperative_groups {
struct thread_block {};
inline __device__ thread_block this_thread_block() { return thread_block(); }
template <int Size, typename ParentT>
struct thread_block_tile {};
template <int Size>
__device__ thread_block_tile<Size, thread_block> tiled_partition(thread_block) {
  return thread_block_tile<Size, thread_block>();
}
}
