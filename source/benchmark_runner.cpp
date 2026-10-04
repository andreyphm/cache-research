#include "benchmark_runner.hpp"
#include "cache_runner.hpp"
#include "belady_cache.hpp"
#include "SlowGetPage.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

void run_benchmark(std::istream& input, std::ostream& output) {
    std::vector<std::size_t> capacities(3);
    std::size_t count = 0;
    if (!(input >> capacities[0] >> capacities[1] >> capacities[2] >> count) ||
        capacities[0] == 0 || capacities[1] == 0 || capacities[2] == 0 ||
        capacities[0] > capacities[1] || capacities[1] > capacities[2]) {
        throw std::invalid_argument("Invalid trace header");
    }
    std::vector<std::string> requests(count);
    for (auto& key : requests) {
        if (!(input >> key)) {
            throw std::invalid_argument("Incomplete trace");
        }
    }
    std::string extra;
    if (input >> extra) {
        throw std::invalid_argument("Unexpected keys after trace");
    }
    std::size_t misses = 0;
    SlowGetPage<std::string> storage;
    storage.load = [&misses](const std::string& key) {
        ++misses;
        return key;
    };
    Belady::Cache<std::string, SlowGetPage<std::string>> ideal(storage, capacities.back(), requests);
    for (const auto& key : requests) {
        ideal.fetch(key);
    }
    const auto belady_hits = count - misses;

    constexpr std::array policies{CachePolicy::LFU, CachePolicy::ARC,
                                  CachePolicy::TWO_Q, CachePolicy::LIRS};
    constexpr std::array names{"LFU", "ARC", "2Q", "LIRS"};
    output << "capacity,l1_capacity,l2_capacity,l3_capacity,"
              "l1,l2,l3,hits,l1_hits,l2_hits,l3_hits,belady_hits\n";

    for (std::size_t i = 0; i < policies.size(); ++i) {
        for (std::size_t j = 0; j < policies.size(); ++j) {
            for (std::size_t k = 0; k < policies.size(); ++k) {
                Config config{{policies[i], policies[j], policies[k]}};
                const auto statistics = count_hits_by_level(config, capacities, requests);
                output << capacities[0] << ',' << capacities[0] << ','
                       << capacities[1] << ',' << capacities[2] << ','
                       << names[i] << ',' << names[j] << ',' << names[k] << ','
                       << statistics.total_hits() << ','
                       << statistics.level_hits[0] << ','
                       << statistics.level_hits[1] << ','
                       << statistics.level_hits[2] << ','
                       << belady_hits << '\n';
            }
        }
    }
}
