#include "ARC_cache.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Tests {

using StringCache = ARC::Cache<std::string>;

void insert(StringCache& cache, const std::string& key,
            const std::string& value) {
    ASSERT_EQ(cache.insert(key, value), ARC::Status::success) << "insert failed: " + key;
}

const std::string* expect_hit(StringCache& cache, const std::string& key,
                              const std::string& expected) {
    const std::string* data = nullptr;
    EXPECT_EQ(cache.get(key, data), ARC::Status::success) << "expected hit: " << key;
    EXPECT_NE(data, nullptr) << "hit returned null: " << key;
    if (data != nullptr) {
        EXPECT_EQ(*data, expected) << "unexpected data: " << key;
    }
    return data;
}

void expect_miss(StringCache& cache, const std::string& key) {
    const std::string sentinel = "sentinel";
    const std::string* data = &sentinel;
    ASSERT_EQ(cache.get(key, data), ARC::Status::not_found) << "expected miss: " + key;
    ASSERT_EQ(data, nullptr) << "miss did not clear pointer: " + key;
}

void fill(StringCache& cache) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        insert(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, empty_cache) {
    StringCache cache;
    expect_miss(cache, "missing");
    expect_miss(cache, "");
}

TEST(ARC, miss_clears_pointer) {
    StringCache cache;
    insert(cache, "A", "value-A");
    const auto* data = expect_hit(cache, "A", "value-A");
    ASSERT_EQ(cache.get("missing", data), ARC::Status::not_found) << "unknown key must miss";
    ASSERT_EQ(data, nullptr) << "miss retained previous hit pointer";
    expect_hit(cache, "A", "value-A");
}

TEST(ARC, insert_and_get) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, repeated_hit) {
    StringCache cache;
    insert(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(ARC, duplicate_preserves_value) {
    StringCache cache;
    insert(cache, "A", "original");
    insert(cache, "A", "replacement");
    expect_hit(cache, "A", "original");
    insert(cache, "A", "another replacement");
    expect_hit(cache, "A", "original");
}

TEST(ARC, empty_key_and_value) {
    StringCache cache;
    insert(cache, "", "");
    expect_hit(cache, "", "");
    expect_miss(cache, "different");
}

TEST(ARC, embedded_null_key) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    insert(cache, key, value);
    insert(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(ARC, integer_data) {
    ARC::Cache<int> cache;
    ASSERT_EQ(cache.insert("zero", 0), ARC::Status::success) << "insert integer";
    ASSERT_EQ(cache.insert("negative", -42), ARC::Status::success) << "insert negative";
    const int* data = nullptr;
    ASSERT_EQ(cache.get("zero", data), ARC::Status::success) << "get zero";
    ASSERT_NE(data, nullptr) << "zero is a present value";
    ASSERT_EQ(*data, 0) << "zero is a present value";
    ASSERT_EQ(cache.get("negative", data), ARC::Status::success) << "get negative";
    ASSERT_NE(data, nullptr) << "negative value mismatch";
    ASSERT_EQ(*data, -42) << "negative value mismatch";
}

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};

TEST(ARC, non_default_data) {
    ARC::Cache<Payload> cache;
    ASSERT_EQ(cache.insert("A", Payload{42}), ARC::Status::success)
        << "insert non-default-constructible data";
    const Payload* data = nullptr;
    ASSERT_EQ(cache.get("A", data), ARC::Status::success) << "get payload";
    ASSERT_NE(data, nullptr) << "payload mismatch";
    ASSERT_EQ(data->value_, 42) << "payload mismatch";
}

TEST(ARC, independent_caches) {
    StringCache first;
    StringCache second;
    insert(first, "A", "first");
    expect_miss(second, "A");
    insert(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(ARC, pointer_survives_promotion) {
    StringCache cache;
    fill(cache);
    const auto* saved = expect_hit(cache, "A", "value-A");
    expect_hit(cache, "B", "value-B");
    ASSERT_EQ(expect_hit(cache, "A", "value-A"), saved)
        << "promotion changed the resident data address";
}

TEST(ARC, sequential_eviction) {
    StringCache cache;
    for (int key = 0; key < 40; ++key) {
        insert(cache, std::to_string(key), "value-" + std::to_string(key));
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
    insert(cache, "E", "value-E");
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
    insert(cache, "E", "value-E");
    expect_miss(cache, "A");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, ghost_lookup_is_miss) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    insert(cache, "E", "value-E");
    for (int access = 0; access < 3; ++access) {
        expect_miss(cache, "B");
    }
}

TEST(ARC, reload_b1) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    insert(cache, "E", "value-E");
    expect_miss(cache, "B");
    insert(cache, "B", "reloaded-B");
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
    insert(cache, "E", "value-E");
    expect_miss(cache, "A");
    insert(cache, "A", "reloaded-A");
    expect_hit(cache, "A", "reloaded-A");
    expect_miss(cache, "E");
    ASSERT_LE(cache.get_size_parameter(), StringCache::capacity)
        << "adaptation parameter exceeds capacity";
}

TEST(ARC, ghost_releases_data) {
    ARC::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    ASSERT_EQ(cache.insert("A", std::make_shared<int>(1)), ARC::Status::success) << "insert A";
    ASSERT_EQ(cache.insert("B", payload), ARC::Status::success) << "insert B";
    payload.reset();
    for (const auto* key : {"C", "D"}) {
        ASSERT_EQ(cache.insert(key, std::make_shared<int>(2)), ARC::Status::success)
            << "fill shared data cache";
    }
    const std::shared_ptr<int>* data = nullptr;
    ASSERT_EQ(cache.get("A", data), ARC::Status::success) << "promote A";
    ASSERT_FALSE(observer.expired()) << "resident data disappeared";
    ASSERT_EQ(cache.insert("E", std::make_shared<int>(3)), ARC::Status::success) << "insert E";
    ASSERT_TRUE(observer.expired()) << "ghost entry retained its payload";
}

TEST(ARC, destruction_releases_data) {
    std::weak_ptr<int> observer;
    {
        ARC::Cache<std::shared_ptr<int>> cache;
        auto payload = std::make_shared<int>(42);
        observer = payload;
        ASSERT_EQ(cache.insert("A", payload), ARC::Status::success) << "insert payload";
        payload.reset();
        ASSERT_FALSE(observer.expired()) << "cache did not retain payload";
    }
    ASSERT_TRUE(observer.expired()) << "cache destruction retained payload";
}

class Workload {
public:
    void access(const std::string& key) {
        const std::string* data = nullptr;
        const auto status = cache_.get(key, data);
        if (status == ARC::Status::success) {
            const auto expected = last_loaded_.find(key);
            ASSERT_NE(expected, last_loaded_.end()) << "hit on a never-inserted key";
            ASSERT_NE(data, nullptr) << "workload hit data mismatch: " + key;
            ASSERT_EQ(*data, expected->second) << "workload hit data mismatch: " + key;
        } else {
            ASSERT_EQ(status, ARC::Status::not_found) << "unexpected get status";
            ASSERT_EQ(data, nullptr) << "workload miss retained pointer";
            const auto value = key + "-revision-" + std::to_string(revision_++);
            insert(cache_, key, value);
            last_loaded_[key] = value;
            expect_hit(cache_, key, value);
        }
        ASSERT_LE(cache_.get_size_parameter(), StringCache::capacity)
            << "adaptation parameter outside [0, capacity]";
    }

private:
    StringCache cache_;
    std::unordered_map<std::string, std::string> last_loaded_;
    std::size_t revision_ = 0;
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
    for (int step = 0; step < 2000; ++step) {
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

} // namespace Tests
