#include <chrono>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "darkforest.h"
#include "poseidon2.h"

static std::string loc_key(int64_t x, int64_t y)
{
    return std::to_string(x) + "," + std::to_string(y);
}

static int compare_chunk(int64_t bx, int64_t by, uint32_t side, uint32_t key, uint32_t rarity)
{
    using clock = std::chrono::steady_clock;

    std::vector<location_hash_t> cpu_hits, gpu_hits;

    auto t0 = clock::now();
    cpu_explore_chunk(bx, by, side, key, rarity, cpu_hits);
    auto t1 = clock::now();
    gpu_explore_chunk(bx, by, side, key, rarity, gpu_hits);
    auto t2 = clock::now();

    double cpu_s = std::chrono::duration<double>(t1 - t0).count();
    double gpu_s = std::chrono::duration<double>(t2 - t1).count();
    double cells = (double)side * (double)side;

    std::map<std::string, std::string> cpu_map, gpu_map;
    for (auto &p : cpu_hits) cpu_map[loc_key(p.x, p.y)] = p.hash;
    for (auto &p : gpu_hits) gpu_map[loc_key(p.x, p.y)] = p.hash;

    printf("chunk (%lld,%lld) %ux%u: cpu=%zu gpu=%zu  cpu=%.2fs (%.0f H/s) gpu=%.3fs (%.0f H/s)\n",
           (long long)bx, (long long)by, side, side,
           cpu_map.size(), gpu_map.size(),
           cpu_s, cells / cpu_s, gpu_s, cells / gpu_s);

    if (cpu_map == gpu_map) {
        printf("  OK match\n");
        return 0;
    }

    printf("  FAIL mismatch\n");
    for (auto &kv : cpu_map) {
        auto it = gpu_map.find(kv.first);
        if (it == gpu_map.end()) {
            printf("    missing on GPU: %s hash=%s\n", kv.first.c_str(), kv.second.c_str());
        } else if (it->second != kv.second) {
            printf("    hash mismatch %s\n      cpu=%s\n      gpu=%s\n",
                   kv.first.c_str(), kv.second.c_str(), it->second.c_str());
        }
    }
    for (auto &kv : gpu_map) {
        if (!cpu_map.count(kv.first)) {
            printf("    extra on GPU: %s hash=%s\n", kv.first.c_str(), kv.second.c_str());
        }
    }
    return 1;
}

int main()
{
    init_device();

    const uint32_t key = 420;
    const uint32_t rarity = 16384;
    const int64_t coords[3][2] = {
        {-256, 0},
        {-512, -512},
        {1024, -2048},
    };
    const uint32_t sides[2] = {512, 1024};

    int failed = 0;
    for (int c = 0; c < 3; ++c) {
        for (int s = 0; s < 2; ++s) {
            failed += compare_chunk(coords[c][0], coords[c][1], sides[s], key, rarity);
        }
    }

    clear_device();
    if (failed) {
        fprintf(stderr, "FAILED %d chunk comparisons\n", failed);
        return 1;
    }
    printf("ALL OK: 3 coords x {512,1024} CPU==GPU\n");
    return 0;
}
