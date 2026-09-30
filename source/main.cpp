#include "cache_runner.hpp"
#include "config.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: cache_research.out <config file>\n";
        return 1;
    }
    std::ifstream file(argv[1]);
    Config config;
    if (!read_config(file, config)) {
        std::cerr << "Cannot read config. Expected level count and cache names.\n";
        return 1;
    }

    std::size_t capacity = 0;
    std::size_t request_count = 0;
    if (!(std::cin >> capacity >> request_count) || capacity == 0) {
        std::cerr << "Expected positive cache capacity and request count.\n";
        return 1;
    }
    std::cout << count_hits(config, capacity, request_count, std::cin) << '\n';
}
