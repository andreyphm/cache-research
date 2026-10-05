#include "benchmark_runner.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

int run_benchmark_command(const char* const path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "Cannot open benchmark trace\n";
        return 1;
    }

    try {
        run_benchmark(input, std::cout);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Invalid benchmark trace: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

void print_usage() {
    std::cerr << "Usage: cache_benchmark.out <trace file>\n";
}

} // namespace

int main(const int argc, char* argv[]) {
    if (argc == 2) {
        return run_benchmark_command(argv[1]);
    }

    print_usage();
    return 1;
}
