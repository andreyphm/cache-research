#include "ARC_cache.hpp"

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

using StringCache = ARC::Cache<std::string>;

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
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_load(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, miss_loads_and_caches_value) {
    StringCache cache;
    expect_load(cache, "missing", "loaded");
    expect_hit(cache, "missing", "loaded");
}

TEST(ARC, hit_does_not_replace_value) {
    StringCache cache;
    expect_load(cache, "A", "original");
    EXPECT_EQ(cache.fetch("A", [](const std::string&) {
        ADD_FAILURE() << "loader called for a resident page";
        return std::string("replacement");
    }), "original");
    expect_hit(cache, "A", "original");
}

TEST(ARC, repeated_hit) {
    StringCache cache;
    expect_load(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(ARC, empty_key_and_value) {
    StringCache cache;
    expect_load(cache, "", "");
    expect_hit(cache, "", "");
}

TEST(ARC, embedded_null_key) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    expect_load(cache, key, value);
    expect_load(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(ARC, integer_data) {
    ARC::Cache<int> cache;
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

TEST(ARC, non_default_data) {
    ARC::Cache<Payload> cache;
    int calls = 0;
    const auto loader = [&](const std::string&) {
        ++calls;
        return Payload{42};
    };
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(calls, 1);
}

TEST(ARC, returned_value_is_a_copy) {
    StringCache cache;
    auto value = cache.fetch("A", [](const std::string&) {
        return std::string("original");
    });
    value = "modified";
    expect_hit(cache, "A", "original");
}

TEST(ARC, independent_caches) {
    StringCache first;
    StringCache second;
    expect_load(first, "A", "first");
    expect_load(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(ARC, empty_loader_is_only_needed_on_miss) {
    StringCache cache;
    expect_load(cache, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(cache.fetch("A", empty_loader), "value-A");
    EXPECT_THROW((void)cache.fetch("missing", empty_loader), std::bad_function_call);
    expect_load(cache, "missing", "loaded");
}

TEST(ARC, destruction_releases_data) {
    std::weak_ptr<int> observer;
    {
        ARC::Cache<std::shared_ptr<int>> cache;
        (void)cache.fetch("A", [&](const std::string&) {
            auto payload = std::make_shared<int>(42);
            observer = payload;
            return payload;
        });
        ASSERT_FALSE(observer.expired());
    }
    EXPECT_TRUE(observer.expired());
}

TEST(ARC, fetch_at_capacity) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, sequential_eviction) {
    StringCache cache;
    for (int key = 0; key < 40; ++key) {
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    for (int key = 0; key < 36; ++key) {
        expect_miss(cache, std::to_string(key));
    }
    for (int key = 36; key < 40; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(ARC, promotion_protects_page) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    expect_load(cache, "E", "value-E");
    expect_hit(cache, "A", "value-A");
    expect_miss(cache, "B");
    expect_hit(cache, "E", "value-E");
}

TEST(ARC, all_pages_frequent) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "E", "value-E");
    expect_miss(cache, "A");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, ghost_lookup_is_miss) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    expect_load(cache, "E", "value-E");
    for (int access = 0; access < 3; ++access) {
        expect_miss(cache, "B");
    }
}

TEST(ARC, reload_b1) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    expect_load(cache, "E", "value-E");
    const auto initial_parameter = cache.get_size_parameter();
    expect_miss(cache, "B");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter);
    expect_load(cache, "B", "reloaded-B");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter + 1);
    expect_hit(cache, "B", "reloaded-B");
    expect_miss(cache, "A");
    ASSERT_LE(cache.get_size_parameter(), StringCache::capacity)
        << "adaptation parameter exceeds capacity";
}

TEST(ARC, reload_b2) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "E", "value-E");
    const auto initial_parameter = cache.get_size_parameter();
    expect_miss(cache, "A");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter);
    expect_load(cache, "A", "reloaded-A");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter - 1);
    expect_hit(cache, "A", "reloaded-A");
    expect_miss(cache, "E");
    ASSERT_LE(cache.get_size_parameter(), StringCache::capacity)
        << "adaptation parameter exceeds capacity";
}

