#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <cuda_runtime.h>
#include <gmp.h>
#include <gmpxx.h>

#include "poseidon2.h"
#include "poseidon2_fr_params.h"

#define DEFAULT_TPB 256
#define MAX_CUDA_OUT 8192

#define CUDA_CHECK(call)                                                       \
    do {                                                                       \
        cudaError_t err__ = (call);                                            \
        if (err__ != cudaSuccess) {                                            \
            fprintf(stderr, "CUDA error %s:%d: %s\n", __FILE__, __LINE__,      \
                    cudaGetErrorString(err__));                                \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

// Host constexpr tables are not visible to device code; mirror into constant mem.
__constant__ uint64_t C_P[4];
__constant__ uint64_t C_NP0;
__constant__ uint64_t C_R2[4];
__constant__ uint64_t C_IV3_M[4];
__constant__ uint64_t C_DIAG_M[4][4];
__constant__ uint64_t C_RC_M[64][4][4];
__constant__ uint64_t C_PERM_IN[4][4];
__constant__ uint64_t C_PERM_OUT[4][4];

struct explore_in_t {
    int64_t x;
    int64_t y;
    uint32_t side_length;
    uint32_t key;
    uint32_t rarity;
    uint64_t threshold[4]; // P / rarity, little-endian limbs
};

struct explore_out_item_t {
    uint64_t hash[4]; // normal-domain little-endian u64
    int64_t x;
    int64_t y;
};

struct explore_out_t {
    explore_out_item_t planets[MAX_CUDA_OUT];
    uint32_t count;
};

static int32_t g_tpb = DEFAULT_TPB;

// ---------------- BN254 Fr (4x uint64, Montgomery CIOS) ----------------

struct Fr {
    uint64_t v[4];
};

__device__ __forceinline__ void fr_set0(Fr &r)
{
    r.v[0] = r.v[1] = r.v[2] = r.v[3] = 0;
}

__device__ __forceinline__ void fr_set_u64(Fr &r, uint64_t x)
{
    r.v[0] = x;
    r.v[1] = r.v[2] = r.v[3] = 0;
}

__device__ __forceinline__ void fr_copy(Fr &r, const Fr &a)
{
    r.v[0] = a.v[0];
    r.v[1] = a.v[1];
    r.v[2] = a.v[2];
    r.v[3] = a.v[3];
}

__device__ __forceinline__ void fr_load(Fr &r, const uint64_t src[4])
{
    r.v[0] = src[0];
    r.v[1] = src[1];
    r.v[2] = src[2];
    r.v[3] = src[3];
}

__device__ __forceinline__ void fr_store(uint64_t dst[4], const Fr &a)
{
    dst[0] = a.v[0];
    dst[1] = a.v[1];
    dst[2] = a.v[2];
    dst[3] = a.v[3];
}

__device__ __forceinline__ int fr_cmp(const Fr &a, const Fr &b)
{
    for (int i = 3; i >= 0; --i) {
        if (a.v[i] < b.v[i]) return -1;
        if (a.v[i] > b.v[i]) return 1;
    }
    return 0;
}

__device__ __forceinline__ void fr_add(Fr &r, const Fr &a, const Fr &b)
{
    uint64_t c = 0;
#pragma unroll
    for (int i = 0; i < 4; ++i) {
        uint64_t t = a.v[i] + c;
        c = (t < a.v[i]) ? 1 : 0;
        uint64_t s = t + b.v[i];
        c += (s < t) ? 1 : 0;
        r.v[i] = s;
    }
    // if r >= P, r -= P (also if carry)
    Fr p;
    fr_load(p, C_P);
    if (c || fr_cmp(r, p) >= 0) {
        uint64_t br = 0;
#pragma unroll
        for (int i = 0; i < 4; ++i) {
            uint64_t t = r.v[i] - br;
            br = (t > r.v[i]) ? 1 : 0;
            uint64_t s = t - p.v[i];
            br += (s > t) ? 1 : 0;
            r.v[i] = s;
        }
    }
}

__device__ __forceinline__ void fr_sub(Fr &r, const Fr &a, const Fr &b)
{
    // r = a - b; if a < b then a + P - b
    uint64_t br = 0;
    Fr tmp;
#pragma unroll
    for (int i = 0; i < 4; ++i) {
        uint64_t t = a.v[i] - br;
        br = (t > a.v[i]) ? 1 : 0;
        uint64_t s = t - b.v[i];
        br += (s > t) ? 1 : 0;
        tmp.v[i] = s;
    }
    if (br) {
        Fr p;
        fr_load(p, C_P);
        uint64_t c = 0;
#pragma unroll
        for (int i = 0; i < 4; ++i) {
            uint64_t t = tmp.v[i] + c;
            c = (t < tmp.v[i]) ? 1 : 0;
            uint64_t s = t + p.v[i];
            c += (s < t) ? 1 : 0;
            tmp.v[i] = s;
        }
    }
    fr_copy(r, tmp);
}

// Montgomery CIOS multiply: out = a * b * R^{-1} mod P
__device__ __forceinline__ void mont_mul(Fr &out, const Fr &a, const Fr &b)
{
    uint64_t t[5] = {0, 0, 0, 0, 0};
#pragma unroll
    for (int i = 0; i < 4; ++i) {
        uint64_t C = 0;
#pragma unroll
        for (int j = 0; j < 4; ++j) {
            unsigned __int128 p = (unsigned __int128)a.v[j] * b.v[i] + t[j] + C;
            t[j] = (uint64_t)p;
            C = (uint64_t)(p >> 64);
        }
        {
            unsigned __int128 p = (unsigned __int128)t[4] + C;
            t[4] = (uint64_t)p;
            // overflow into bit above ignored (bounded)
        }

        uint64_t m = t[0] * C_NP0;
        unsigned __int128 p = (unsigned __int128)m * C_P[0] + t[0];
        C = (uint64_t)(p >> 64);
#pragma unroll
        for (int j = 1; j < 4; ++j) {
            p = (unsigned __int128)m * C_P[j] + t[j] + C;
            t[j - 1] = (uint64_t)p;
            C = (uint64_t)(p >> 64);
        }
        p = (unsigned __int128)t[4] + C;
        t[3] = (uint64_t)p;
        t[4] = (uint64_t)(p >> 64);
    }

    out.v[0] = t[0];
    out.v[1] = t[1];
    out.v[2] = t[2];
    out.v[3] = t[3];
    Fr pmod;
    fr_load(pmod, C_P);
    if (t[4] || fr_cmp(out, pmod) >= 0) {
        fr_sub(out, out, pmod);
    }
}

__device__ __forceinline__ void mont_sqr(Fr &out, const Fr &a)
{
    mont_mul(out, a, a);
}

__device__ __forceinline__ void to_mont(Fr &out, const Fr &a)
{
    Fr r2;
    fr_load(r2, C_R2);
    mont_mul(out, a, r2);
}

__device__ __forceinline__ void from_mont(Fr &out, const Fr &a)
{
    Fr one;
    fr_set_u64(one, 1);
    mont_mul(out, a, one);
}

__device__ __forceinline__ void apply_sbox(Fr &x)
{
    Fr x2, x4;
    mont_sqr(x2, x);
    mont_sqr(x4, x2);
    mont_mul(x, x, x4);
}

__device__ __forceinline__ void matrix_4x4(Fr s[4])
{
    Fr t0, t1, t2, t3, t4, t5, t6, t7;
    fr_add(t0, s[0], s[1]);
    fr_add(t1, s[2], s[3]);
    fr_add(t2, s[1], s[1]);
    fr_add(t2, t2, t1);
    fr_add(t3, s[3], s[3]);
    fr_add(t3, t3, t0);
    fr_add(t4, t1, t1);
    fr_add(t4, t4, t4);
    fr_add(t4, t4, t3);
    fr_add(t5, t0, t0);
    fr_add(t5, t5, t5);
    fr_add(t5, t5, t2);
    fr_add(t6, t3, t5);
    fr_add(t7, t2, t4);
    fr_copy(s[0], t6);
    fr_copy(s[1], t5);
    fr_copy(s[2], t7);
    fr_copy(s[3], t4);
}

__device__ __forceinline__ void matrix_internal(Fr s[4])
{
    Fr sum, t, diag;
    fr_copy(sum, s[0]);
    fr_add(sum, sum, s[1]);
    fr_add(sum, sum, s[2]);
    fr_add(sum, sum, s[3]);
#pragma unroll
    for (int i = 0; i < 4; ++i) {
        fr_load(diag, C_DIAG_M[i]);
        mont_mul(t, diag, s[i]);
        fr_add(s[i], t, sum);
    }
}

__device__ __forceinline__ void permutation(Fr state[4])
{
    Fr rc;
    matrix_4x4(state);

    const int rounds_f_beginning = 4;
    for (int i = 0; i < rounds_f_beginning; ++i) {
#pragma unroll
        for (int j = 0; j < 4; ++j) {
            fr_load(rc, C_RC_M[i][j]);
            fr_add(state[j], state[j], rc);
        }
#pragma unroll
        for (int j = 0; j < 4; ++j) {
            apply_sbox(state[j]);
        }
        matrix_4x4(state);
    }

    const int p_end = 4 + 56;
    for (int i = rounds_f_beginning; i < p_end; ++i) {
        fr_load(rc, C_RC_M[i][0]);
        fr_add(state[0], state[0], rc);
        apply_sbox(state[0]);
        matrix_internal(state);
    }

    for (int i = p_end; i < 64; ++i) {
#pragma unroll
        for (int j = 0; j < 4; ++j) {
            fr_load(rc, C_RC_M[i][j]);
            fr_add(state[j], state[j], rc);
        }
#pragma unroll
        for (int j = 0; j < 4; ++j) {
            apply_sbox(state[j]);
        }
        matrix_4x4(state);
    }
}

__device__ __forceinline__ void coords_to_fr(Fr &r, int64_t num)
{
    uint64_t mag = (uint64_t)llabs(num);
    fr_set_u64(r, 0);
    r.v[0] = mag;
    // mag fits in 63 bits for game coords; if ever larger use more limbs
    if (num < 0) {
        Fr p;
        fr_load(p, C_P);
        fr_sub(r, p, r);
    }
}

__device__ __forceinline__ void planet_hash(Fr &out_normal, uint32_t key, int64_t x, int64_t y)
{
    Fr state[4];
    Fr fk, fx, fy;

    fr_set_u64(fk, key);
    coords_to_fr(fx, x);
    coords_to_fr(fy, y);

    to_mont(state[0], fk);
    to_mont(state[1], fx);
    to_mont(state[2], fy);
    fr_load(state[3], C_IV3_M);

    permutation(state);
    from_mont(out_normal, state[0]);
}

__device__ __forceinline__ bool is_planet(const Fr &hash, const uint64_t thr_limbs[4])
{
    Fr thr;
    fr_load(thr, thr_limbs);
    return fr_cmp(hash, thr) < 0;
}

static void host_threshold(uint64_t out[4], uint32_t rarity)
{
    uint64_t rem = 0;
    for (int i = 3; i >= 0; --i) {
        unsigned __int128 cur = ((unsigned __int128)rem << 64) | Poseidon2Fr::P[i];
        out[i] = (uint64_t)(cur / rarity);
        rem = (uint64_t)(cur % rarity);
    }
}

__global__ void kernel_explore(const explore_in_t *__restrict__ explore_params,
                               explore_out_t *__restrict__ explore_out,
                               uint32_t count)
{
    uint32_t instance = blockIdx.x * blockDim.x + threadIdx.x;
    if (instance >= count) return;

    int64_t index_x = (int64_t)(instance / explore_params->side_length);
    int64_t x = explore_params->x + index_x;
    int64_t y = explore_params->y + (int64_t)(instance % explore_params->side_length);

    Fr hash;
    planet_hash(hash, explore_params->key, x, y);
    if (!is_planet(hash, explore_params->threshold)) return;

    uint32_t i = atomicInc((uint32_t *)&(explore_out->count), 0xffffffff);
    if (i >= MAX_CUDA_OUT) return;
    explore_out->planets[i].x = x;
    explore_out->planets[i].y = y;
    fr_store(explore_out->planets[i].hash, hash);
}

// GPU self-check: permutation test vector
__global__ void kernel_perm_test(uint64_t out[4][4], int *ok)
{
    if (threadIdx.x != 0 || blockIdx.x != 0) return;
    Fr state[4];
    for (int i = 0; i < 4; ++i) {
        Fr n;
        fr_load(n, C_PERM_IN[i]);
        to_mont(state[i], n);
    }
    permutation(state);
    int good = 1;
    for (int i = 0; i < 4; ++i) {
        Fr n;
        from_mont(n, state[i]);
        fr_store(out[i], n);
        Fr exp;
        fr_load(exp, C_PERM_OUT[i]);
        if (fr_cmp(n, exp) != 0) good = 0;
    }
    *ok = good;
}

static int32_t get_env_i32(const char *name, int32_t default_value)
{
    char *value_str = getenv(name);
    if (value_str == nullptr) return default_value;
    return atoi(value_str);
}

void init()
{
    g_tpb = get_env_i32("POSEIDON_CUDA_BLOCK_SIZE", DEFAULT_TPB);
    if (g_tpb <= 0 || g_tpb > 1024) g_tpb = DEFAULT_TPB;
    int32_t device_id = get_env_i32("POSEIDON_CUDA_DEVICE", 0);

    printf("poseidon2 fast-fr cuda block size: %d\n", g_tpb);
    printf("poseidon2 cuda device id: %d\n", device_id);
    CUDA_CHECK(cudaSetDevice(device_id));

    {
        uint64_t np0 = Poseidon2Fr::NP0;
        CUDA_CHECK(cudaMemcpyToSymbol(C_P, Poseidon2Fr::P, sizeof(C_P)));
        CUDA_CHECK(cudaMemcpyToSymbol(C_NP0, &np0, sizeof(np0)));
        CUDA_CHECK(cudaMemcpyToSymbol(C_R2, Poseidon2Fr::R2, sizeof(C_R2)));
        CUDA_CHECK(cudaMemcpyToSymbol(C_IV3_M, Poseidon2Fr::IV3_M, sizeof(C_IV3_M)));
        CUDA_CHECK(cudaMemcpyToSymbol(C_DIAG_M, Poseidon2Fr::DIAG_M, sizeof(C_DIAG_M)));
        CUDA_CHECK(cudaMemcpyToSymbol(C_RC_M, Poseidon2Fr::RC_M, sizeof(C_RC_M)));
        CUDA_CHECK(cudaMemcpyToSymbol(C_PERM_IN, Poseidon2Fr::PERM_IN, sizeof(C_PERM_IN)));
        CUDA_CHECK(cudaMemcpyToSymbol(C_PERM_OUT, Poseidon2Fr::PERM_OUT, sizeof(C_PERM_OUT)));
    }

    int st = poseidon2_self_test();
    if (st != 0) {
        fprintf(stderr, "FATAL: poseidon2 CPU self-test failed (%d)\n", st);
        exit(1);
    }

    // GPU permutation vector check
    uint64_t (*d_out)[4];
    int *d_ok;
    uint64_t h_out[4][4];
    int h_ok = 0;
    CUDA_CHECK(cudaMalloc((void **)&d_out, sizeof(uint64_t) * 16));
    CUDA_CHECK(cudaMalloc((void **)&d_ok, sizeof(int)));
    kernel_perm_test<<<1, 1>>>(d_out, d_ok);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(&h_ok, d_ok, sizeof(int), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_out, d_out, sizeof(h_out), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaFree(d_out));
    CUDA_CHECK(cudaFree(d_ok));
    if (!h_ok) {
        fprintf(stderr, "FATAL: GPU Poseidon2 permutation test vector mismatch\n");
        for (int i = 0; i < 4; ++i) {
            fprintf(stderr, "  out[%d]=%016lx%016lx%016lx%016lx\n", i,
                    h_out[i][3], h_out[i][2], h_out[i][1], h_out[i][0]);
        }
        exit(1);
    }
    printf("poseidon2 GPU permutation vector: OK\n");
}

static void u64_le_to_mpz(mpz_class &out, const uint64_t v[4])
{
    // little-endian limbs → mpz (order=-1: least significant word first)
    mpz_import(out.get_mpz_t(), 4, -1, sizeof(uint64_t), 0, 0, v);
}

static void get_result(explore_out_t *cuda_result, std::vector<location_hash_t> &hashes)
{
    mpz_class h;
    for (uint32_t i = 0; i < cuda_result->count; ++i) {
        u64_le_to_mpz(h, cuda_result->planets[i].hash);
        hashes.push_back({h.get_str(10), cuda_result->planets[i].x, cuda_result->planets[i].y});
    }
}

void gpu_explore_chunk(int64_t bottom_left_x,
                       int64_t bottom_left_y,
                       uint32_t side_length,
                       uint32_t key,
                       uint32_t rarity,
                       std::vector<location_hash_t> &hashes)
{
    uint32_t TPB = (uint32_t)g_tpb;

    explore_in_t in_params;
    in_params.x = bottom_left_x;
    in_params.y = bottom_left_y;
    in_params.side_length = side_length;
    in_params.key = key;
    in_params.rarity = rarity;
    host_threshold(in_params.threshold, rarity);

    explore_in_t *gpu_in_params;
    CUDA_CHECK(cudaMalloc((void **)&gpu_in_params, sizeof(explore_in_t)));
    CUDA_CHECK(cudaMemcpy(gpu_in_params, &in_params, sizeof(explore_in_t), cudaMemcpyHostToDevice));

    explore_out_t *out;
    CUDA_CHECK(cudaHostAlloc((void **)&out, sizeof(explore_out_t), cudaHostAllocDefault));
    out->count = 0;
    explore_out_t *gpu_out;
    CUDA_CHECK(cudaMalloc((void **)&gpu_out, sizeof(explore_out_t)));
    CUDA_CHECK(cudaMemcpy(gpu_out, out, sizeof(explore_out_t), cudaMemcpyHostToDevice));

    uint32_t count = side_length * side_length;
    kernel_explore<<<(count + TPB - 1) / TPB, TPB>>>(gpu_in_params, gpu_out, count);
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(out, gpu_out, sizeof(explore_out_t), cudaMemcpyDeviceToHost));
    get_result(out, hashes);

    CUDA_CHECK(cudaFreeHost(out));
    CUDA_CHECK(cudaFree(gpu_in_params));
    CUDA_CHECK(cudaFree(gpu_out));
}
