#include "cache_runner.hpp"
#include "config.hpp"
#include "benchmark_runner.hpp"

#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc == 3 && std::string_view(argv[1]) == "--benchmark") {
        std::ifstream input(argv[2]);
        if (!input) {
            std::cerr << "Cannot open benchmark trace\n";
            return 1;
        }
        run_benchmark(input, std::cout);
        return 0;
    }

    if (argc != 2 || std::string_view(argv[1]) == "--benchmark") {
        std::cerr << "Usage: cache_research.out <config file>\n"
                  << "For benchmark: cache_research.out --benchmark <trace.txt>\n";
        return 1;
    }
    std::ifstream file(argv[1]);
    Config config;
    if (!read_config(file, config)) {
        std::cerr << "Cannot read config. Expected level count and cache names.\n";
        return 1;
    }

    std::vector<std::size_t> capacities(config.levels.size());
    std::size_t request_count = 0;
    for (auto& capacity : capacities) {
        if (!(std::cin >> capacity) || capacity == 0) {
            std::cerr << "Expected one positive capacity per cache level.\n";
            return 1;
        }
    }
    if (!(std::cin >> request_count)) {
        std::cerr << "Expected request count after cache capacities.\n";
        return 1;
    }

    std::cout << count_hits(config, capacities, request_count, std::cin) << '\n';
    return 0;
}
