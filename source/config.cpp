#include "config.hpp"

#include <cstddef>
#include <string>

bool read_config(std::istream& input, Config& config) {
    std::size_t count = 0;
    if (!(input >> count) || count == 0) {
        return false;
    }

    config.levels.clear();
    for (std::size_t i = 0; i < count; ++i) {
        std::string name;
        input >> name;
        if (name == "LFU") {
            config.levels.push_back(CachePolicy::LFU);
        } else if (name == "ARC") {
            config.levels.push_back(CachePolicy::ARC);
        } else if (name == "2Q") {
            config.levels.push_back(CachePolicy::TWO_Q);
        } else if (name == "LIRS") {
            config.levels.push_back(CachePolicy::LIRS);
        } else {
            return false;
        }
    }
    return true;
}
