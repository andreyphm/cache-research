#include "cache_runner.hpp"
#include "ARC_cache.hpp"
#include "LFU_cache.hpp"
#include "2Q_cache.hpp"
#include "LIRS_cache.hpp"
#include "SlowGetPage.hpp"

#include <deque>
#include <string>
#include <type_traits>
#include <variant>

namespace {

struct Level {
    using Storage = SlowGetPage<std::string>;
    using Lfu = LFU::Cache<std::string, Level>;
    using Arc = ARC::Cache<std::string, Level>;
    using TwoQ = TWO_Q::Cache<std::string, Level>;
    using Lirs = LIRS::Cache<std::string, Level>;

    std::variant<Storage, Lfu, Arc, TwoQ, Lirs> cache;
    Level* lower = nullptr;

    Level(std::size_t& misses) {
        std::get<Storage>(cache).load = [&misses](const std::string& key) {
            ++misses;
            return key;
        };
    }

    Level(CachePolicy policy, std::size_t capacity, Level& lower_level)
        : lower(&lower_level) {
        switch (policy) {
            case CachePolicy::LFU:
                cache.emplace<Lfu>(lower_level, capacity);
                break;
            case CachePolicy::ARC:
                cache.emplace<Arc>(lower_level, capacity);
                break;
            case CachePolicy::TWO_Q:
                cache.emplace<TwoQ>(lower_level, capacity);
                break;
            case CachePolicy::LIRS:
                cache.emplace<Lirs>(lower_level, capacity);
                break;
        }
    }   

    std::string fetch(const std::string& key) {
        return std::visit([&](auto& value) { return value.fetch(key); }, cache);
    }

    void remove(const std::string& key) {
        std::visit([&](auto& value) { value.remove(key); }, cache);
    }

    void insert(const std::string& key, const std::string& data) {
        std::visit([&](auto& value) {
            if constexpr (!std::is_same_v<std::decay_t<decltype(value)>, Storage>) {
                auto evicted = value.insert(key, data);
                if (evicted) {
                    lower->insert(evicted->first, evicted->second);
                }
            }
        }, cache);
    }
};

} // namespace

std::size_t count_hits(const Config& config, std::size_t capacity,
                       std::size_t request_count, std::istream& input) {
    std::size_t misses = 0;
    std::deque<Level> levels;
    levels.emplace_front(misses);
    for (auto policy = config.levels.rbegin(); policy != config.levels.rend(); ++policy) {
        levels.emplace_front(*policy, capacity, levels.front());
    }

    for (std::size_t i = 0; i < request_count; ++i) {
        std::string key;
        input >> key;
        levels.front().fetch(key);
    }
    return request_count - misses;
}
