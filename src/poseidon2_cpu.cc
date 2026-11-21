#include "poseidon2.h"
#include "poseidon2_params.h"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

mpz_class g_p;
mpz_class g_diag[4];
mpz_class g_rc[64][4];
bool g_cpu_ready = false;

mpz_class limbs_to_mpz(const uint32_t limbs[8])
{
    mpz_class r = 0;
    for (int i = 7; i >= 0; --i) {
        r <<= 32;
        r += limbs[i];
    }
    return r;
}

void ensure_cpu_params()
{
    if (g_cpu_ready) {
        return;
    }
    g_p = limbs_to_mpz(Poseidon2Params::P_LIMBS);
    for (int i = 0; i < 4; ++i) {
        g_diag[i] = limbs_to_mpz(Poseidon2Params::DIAG_MINUS_ONE[i]);
    }
    for (int r = 0; r < 64; ++r) {
        for (int i = 0; i < 4; ++i) {
            g_rc[r][i] = limbs_to_mpz(Poseidon2Params::ROUND_CONSTANTS[r][i]);
        }
    }
    g_cpu_ready = true;
}

void fr_add(mpz_class &r, const mpz_class &a, const mpz_class &b)
{
    r = a + b;
    if (r >= g_p) {
        r -= g_p;
    }
}

void fr_sub(mpz_class &r, const mpz_class &a, const mpz_class &b)
{
    if (a >= b) {
        r = a - b;
    } else {
        r = g_p - (b - a);
    }
}

void fr_mul(mpz_class &r, const mpz_class &a, const mpz_class &b)
{
    r = (a * b) % g_p;
}

void fr_sqr(mpz_class &r, const mpz_class &a)
{
    fr_mul(r, a, a);
}

void apply_sbox(mpz_class &x)
{
    mpz_class x2, x4;
    fr_sqr(x2, x);
    fr_sqr(x4, x2);
    fr_mul(x, x, x4); // x^5
}

void matrix_multiplication_4x4(std::array<mpz_class, 4> &s)
{
    // MDS from Poseidon2 paper / Barretenberg
    mpz_class t0, t1, t2, t3, t4, t5, t6, t7, tmp;
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
    s[0] = t6;
    s[1] = t5;
    s[2] = t7;
    s[3] = t4;
}

void matrix_multiplication_internal(std::array<mpz_class, 4> &s)
{
    mpz_class sum = s[0];
    fr_add(sum, sum, s[1]);
    fr_add(sum, sum, s[2]);
    fr_add(sum, sum, s[3]);
    for (int i = 0; i < 4; ++i) {
        mpz_class t;
        fr_mul(t, g_diag[i], s[i]);
        fr_add(s[i], t, sum);
    }
}

void add_round_constants(std::array<mpz_class, 4> &s, int round)
{
    for (int i = 0; i < 4; ++i) {
        fr_add(s[i], s[i], g_rc[round][i]);
    }
}

void permutation_inplace(std::array<mpz_class, 4> &state)
{
    matrix_multiplication_4x4(state);

    constexpr int rounds_f_beginning = Poseidon2Params::ROUNDS_F / 2;
    for (int i = 0; i < rounds_f_beginning; ++i) {
        add_round_constants(state, i);
        for (int j = 0; j < 4; ++j) {
            apply_sbox(state[j]);
        }
        matrix_multiplication_4x4(state);
    }

    constexpr int p_end = rounds_f_beginning + Poseidon2Params::ROUNDS_P;
    for (int i = rounds_f_beginning; i < p_end; ++i) {
        fr_add(state[0], state[0], g_rc[i][0]);
        apply_sbox(state[0]);
        matrix_multiplication_internal(state);
    }

    for (int i = p_end; i < Poseidon2Params::NUM_ROUNDS; ++i) {
        add_round_constants(state, i);
        for (int j = 0; j < 4; ++j) {
            apply_sbox(state[j]);
        }
        matrix_multiplication_4x4(state);
    }
}

mpz_class to_fr_coord(int64_t n)
{
    ensure_cpu_params();
    if (n >= 0) {
        return mpz_class(static_cast<unsigned long>(n));
    }
    mpz_class mag(static_cast<unsigned long>(llabs(n)));
    return g_p - mag;
}

std::string mpz_to_dec(const mpz_class &v)
{
    return v.get_str(10);
}

bool is_planet(const mpz_class &hash, uint32_t rarity)
{
    ensure_cpu_params();
    mpz_class threshold = g_p / rarity;
    return hash < threshold;
}

} // namespace

mpz_class cpu_poseidon2_hash(const std::vector<mpz_class> &inputs)
{
    ensure_cpu_params();
    const size_t n = inputs.size();
    // IV = n << 64
    mpz_class iv = mpz_class(static_cast<unsigned long>(n));
    iv <<= 64;

    std::array<mpz_class, 4> state = {0, 0, 0, iv};
    std::array<mpz_class, 3> cache = {0, 0, 0};
    size_t cache_size = 0;

    auto duplex = [&]() {
        for (int i = 0; i < 3; ++i) {
            fr_add(state[i], state[i], cache[i]);
        }
        permutation_inplace(state);
        cache = {0, 0, 0};
        cache_size = 0;
    };

    for (size_t i = 0; i < n; ++i) {
        if (cache_size == 3) {
            duplex();
            cache[0] = inputs[i];
            cache_size = 1;
        } else {
            cache[cache_size] = inputs[i];
            cache_size += 1;
        }
    }
    duplex(); // squeeze
    return state[0];
}

