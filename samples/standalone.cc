#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "darkforest.h"
#include "poseidon2.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

double time_it(const std::function<void()> &func)
{
    using clock = std::chrono::steady_clock;
    auto start = clock::now();
    func();
    auto end = clock::now();
    double time_use = std::chrono::duration<double>(end - start).count();
    printf("explore time : %lf sec\n", time_use);
    return time_use;
}

static std::string loc_key(int64_t x, int64_t y)
{
    return std::to_string(x) + "," + std::to_string(y);
}

int main()
{
    init_device();

    // CPU vs GPU equality on a small chunk
    {
        const int64_t bx = -32;
        const int64_t by = -16;
        const uint32_t side = 64; // below server min; direct API for test
        const uint32_t key = 420;
        const uint32_t rarity = 16384;

        std::vector<location_hash_t> cpu_hits;
        std::vector<location_hash_t> gpu_hits;
        cpu_explore_chunk(bx, by, side, key, rarity, cpu_hits);
        gpu_explore_chunk(bx, by, side, key, rarity, gpu_hits);

        std::map<std::string, std::string> cpu_map;
        for (auto &p : cpu_hits) {
            cpu_map[loc_key(p.x, p.y)] = p.hash;
        }
        std::map<std::string, std::string> gpu_map;
        for (auto &p : gpu_hits) {
            gpu_map[loc_key(p.x, p.y)] = p.hash;
        }

        if (cpu_map != gpu_map) {
            fprintf(stderr, "FAIL: CPU/GPU explore mismatch (cpu=%zu gpu=%zu)\n",
                    cpu_map.size(), gpu_map.size());
            for (auto &kv : cpu_map) {
                auto it = gpu_map.find(kv.first);
                if (it == gpu_map.end()) {
                    fprintf(stderr, "  missing on GPU: %s hash=%s\n", kv.first.c_str(), kv.second.c_str());
                } else if (it->second != kv.second) {
                    fprintf(stderr, "  hash mismatch %s cpu=%s gpu=%s\n",
                            kv.first.c_str(), kv.second.c_str(), it->second.c_str());
                }
            }
            for (auto &kv : gpu_map) {
                if (!cpu_map.count(kv.first)) {
                    fprintf(stderr, "  extra on GPU: %s hash=%s\n", kv.first.c_str(), kv.second.c_str());
                }
            }
            return 1;
        }
        fprintf(stderr, "CPU/GPU explore match: %zu planets in %ux%u\n",
                cpu_map.size(), side, side);

        // Negative-coord planet hash spot check
        mpz_class hneg = cpu_planet_hash(key, -7, -3);
        fprintf(stderr, "planet_hash(420,-7,-3)=%s\n", hneg.get_str(16).c_str());

        // Known first-planet hash (bb.js / CPU golden)
        mpz_class h225 = cpu_planet_hash(420, -225, 198);
        std::vector<location_hash_t> one;
        gpu_explore_chunk(-225, 198, 1, 420, 1, one);
        if (one.empty() || one[0].hash != h225.get_str(10)) {
            fprintf(stderr, "FAIL: CPU/GPU hash mismatch for (-225,198)\n");
            if (!one.empty()) {
                fprintf(stderr, "  cpu=%s\n  gpu=%s\n", h225.get_str(10).c_str(), one[0].hash.c_str());
            }
            return 3;
        }
        fprintf(stderr, "CPU/GPU single-point hash OK: %s\n", one[0].hash.c_str());
    }

    // Larger chunk with planets: full CPU/GPU equality
    {
        const int64_t bx = -256;
        const int64_t by = 0;
        const uint32_t side = 256;
        const uint32_t key = 420;
        const uint32_t rarity = 16384;
        std::vector<location_hash_t> cpu_hits, gpu_hits;
        cpu_explore_chunk(bx, by, side, key, rarity, cpu_hits);
        gpu_explore_chunk(bx, by, side, key, rarity, gpu_hits);
        std::map<std::string, std::string> cpu_map, gpu_map;
        for (auto &p : cpu_hits) cpu_map[loc_key(p.x, p.y)] = p.hash;
        for (auto &p : gpu_hits) gpu_map[loc_key(p.x, p.y)] = p.hash;
        if (cpu_map != gpu_map) {
            fprintf(stderr, "FAIL: 256x256 CPU/GPU mismatch cpu=%zu gpu=%zu\n",
                    cpu_map.size(), gpu_map.size());
            return 4;
        }
        fprintf(stderr, "CPU/GPU 256x256 match: %zu planets\n", cpu_map.size());
    }

    darkforest::ExploreResult result;
    darkforest::ExploreTask task = json::parse(
        R"({"chunkFootprint":{"bottomLeft":{"x":-256,"y":0},"sideLength":256},"planetRarity":16384,"planetHashKey":420})");
    json req = task;
    printf("explore task: %s\n", req.dump().c_str());

    double time_use = time_it(std::bind(darkforest::explore_chunk, task, std::ref(result)));

    json r = result;
    printf("explore result planets: %zu\n", result.planetLocations.size());
    if (!result.planetLocations.empty()) {
        printf("first planet: %s\n", json(result.planetLocations[0]).dump().c_str());
    }

    int64_t count = task.chunkFootprint.sideLength * task.chunkFootprint.sideLength;
    printf("explore rate: %.0f H/s\n", count / time_use);
    clear_device();
    return 0;
}
