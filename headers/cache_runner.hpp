#pragma once

#include "config.hpp"

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

struct CacheHitStatistics {
    std::vector<std::size_t> level_hits;
    std::size_t storage_misses = 0;

    std::size_t total_hits() const;
};

std::vector<std::string> read_requests(std::istream& input,
                                       std::size_t request_count);

CacheHitStatistics count_hits_by_level(const Config& config, const std::vector<std::size_t>& capacities,
                                       const std::vector<std::string>& requests);

std::size_t count_hits(const Config& config,
                       const std::vector<std::size_t>& capacities,
                       const std::vector<std::string>& requests);
