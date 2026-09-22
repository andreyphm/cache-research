#include "2Q_cache.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Tests {
namespace {

using StringCache = TWO_Q::Cache<std::string>;

// A miss is observable only through the loader; fetch also inserts the result.
void expect_load(StringCache& cache, const std::string& key,
                 const std::string& value) {
    int calls = 0;
    EXPECT_EQ(cache.fetch(key, [&](const std::string& url) {
        ++calls;
        EXPECT_EQ(url, key);
        return value;
    }), value);
    EXPECT_EQ(calls, 1) << "expected load: " << key;
}

void expect_hit(StringCache& cache, const std::string& key,
                const std::string& value) {
    int calls = 0;
    EXPECT_EQ(cache.fetch(key, [&](const std::string&) {
        ++calls;
        return std::string("unexpected load");
    }), value);
    EXPECT_EQ(calls, 0) << "expected hit: " << key;
}

// A throwing loader observes a miss without inserting or changing ghost history.
struct LoadStopped {};

void expect_miss(StringCache& cache, const std::string& key) {
    int calls = 0;
    EXPECT_THROW((void)cache.fetch(key, [&](const std::string& url) -> std::string {
        ++calls;
        EXPECT_EQ(url, key);
        throw LoadStopped{};
    }), LoadStopped);
    EXPECT_EQ(calls, 1) << "expected miss: " << key;
}

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};
void fill(StringCache& cache) {
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

void fill_frequent(StringCache& cache) {
    fill(cache);
    expect_load(cache, "cold", "cold");
    for (std::size_t key = 0; key < StringCache::capacity - StringCache::kin; ++key) {
        expect_miss(cache, std::to_string(key));
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, miss_loads_and_caches_value) {
    StringCache cache;
    expect_load(cache, "missing", "loaded");
    expect_hit(cache, "missing", "loaded");
}

TEST(TwoQ, hit_does_not_replace_value) {
    StringCache cache;
    expect_load(cache, "A", "original");
    EXPECT_EQ(cache.fetch("A", [](const std::string&) {
        ADD_FAILURE() << "loader called for a resident page";
        return std::string("replacement");
    }), "original");
    expect_hit(cache, "A", "original");
}

TEST(TwoQ, repeated_hit) {
    StringCache cache;
    expect_load(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(TwoQ, empty_key_and_value) {
    StringCache cache;
    expect_load(cache, "", "");
    expect_hit(cache, "", "");
}

TEST(TwoQ, embedded_null_key) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    expect_load(cache, key, value);
    expect_load(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(TwoQ, integer_data) {
    TWO_Q::Cache<int> cache;
    int calls = 0;
    const auto loader = [&](const std::string& key) {
        ++calls;
        return key == "zero" ? 0 : -42;
    };
    for (int access = 0; access < 3; ++access) {
        EXPECT_EQ(cache.fetch("zero", loader), 0);
        EXPECT_EQ(cache.fetch("negative", loader), -42);
    }
    EXPECT_EQ(calls, 2);
}

TEST(TwoQ, non_default_data) {
    TWO_Q::Cache<Payload> cache;
    int calls = 0;
    const auto loader = [&](const std::string&) {
        ++calls;
        return Payload{42};
    };
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(calls, 1);
}

TEST(TwoQ, returned_value_is_a_copy) {
    StringCache cache;
    auto value = cache.fetch("A", [](const std::string&) {
        return std::string("original");
    });
    value = "modified";
    expect_hit(cache, "A", "original");
}

TEST(TwoQ, independent_caches) {
    StringCache first;
    StringCache second;
    expect_load(first, "A", "first");
    expect_load(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(TwoQ, empty_loader_is_only_needed_on_miss) {
    StringCache cache;
    expect_load(cache, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(cache.fetch("A", empty_loader), "value-A");
    EXPECT_THROW((void)cache.fetch("missing", empty_loader), std::bad_function_call);
    expect_load(cache, "missing", "loaded");
}

TEST(TwoQ, destruction_releases_data) {
    std::weak_ptr<int> observer;
    {
        TWO_Q::Cache<std::shared_ptr<int>> cache;
        (void)cache.fetch("A", [&](const std::string&) {
            auto payload = std::make_shared<int>(42);
            observer = payload;
            return payload;
        });
        ASSERT_FALSE(observer.expired());
    }
    EXPECT_TRUE(observer.expired());
}

TEST(TwoQ, fetch_at_capacity) {
    StringCache cache;
    fill(cache);
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, sequential_eviction) {
    StringCache cache;
    for (int key = 0; key < 40; ++key) {
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    for (std::size_t key = 0; key < 40 - StringCache::capacity; ++key) {
        expect_miss(cache, std::to_string(key));
    }
    for (std::size_t key = 40 - StringCache::capacity; key < 40; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, a1in_hit_preserves_fifo) {
    StringCache cache;
    fill(cache);
    for (int access = 0; access < 10; ++access) {
        expect_hit(cache, "0", "value-0");
    }
    expect_load(cache, "new", "new");
    expect_miss(cache, "0");
    expect_hit(cache, "1", "value-1");
}

TEST(TwoQ, ghost_lookup_is_miss) {
    StringCache cache;
    fill(cache);
    expect_load(cache, "new", "new");
    for (int access = 0; access < 10; ++access) {
        expect_miss(cache, "0");
    }
}

TEST(TwoQ, ghost_reload_promotes_page) {
    StringCache cache;
    fill(cache);
    expect_load(cache, "new", "new");
    expect_miss(cache, "0");
    expect_load(cache, "0", "reloaded");
    expect_hit(cache, "0", "reloaded");
    expect_miss(cache, "1");
    for (int key = 0; key < 40; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_hit(cache, "0", "reloaded");
}

TEST(TwoQ, forgotten_ghost_returns_to_a1in) {
    StringCache cache;
    fill(cache);
    for (std::size_t key = 0; key <= StringCache::kout; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_load(cache, "0", "reloaded");
    expect_hit(cache, "0", "reloaded");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, "0");
}

TEST(TwoQ, ghost_hit_preserves_fifo) {
    StringCache cache;
    fill(cache);
    expect_load(cache, "first", "first");
    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_miss(cache, "0");
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_load(cache, "0", "reloaded");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, "0");
}

TEST(TwoQ, newest_ghost_survives_history_limit) {
    StringCache cache;
    fill(cache);
    for (std::size_t key = 0; key <= StringCache::kout; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    const auto key = std::to_string(StringCache::kout);
    expect_miss(cache, key);
    expect_load(cache, key, "reloaded");
    for (std::size_t index = 0; index < StringCache::capacity; ++index) {
        expect_load(cache, "next-" + std::to_string(index), "next");
    }
    expect_hit(cache, key, "reloaded");
}

TEST(TwoQ, ghost_reload_at_kin_evicts_am) {
    StringCache cache;
    fill_frequent(cache);
    expect_miss(cache, "6");
    expect_load(cache, "6", "reloaded");
    expect_miss(cache, "0");
    expect_hit(cache, "6", "reloaded");
    expect_hit(cache, "7", "value-7");
    expect_hit(cache, "cold", "cold");
}

TEST(TwoQ, oldest_ghost_reload_at_full_history_preserves_other_ghosts) {
    StringCache cache;
    fill(cache);
    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }

    // Restoring the oldest ghost frees its history slot for the evicted page.
    // Every other ghost must still be promoted on its next successful load.
    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_load(cache, std::to_string(key), "fresh-" + std::to_string(key));
    }
    for (std::size_t key = 0; key < 2 * StringCache::capacity; ++key) {
        expect_load(cache, "next-" + std::to_string(key), "next");
    }
    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_hit(cache, std::to_string(key), "fresh-" + std::to_string(key));
    }
}

TEST(TwoQ, failed_ghost_reload_preserves_residents_and_promotion) {
    StringCache cache;
    fill_frequent(cache);
    // A1in is at kin: a successful restore would evict the oldest Am page.
    expect_miss(cache, "6");
    expect_miss(cache, "6");
    for (std::size_t key = 0; key < StringCache::capacity - StringCache::kin; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    expect_hit(cache, "7", "value-7");
    expect_hit(cache, "cold", "cold");

    expect_load(cache, "6", "recovered");
    expect_miss(cache, "0");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_hit(cache, "6", "recovered");
}

TEST(TwoQ, evicted_am_page_reloads_into_a1in) {
    StringCache cache;
    fill_frequent(cache);
    expect_load(cache, "6", "reloaded-6");
    expect_load(cache, "0", "reloaded-0");
    expect_hit(cache, "0", "reloaded-0");

    // Am eviction leaves no ghost entry, so reloading 0 must not protect it
    // from a sequential scan as a ghost promotion would.
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_miss(cache, "0");
    expect_hit(cache, "6", "reloaded-6");
}

TEST(TwoQ, loader_exception_preserves_residents) {
    StringCache cache;
    fill(cache);
    expect_miss(cache, "failed-load");
    expect_miss(cache, "failed-load");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    expect_load(cache, "failed-load", "recovered");
    expect_hit(cache, "failed-load", "recovered");
}

TEST(TwoQ, ghost_releases_data_and_reload_caches_fresh_value) {
    TWO_Q::Cache<std::shared_ptr<int>> cache;
    std::weak_ptr<int> observer;
    (void)cache.fetch("0", [&](const std::string&) {
        auto payload = std::make_shared<int>(42);
        observer = payload;
        return payload;
    });
    const auto loader = [](const std::string&) { return std::make_shared<int>(1); };
    for (std::size_t key = 1; key < StringCache::capacity; ++key) {
        (void)cache.fetch(std::to_string(key), loader);
    }
    (void)cache.fetch(std::to_string(StringCache::capacity - 1), loader);
    ASSERT_FALSE(observer.expired());
    (void)cache.fetch("new", loader);
    EXPECT_TRUE(observer.expired()) << "ghost retained resident payload";
    int calls = 0;
    const auto reload = [&](const std::string&) {
        ++calls;
        return std::make_shared<int>(7);
    };
    auto result = cache.fetch("0", reload);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 7);
    EXPECT_EQ(cache.fetch("0", reload), result);
    EXPECT_EQ(calls, 1);
}

TEST(TwoQ, am_hit_refreshes_recency) {
    StringCache cache;
    fill_frequent(cache);
    expect_hit(cache, "0", "value-0");
    expect_load(cache, "new", "new");
    expect_miss(cache, "1");
    expect_hit(cache, "0", "value-0");
}

class Workload {
public:
    void put(const std::string& key) {
        const auto value = key + "-revision-" + std::to_string(revision_++);
        expect_load(cache_, key, value);
        if (values_.contains(key)) {
            refresh_frequent(key);
        } else {
            const auto ghost = std::find(history_.begin(), history_.end(), key);
            const bool reload = ghost != history_.end();
            if (reload) {
                history_.erase(ghost);
            }
            if (values_.size() == StringCache::capacity) {
                if (incoming_.size() > StringCache::kin) {
                    history_.push_back(incoming_.front());
                    values_.erase(incoming_.front());
                    incoming_.pop_front();
                    if (history_.size() > StringCache::kout) {
                        history_.pop_front();
                    }
                } else {
                    ASSERT_FALSE(frequent_.empty()) << "reference frequent queue is empty";
                    values_.erase(frequent_.front());
                    frequent_.pop_front();
                }
            }
            (reload ? frequent_ : incoming_).push_back(key);
        }
        values_[key] = value;
    }

    bool get(const std::string& key) {
        const auto found = values_.find(key);
        if (found == values_.end()) {
            return false;
        }
        expect_hit(cache_, key, found->second);
        refresh_frequent(key);
        return true;
    }

    void access(const std::string& key) {
        SCOPED_TRACE("access " + std::to_string(access_count_++) + ": " + key);
        if (!get(key)) {
            put(key);
        }
    }

private:
    void refresh_frequent(const std::string& key) {
        const auto found = std::find(frequent_.begin(), frequent_.end(), key);
        if (found != frequent_.end()) {
            frequent_.erase(found);
            frequent_.push_back(key);
        }
    }

    StringCache cache_;
    std::deque<std::string> incoming_;
    std::deque<std::string> history_;
    std::deque<std::string> frequent_;
    std::unordered_map<std::string, std::string> values_;
    std::size_t revision_ = 0;
    std::size_t access_count_ = 0;
};

TEST(TwoQ, repeated_reloads) {
    Workload workload;
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (int key = 0; key < 12; ++key) {
            workload.access(std::to_string(key));
        }
    }
}

TEST(TwoQ, mixed_workload) {
    Workload workload;
    std::uint32_t state = 0x12345678U;
    for (int step = 0; step < 4000; ++step) {
        state = state * 1664525U + 1013904223U;
        const auto key = std::to_string((state >> 16U) % 23U);
        workload.access(key);
    }
}

TEST(TwoQ, hot_and_cold_workload) {
    Workload workload;
    for (int step = 0; step < 200; ++step) {
        workload.access("hot-A");
        workload.access("hot-B");
        workload.access("cold-" + std::to_string(step));
        workload.access("hot-A");
    }
    for (int step = 0; step < 200; ++step) {
        workload.access("cold-" + std::to_string(step));
    }
}

} // namespace
} // namespace Tests
