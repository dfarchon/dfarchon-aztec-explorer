#ifndef DF_EXPLORER_POSEIDON2_H
#define DF_EXPLORER_POSEIDON2_H

#include <stdint.h>
#include <string>
#include <vector>
#include <gmpxx.h>

typedef struct {
    std::string hash;
    int64_t x;
    int64_t y;
} location_hash_t;

void init();

void gpu_explore_chunk(int64_t bottom_left_x,
                       int64_t bottom_left_y,
                       uint32_t side_length,
                       uint32_t key,
                       uint32_t rarity,
                       std::vector<location_hash_t> &hashes);

// CPU Poseidon2 (Barretenberg BN254 sponge). Used for correctness checks.
mpz_class cpu_poseidon2_hash(const std::vector<mpz_class> &inputs);

// Hash planet location: Poseidon2([key, toFr(x), toFr(y)])
mpz_class cpu_planet_hash(uint32_t key, int64_t x, int64_t y);

void cpu_explore_chunk(int64_t bottom_left_x,
                       int64_t bottom_left_y,
                       uint32_t side_length,
                       uint32_t key,
                       uint32_t rarity,
                       std::vector<location_hash_t> &hashes);

// Self-test: permutation + known sponge vectors. Returns 0 on success.
int poseidon2_self_test();

#endif // DF_EXPLORER_POSEIDON2_H