TEST(ARC, loader_exception_preserves_residents) {
    StringCache cache;
    fill(cache);
    expect_miss(cache, "failed-load");
    expect_miss(cache, "failed-load");
    // These keys are the initial fill for the respective cache.
    for (const auto& key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "failed-load", "recovered");
    expect_hit(cache, "failed-load", "recovered");
}

TEST(ARC, ghost_releases_data_and_reload_caches_fresh_value) {
    ARC::Cache<std::shared_ptr<int>> cache;
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

TEST(ARC, t2_hit_refreshes_recency) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D", "A"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "E", "value-E");
    expect_miss(cache, "B");
    expect_hit(cache, "A", "value-A");
}

// Reference queues run from oldest to newest, independently of cache iterators.
class Workload {
public:
    void access(const std::string& key) {
        SCOPED_TRACE("key=" + key + " step=" + std::to_string(step_++));
        const auto resident = values_.find(key);
        if (resident != values_.end()) {
            expect_hit(cache_, key, resident->second);
            remove(t1_, key);
            remove(t2_, key);
            t2_.push_back(key);
        } else {
            const auto value = key + "-revision-" + std::to_string(step_);
            expect_load(cache_, key, value);
            if (contains(b1_, key)) {
                if (target_ < StringCache::capacity) {
                    ++target_;
                }
                replace(false);
                remove(b1_, key);
                t2_.push_back(key);
            } else if (contains(b2_, key)) {
                if (target_ > 0) {
                    --target_;
                }
                replace(true);
                remove(b2_, key);
                t2_.push_back(key);
            } else {
                if (t1_.size() + b1_.size() == StringCache::capacity) {
                    if (t1_.size() == StringCache::capacity) {
                        values_.erase(t1_.front());
                        t1_.pop_front();
                    } else {
                        b1_.pop_front();
                        replace(false);
                    }
                } else if (values_.size() + b1_.size() + b2_.size() >= StringCache::capacity) {
                    if (values_.size() + b1_.size() + b2_.size() == 2 * StringCache::capacity) {
                        b2_.pop_front();
                    }
                    replace(false);
                }
                t1_.push_back(key);
            }
            values_[key] = value;
        }
        EXPECT_EQ(cache_.get_size_parameter(), target_);
        EXPECT_LE(values_.size(), StringCache::capacity);
    }

private:
    using Queue = std::deque<std::string>;
    static bool contains(const Queue& queue, const std::string& key) {
        return std::find(queue.begin(), queue.end(), key) != queue.end();
    }
    static void remove(Queue& queue, const std::string& key) {
        const auto found = std::find(queue.begin(), queue.end(), key);
        if (found != queue.end()) {
            queue.erase(found);
        }
    }
    void replace(bool b2_hit) {
        const bool from_t1 = !t1_.empty() &&
            (t1_.size() > target_ || (b2_hit && t1_.size() == target_));
        auto& source = from_t1 ? t1_ : t2_;
        auto& history = from_t1 ? b1_ : b2_;
        ASSERT_FALSE(source.empty());
        history.push_back(source.front());
        values_.erase(source.front());
        source.pop_front();
    }

    StringCache cache_;
    Queue t1_, t2_, b1_, b2_;
    std::unordered_map<std::string, std::string> values_;
    std::size_t target_ = StringCache::capacity / 2;
    std::size_t step_ = 0;
};

TEST(ARC, repeated_reloads) {
    Workload workload;
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (const auto* key : {"A", "B", "C", "D", "E", "A", "F", "B"}) {
            workload.access(key);
        }
    }
}

TEST(ARC, mixed_workload) {
    Workload workload;
    std::uint32_t state = 0x12345678U;
    for (int step = 0; step < 4000; ++step) {
        state = state * 1664525U + 1013904223U;
        workload.access(std::to_string((state >> 16U) % 17U));
    }
}

TEST(ARC, hot_and_cold_workload) {
    Workload workload;
    for (int step = 0; step < 200; ++step) {
        workload.access("hot-A");
        workload.access("hot-B");
        workload.access("cold-" + std::to_string(step));
        workload.access("hot-A");
    }
}

} // namespace
} // namespace Tests
