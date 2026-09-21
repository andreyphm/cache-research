#include "LFU_cache.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Tests {
namespace {

using StringCache = LFU::Cache<std::string>;

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

void fill(StringCache& cache) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_load(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, miss_loads_and_caches_value) {
    StringCache cache;
    expect_load(cache, "missing", "loaded");
    expect_hit(cache, "missing", "loaded");
}

TEST(LFU, hit_does_not_replace_value) {
    StringCache cache;
    expect_load(cache, "A", "original");
    EXPECT_EQ(cache.fetch("A", [](const std::string&) {
        ADD_FAILURE() << "loader called for a resident page";
        return std::string("replacement");
    }), "original");
    expect_hit(cache, "A", "original");
}

TEST(LFU, repeated_hit) {
    StringCache cache;
    expect_load(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(LFU, hit_at_capacity_does_not_evict) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "B", "value-B");
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, empty_key_and_value) {
    StringCache cache;
    expect_load(cache, "", "");
    expect_hit(cache, "", "");
}

TEST(LFU, embedded_null_key) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    expect_load(cache, key, value);
    expect_load(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(LFU, integer_data) {
    LFU::Cache<int> cache;
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

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};

TEST(LFU, non_default_data) {
    LFU::Cache<Payload> cache;
    int calls = 0;
    const auto loader = [&](const std::string&) {
        ++calls;
        return Payload{42};
    };
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(calls, 1);
}

TEST(LFU, returned_value_is_a_copy) {
    StringCache cache;
    auto value = cache.fetch("A", [](const std::string&) {
        return std::string("original");
    });
    value = "modified";
    expect_hit(cache, "A", "original");
}

TEST(LFU, independent_caches) {
    StringCache first;
    StringCache second;
    expect_load(first, "A", "first");
    expect_load(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(LFU, sequential_eviction) {
    StringCache cache;
    for (int key = 0; key < 40; ++key) {
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    // Check residents first: checking a miss would itself change the cache.
    for (int key = 36; key < 40; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    for (int key = 0; key < 36; ++key) {
        expect_load(cache, std::to_string(key), "reloaded-" + std::to_string(key));
    }
}

TEST(LFU, promotion_protects_page) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    expect_load(cache, "E", "value-E");
    for (const auto* key : {"A", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "B", "reloaded-B");
}

TEST(LFU, frequency_beats_recency) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    for (const auto* key : {"E", "F", "G", "H"}) {
        expect_load(cache, key, std::string("value-") + key);
    }
    for (const auto* key : {"A", "F", "G", "H"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_load(cache, key, std::string("reloaded-") + key);
    }
}

TEST(LFU, equal_frequency_evicts_oldest_page) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    expect_hit(cache, "B", "value-B");
    expect_load(cache, "E", "value-E");
    expect_load(cache, "F", "value-F");
    for (const auto* key : {"A", "B", "E", "F"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "C", "reloaded-C");
    expect_load(cache, "D", "reloaded-D");
}

TEST(LFU, all_pages_frequent_admit_new_page) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    // All residents have frequency 2; A is the oldest and leaves before E enters.
    expect_load(cache, "E", "value-E");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "A", "reloaded-A");
}

TEST(LFU, reload_uses_fresh_data_and_resets_frequency) {
    StringCache cache;
    fill(cache);
    expect_load(cache, "E", "value-E");
    expect_load(cache, "A", "reloaded-A");
    for (const auto* key : {"F", "G", "H", "I"}) {
        expect_load(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "A", "latest-A");
    expect_hit(cache, "A", "latest-A");
}

TEST(LFU, loader_exception_does_not_change_cache) {
    StringCache cache;
    fill(cache);
    int calls = 0;
    const auto failing_loader = [&](const std::string& key) -> std::string {
        ++calls;
        EXPECT_EQ(key, "E");
        throw std::runtime_error("load failed");
    };
    EXPECT_THROW((void)cache.fetch("E", failing_loader), std::runtime_error);
    EXPECT_THROW((void)cache.fetch("E", failing_loader), std::runtime_error);
    EXPECT_EQ(calls, 2);
    // A must still be the oldest page at frequency 1 after the failed loads.
    expect_load(cache, "E", "value-E");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "A", "reloaded-A");
}

TEST(LFU, empty_loader_is_only_needed_on_miss) {
    StringCache cache;
    expect_load(cache, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(cache.fetch("A", empty_loader), "value-A");
    EXPECT_THROW((void)cache.fetch("missing", empty_loader), std::bad_function_call);
    expect_load(cache, "missing", "loaded");
}

TEST(LFU, eviction_releases_data) {
    LFU::Cache<std::shared_ptr<int>> cache;
    std::weak_ptr<int> observer;
    (void)cache.fetch("A", [&](const std::string&) {
        auto payload = std::make_shared<int>(42);
        observer = payload;
        return payload;
    });
    ASSERT_FALSE(observer.expired());
    for (const auto* key : {"B", "C", "D", "E"}) {
        (void)cache.fetch(key, [](const std::string&) { return std::make_shared<int>(1); });
    }
    EXPECT_TRUE(observer.expired());
}

TEST(LFU, admitted_page_lives_until_eviction) {
    LFU::Cache<std::shared_ptr<int>> cache;
    const auto loader = [](const std::string&) { return std::make_shared<int>(1); };
    for (const auto* key : {"A", "B", "C", "D"}) {
        (void)cache.fetch(key, loader);
        (void)cache.fetch(key, loader);
    }
    auto result = cache.fetch("E", [](const std::string&) { return std::make_shared<int>(42); });
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 42);
    const std::weak_ptr<int> observer = result;
    result.reset();
    EXPECT_FALSE(observer.expired()) << "cache must retain the admitted page";
    // E has frequency 1, while B, C and D have frequency 2.
    (void)cache.fetch("F", loader);
    EXPECT_TRUE(observer.expired());
}

TEST(LFU, destruction_releases_data) {
    std::weak_ptr<int> observer;
    {
        LFU::Cache<std::shared_ptr<int>> cache;
        (void)cache.fetch("A", [&](const std::string&) {
            auto payload = std::make_shared<int>(42);
            observer = payload;
            return payload;
        });
        ASSERT_FALSE(observer.expired());
    }
    EXPECT_TRUE(observer.expired());
}

// Independent reference model: frequency, then time of last access.
// Evict a resident before admitting a new page with frequency 1.
class Workload {
public:
    void access(const std::string& key) {
        SCOPED_TRACE("step=" + std::to_string(clock_) + " key=" + key);
        const auto found = entries_.find(key);
        if (found != entries_.end()) {
            expect_hit(cache_, key, found->second.value_);
            ++found->second.frequency_;
            found->second.last_access_ = ++clock_;
            return;
        }
        const auto value = key + "-revision-" + std::to_string(++clock_);
        expect_load(cache_, key, value);
        if (entries_.size() == StringCache::capacity) {
            auto victim = entries_.begin();
            for (auto candidate = entries_.begin(); candidate != entries_.end(); ++candidate) {
                const auto& a = candidate->second;
                const auto& b = victim->second;
                if (a.frequency_ < b.frequency_ ||
                    (a.frequency_ == b.frequency_ && a.last_access_ < b.last_access_)) {
                    victim = candidate;
                }
            }
            entries_.erase(victim);
        }
        entries_.emplace(key, Entry{value, 1, clock_});
    }

private:
    struct Entry {
        std::string value_;
        std::size_t frequency_;
        std::size_t last_access_;
    };
    StringCache cache_;
    std::unordered_map<std::string, Entry> entries_;
    std::size_t clock_ = 0;
};

TEST(LFU, repeated_reloads) {
    Workload workload;
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (const auto* key : {"A", "B", "C", "D", "E", "A", "F", "B"}) {
            workload.access(key);
        }
    }
}

TEST(LFU, mixed_workload) {
    Workload workload;
    std::uint32_t state = 0x12345678U;
    for (int step = 0; step < 4000; ++step) {
        state = state * 1664525U + 1013904223U;
        workload.access(std::to_string((state >> 16U) % 17U));
    }
}

TEST(LFU, hot_and_cold_workload) {
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
