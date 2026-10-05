#include "cache_runner.hpp"
#include "config.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

int run_cache_command(const char* const path) {
    std::ifstream file(path);
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

    try {
        const auto requests = read_requests(std::cin, request_count);
        std::cout << count_hits(config, capacities, requests) << '\n';
    } catch (const std::invalid_argument& error) {
        std::cerr << "Invalid cache configuration: " << error.what() << '\n';
        return 1;
    } catch (const std::runtime_error& error) {
        std::cerr << "Invalid request sequence: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

void print_usage() {
    std::cerr << "Usage: cache_research.out <config file>\n";
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc == 2) {
        return run_cache_command(argv[1]);
    }

    print_usage();
    return 1;
}
