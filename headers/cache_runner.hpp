#pragma once

#include "config.hpp"

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

std::size_t count_hits(const Config& config,
                       const std::vector<std::size_t>& capacities,
                       std::size_t request_count, std::istream& input);

std::size_t count_hits(const Config& config,
                       const std::vector<std::size_t>& capacities,
                       const std::vector<std::string>& requests);