mpz_class cpu_planet_hash(uint32_t key, int64_t x, int64_t y)
{
    std::vector<mpz_class> inputs = {
        mpz_class(key),
        to_fr_coord(x),
        to_fr_coord(y),
    };
    return cpu_poseidon2_hash(inputs);
}

void cpu_explore_chunk(int64_t bottom_left_x,
                       int64_t bottom_left_y,
                       uint32_t side_length,
                       uint32_t key,
                       uint32_t rarity,
                       std::vector<location_hash_t> &hashes)
{
    hashes.clear();
    for (uint32_t dx = 0; dx < side_length; ++dx) {
        for (uint32_t dy = 0; dy < side_length; ++dy) {
            int64_t x = bottom_left_x + static_cast<int64_t>(dx);
            int64_t y = bottom_left_y + static_cast<int64_t>(dy);
            mpz_class h = cpu_planet_hash(key, x, y);
            if (is_planet(h, rarity)) {
                hashes.push_back({mpz_to_dec(h), x, y});
            }
        }
    }
}

int poseidon2_self_test()
{
    ensure_cpu_params();

    // Permutation test vector from Barretenberg poseidon2_params.hpp
    std::array<mpz_class, 4> state;
    std::array<mpz_class, 4> expected;
    for (int i = 0; i < 4; ++i) {
        state[i] = limbs_to_mpz(Poseidon2Params::PERM_TEST_IN[i]);
        expected[i] = limbs_to_mpz(Poseidon2Params::PERM_TEST_OUT[i]);
    }
    permutation_inplace(state);
    for (int i = 0; i < 4; ++i) {
        if (state[i] != expected[i]) {
            fprintf(stderr, "poseidon2_self_test: permutation mismatch at %d\n", i);
            fprintf(stderr, "  got %s\n", state[i].get_str(16).c_str());
            fprintf(stderr, "  exp %s\n", expected[i].get_str(16).c_str());
            return 1;
        }
    }

    // Public alignment vectors (invisi / Aztec):
    // hash(1,2) = 0x038682aa...
    {
        auto h = cpu_poseidon2_hash({mpz_class(1), mpz_class(2)});
        mpz_class exp("0x038682aa1cb5ae4e0a3f13da432a95c77c5c111f6f030faf9cad641ce1ed7383");
        if (h != exp) {
            fprintf(stderr, "poseidon2_self_test: hash(1,2) mismatch\n");
            fprintf(stderr, "  got %s\n", h.get_str(16).c_str());
            fprintf(stderr, "  exp %s\n", exp.get_str(16).c_str());
            return 2;
        }
    }
    {
        auto h = cpu_poseidon2_hash({mpz_class(1), mpz_class(2), mpz_class(3), mpz_class(4)});
        mpz_class exp("0x130bf204a32cac1f0ace56c78b731aa3809f06df2731ebcf6b3464a15788b1b9");
        if (h != exp) {
            fprintf(stderr, "poseidon2_self_test: hash(1,2,3,4) mismatch\n");
            fprintf(stderr, "  got %s\n", h.get_str(16).c_str());
            fprintf(stderr, "  exp %s\n", exp.get_str(16).c_str());
            return 3;
        }
    }
    {
        auto h = cpu_poseidon2_hash({mpz_class(0), mpz_class(0)});
        mpz_class exp("0x0b63a53787021a4a962a452c2921b3663aff1ffd8d5510540f8e659e782956f1");
        if (h != exp) {
            fprintf(stderr, "poseidon2_self_test: hash(0,0) mismatch\n");
            fprintf(stderr, "  got %s\n", h.get_str(16).c_str());
            fprintf(stderr, "  exp %s\n", exp.get_str(16).c_str());
            return 4;
        }
    }

    // Negative coordinate Fr encoding
    {
        mpz_class neg1 = to_fr_coord(-1);
        if (neg1 != g_p - 1) {
            fprintf(stderr, "poseidon2_self_test: toFr(-1) mismatch\n");
            return 5;
        }
    }

    // Planet hash with negative coords (checked against @aztec-labs/foundation)
    {
        mpz_class h = cpu_planet_hash(420, -7, -3);
        mpz_class exp(
            "0x18cffbc87718257f37f1d80fa0d9d969fa5174ec62896e97a164ef3b1ab81a85");
        if (h != exp) {
            fprintf(stderr, "poseidon2_self_test: planet(420,-7,-3) mismatch\n");
            fprintf(stderr, "  got %s\n", h.get_str(16).c_str());
            fprintf(stderr, "  exp %s\n", exp.get_str(16).c_str());
            return 6;
        }
    }

    fprintf(stderr, "poseidon2_self_test: OK\n");
    return 0;
}
