#pragma once

#include "config.hpp"

#include <cstddef>
#include <istream>

std::size_t count_hits(const Config& config, std::size_t capacity,
                       std::size_t request_count, std::istream& input);
