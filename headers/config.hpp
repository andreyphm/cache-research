#pragma once

#include <istream>
#include <vector>

enum class CachePolicy { LFU, ARC, TWO_Q, LIRS };

struct Config {
    std::vector<CachePolicy> levels;
};

bool read_config(std::istream& input, Config& config);
