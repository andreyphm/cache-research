#include "cache_runner.hpp"
#include "ARC_cache.hpp"
#include "LFU_cache.hpp"
#include "2Q_cache.hpp"
#include "LIRS_cache.hpp"
#include "SlowGetPage.hpp"

#include <deque>
#include <numeric>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

struct Level {
    using Storage = SlowGetPage<std::string>;
    using Lfu = LFU::Cache<std::string, Level>;
    using Arc = ARC::Cache<std::string, Level>;
    using TwoQ = TWO_Q::Cache<std::string, Level>;
    using Lirs = LIRS::Cache<std::string, Level>;

    std::variant<Storage, Lfu, Arc, TwoQ, Lirs> cache;
    Level* upper = nullptr;
    Level* lower = nullptr;
    std::size_t* hit_count = nullptr;
    std::size_t fetch_count = 0;

    Level(std::size_t& misses) {
        std::get<Storage>(cache).load = [&misses](const std::string& key) {
            ++misses;
            return key;
        };
    }

    Level(CachePolicy policy, std::size_t capacity, Level& lower_level, std::size_t& level_hits)
        : lower(&lower_level), hit_count(&level_hits) {
        lower_level.upper = this;
        const auto invalidate_upper = [this](const std::string& key) {
            if (upper) {
                upper->remove(key);
            }
        };
        switch (policy) {
            case CachePolicy::LFU:
                cache.emplace<Lfu>(lower_level, capacity, invalidate_upper);
                break;
            case CachePolicy::ARC:
                cache.emplace<Arc>(lower_level, capacity, invalidate_upper);
                break;
            case CachePolicy::TWO_Q:
                cache.emplace<TwoQ>(lower_level, capacity, invalidate_upper);
                break;
            case CachePolicy::LIRS:
                cache.emplace<Lirs>(lower_level, capacity, invalidate_upper);
                break;
        }
    }

    std::string fetch(const std::string& key) {
        ++fetch_count;
        if (!lower) {
            return std::visit([&](auto& value) { return value.fetch(key); }, cache);
        }
        const auto lower_fetches = lower->fetch_count;
        const auto data = std::visit([&](auto& value) { return value.fetch(key); }, cache);
        if (lower->fetch_count == lower_fetches) {
            ++(*hit_count);
        }
        return data;
    }

    void remove(const std::string& key) {
        std::visit([&](auto& value) { value.remove(key); }, cache);
    }
};

} // namespace

std::vector<std::string> read_requests(std::istream& input,
                                       std::size_t request_count) {
    std::vector<std::string> requests(request_count);
    for (auto& key : requests) {
        if (!(input >> key)) {
            throw std::runtime_error("Incomplete request sequence");
        }
    }
    return requests;
}

CacheHitStatistics count_hits_by_level(const Config& config, const std::vector<std::size_t>& capacities,
                                       const std::vector<std::string>& requests) {
    if (capacities.size() != config.levels.size()) {
        throw std::invalid_argument("Capacity count must match level count");
    }
    for (std::size_t i = 0; i < capacities.size(); ++i) {
        if (capacities[i] == 0) {
            throw std::invalid_argument("Cache capacity must be positive");
        }
        if (i > 0 && capacities[i - 1] > capacities[i]) {
            throw std::invalid_argument("Inclusive cache capacities must not decrease");
        }
    }

    CacheHitStatistics statistics;
    statistics.level_hits.resize(config.levels.size());
    std::deque<Level> levels;
    levels.emplace_front(statistics.storage_misses);
    for (std::size_t i = config.levels.size(); i > 0; --i) {
        levels.emplace_front(config.levels[i - 1], capacities[i - 1],
                             levels.front(), statistics.level_hits[i - 1]);
    }

    for (const auto& key : requests) {
        levels.front().fetch(key);
    }
    return statistics;
}

std::size_t count_hits(const Config& config, const std::vector<std::size_t>& capacities,
                       const std::vector<std::string>& requests) {
    return count_hits_by_level(config, capacities, requests).total_hits();
}

std::size_t CacheHitStatistics::total_hits() const {
    return std::accumulate(level_hits.begin(), level_hits.end(), std::size_t{0});
}
