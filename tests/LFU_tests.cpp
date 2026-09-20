#include "LFU_cache.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Tests {

using StringCache = LFU::Cache<std::string>;

void insert(StringCache& cache, const std::string& key,
            const std::string& value) {
    ASSERT_EQ(cache.insert(key, value), LFU::Status::success) << "insert failed: " + key;
}

const std::string* expect_hit(StringCache& cache, const std::string& key,
                              const std::string& expected) {
    const std::string* data = nullptr;
    EXPECT_EQ(cache.get(key, data), LFU::Status::success) << "expected hit: " << key;
    EXPECT_NE(data, nullptr) << "hit returned null: " << key;
    if (data != nullptr) {
        EXPECT_EQ(*data, expected) << "unexpected data: " << key;
    }
    return data;
}

void expect_miss(StringCache& cache, const std::string& key) {
    const std::string sentinel = "sentinel";
    const std::string* data = &sentinel;
    ASSERT_EQ(cache.get(key, data), LFU::Status::not_found) << "expected miss: " + key;
    ASSERT_EQ(data, nullptr) << "miss did not clear pointer: " + key;
}

void fill(StringCache& cache) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        insert(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, empty_cache) {
    StringCache cache;
    expect_miss(cache, "missing");
    expect_miss(cache, "");
}

TEST(LFU, miss_clears_pointer) {
    StringCache cache;
    insert(cache, "A", "value-A");
    const auto* data = expect_hit(cache, "A", "value-A");
    ASSERT_EQ(cache.get("missing", data), LFU::Status::not_found) << "unknown key must miss";
    ASSERT_EQ(data, nullptr) << "miss retained previous hit pointer";
    expect_hit(cache, "A", "value-A");
}

TEST(LFU, insert_and_get) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, repeated_hit) {
    StringCache cache;
    insert(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(LFU, duplicate_updates_value) {
    StringCache cache;
    insert(cache, "A", "original");
    insert(cache, "A", "replacement");
    expect_hit(cache, "A", "replacement");
    insert(cache, "A", "");
    expect_hit(cache, "A", "");
}

TEST(LFU, duplicate_promotes_page) {
    StringCache cache;
    fill(cache);
    insert(cache, "A", "updated-A");
    insert(cache, "E", "value-E");
    expect_miss(cache, "B");
    expect_hit(cache, "A", "updated-A");
    for (const auto* key : {"C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, duplicate_at_capacity) {
    StringCache cache;
    fill(cache);
    insert(cache, "B", "updated-B");
    for (const auto* key : {"A", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_hit(cache, "B", "updated-B");
}

TEST(LFU, empty_key_and_value) {
    StringCache cache;
    insert(cache, "", "");
    expect_hit(cache, "", "");
    expect_miss(cache, "different");
}

TEST(LFU, embedded_null_key) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    insert(cache, key, value);
    insert(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(LFU, integer_data) {
    LFU::Cache<int> cache;
    ASSERT_EQ(cache.insert("zero", 0), LFU::Status::success) << "insert integer";
    ASSERT_EQ(cache.insert("negative", -42), LFU::Status::success) << "insert negative";
    const int* data = nullptr;
    ASSERT_EQ(cache.get("zero", data), LFU::Status::success) << "get zero";
    ASSERT_NE(data, nullptr) << "zero is a present value";
    ASSERT_EQ(*data, 0) << "zero is a present value";
    ASSERT_EQ(cache.get("negative", data), LFU::Status::success) << "get negative";
    ASSERT_NE(data, nullptr) << "negative value mismatch";
    ASSERT_EQ(*data, -42) << "negative value mismatch";
}

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};

TEST(LFU, non_default_data) {
    LFU::Cache<Payload> cache;
    ASSERT_EQ(cache.insert("A", Payload{42}), LFU::Status::success)
        << "insert non-default-constructible data";
    const Payload* data = nullptr;
    ASSERT_EQ(cache.get("A", data), LFU::Status::success) << "get payload";
    ASSERT_NE(data, nullptr) << "payload mismatch";
    ASSERT_EQ(data->value_, 42) << "payload mismatch";
}

TEST(LFU, independent_caches) {
    StringCache first;
    StringCache second;
    insert(first, "A", "first");
    expect_miss(second, "A");
    insert(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(LFU, pointer_survives_promotion) {
    StringCache cache;
    fill(cache);
    const auto* saved = expect_hit(cache, "A", "value-A");
    expect_hit(cache, "B", "value-B");
    ASSERT_EQ(expect_hit(cache, "A", "value-A"), saved)
        << "promotion changed the resident data address";
    insert(cache, "E", "value-E");
    ASSERT_EQ(expect_hit(cache, "A", "value-A"), saved)
        << "eviction of another page changed the resident data address";
}

TEST(LFU, pointer_survives_update) {
    StringCache cache;
    insert(cache, "A", "original");
    const auto* saved = expect_hit(cache, "A", "original");
    insert(cache, "A", "replacement");
    ASSERT_EQ(expect_hit(cache, "A", "replacement"), saved)
        << "update changed the resident data address";
}

TEST(LFU, sequential_eviction) {
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

TEST(LFU, promotion_protects_page) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    insert(cache, "E", "value-E");
    expect_miss(cache, "B");
    for (const auto* key : {"A", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, frequency_beats_recency) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    for (const auto* key : {"E", "F", "G", "H"}) {
        insert(cache, key, std::string("value-") + key);
    }
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_miss(cache, key);
    }
    for (const auto* key : {"A", "F", "G", "H"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, equal_frequency_uses_recency) {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    insert(cache, "B", "updated-B");
    insert(cache, "E", "value-E");
    expect_miss(cache, "C");
    insert(cache, "F", "value-F");
    expect_miss(cache, "D");
    expect_hit(cache, "A", "value-A");
    expect_hit(cache, "B", "updated-B");
    expect_hit(cache, "E", "value-E");
    expect_hit(cache, "F", "value-F");
}

TEST(LFU, all_pages_frequent_reject_new_page) {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        insert(cache, "E", "value-E");
        expect_miss(cache, "E");
    }
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, misses_do_not_change_eviction) {
    StringCache cache;
    fill(cache);
    for (int attempt = 0; attempt < 10; ++attempt) {
        expect_miss(cache, "E");
        expect_miss(cache, "missing");
    }
    insert(cache, "E", "value-E");
    expect_miss(cache, "A");
    insert(cache, "F", "value-F");
    expect_miss(cache, "B");
    for (const auto* key : {"C", "D", "E", "F"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, reload_resets_frequency) {
    StringCache cache;
    fill(cache);
    insert(cache, "E", "value-E");
    expect_miss(cache, "A");
    insert(cache, "A", "reloaded-A");
    expect_miss(cache, "B");
    for (const auto* key : {"F", "G", "H", "I"}) {
        insert(cache, key, std::string("value-") + key);
    }
    expect_miss(cache, "A");
    insert(cache, "A", "latest-A");
    expect_hit(cache, "A", "latest-A");
}

TEST(LFU, eviction_releases_data) {
    LFU::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    ASSERT_EQ(cache.insert("A", payload), LFU::Status::success) << "insert payload";
    payload.reset();
    for (const auto* key : {"B", "C", "D"}) {
        ASSERT_EQ(cache.insert(key, std::make_shared<int>(1)), LFU::Status::success)
            << "fill shared data cache";
    }
    ASSERT_FALSE(observer.expired()) << "resident data disappeared";
    ASSERT_EQ(cache.insert("E", std::make_shared<int>(2)), LFU::Status::success) << "insert E";
    ASSERT_TRUE(observer.expired()) << "evicted entry retained its payload";
}

TEST(LFU, update_releases_old_data) {
    LFU::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    ASSERT_EQ(cache.insert("A", payload), LFU::Status::success) << "insert payload";
    payload.reset();
    ASSERT_FALSE(observer.expired()) << "cache did not retain payload";
    ASSERT_EQ(cache.insert("A", std::make_shared<int>(7)), LFU::Status::success)
        << "update payload";
    ASSERT_TRUE(observer.expired()) << "update retained old payload";
    const std::shared_ptr<int>* data = nullptr;
    ASSERT_EQ(cache.get("A", data), LFU::Status::success) << "get updated payload";
    ASSERT_NE(data, nullptr) << "updated payload mismatch";
    ASSERT_TRUE(*data) << "updated payload mismatch";
    ASSERT_EQ(**data, 7) << "updated payload mismatch";
}

TEST(LFU, rejected_insert_releases_data) {
    LFU::Cache<std::shared_ptr<int>> cache;
    for (const auto* key : {"A", "B", "C", "D"}) {
        ASSERT_EQ(cache.insert(key, std::make_shared<int>(1)), LFU::Status::success)
            << "fill shared data cache";
        const std::shared_ptr<int>* data = nullptr;
        ASSERT_EQ(cache.get(key, data), LFU::Status::success) << "promote resident";
    }
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    ASSERT_EQ(cache.insert("E", payload), LFU::Status::success) << "insert candidate";
    payload.reset();
    ASSERT_TRUE(observer.expired()) << "rejected entry retained its payload";
    const std::shared_ptr<int>* data = nullptr;
    ASSERT_EQ(cache.get("E", data), LFU::Status::not_found) << "candidate must miss";
    ASSERT_EQ(data, nullptr) << "rejected entry returned a pointer";
}

TEST(LFU, destruction_releases_data) {
    std::weak_ptr<int> observer;
    {
        LFU::Cache<std::shared_ptr<int>> cache;
        auto payload = std::make_shared<int>(42);
        observer = payload;
        ASSERT_EQ(cache.insert("A", payload), LFU::Status::success) << "insert payload";
        payload.reset();
        ASSERT_FALSE(observer.expired()) << "cache did not retain payload";
    }
    ASSERT_TRUE(observer.expired()) << "cache destruction retained payload";
}

class Workload {
public:
    void put(const std::string& key) {
        const auto value = key + "-revision-" + std::to_string(revision_++);
        insert(cache_, key, value);
        auto& entry = entries_[key];
        entry.value_ = value;
        ++entry.frequency_;
        entry.last_access_ = ++clock_;
        if (entries_.size() > StringCache::capacity) {
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
    }

    bool get(const std::string& key) {
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            expect_miss(cache_, key);
            return false;
        }
        expect_hit(cache_, key, found->second.value_);
        ++found->second.frequency_;
        found->second.last_access_ = ++clock_;
        return true;
    }

    void access(const std::string& key) {
        if (!get(key)) {
            put(key);
            get(key);
        }
    }

private:
    struct Entry {
        std::string value_;
        std::size_t frequency_ = 0;
        std::size_t last_access_ = 0;
    };

    StringCache cache_;
    std::unordered_map<std::string, Entry> entries_;
    std::size_t revision_ = 0;
    std::size_t clock_ = 0;
};

TEST(LFU, repeated_reloads) {
    Workload workload;
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (const auto* key : {"A", "B", "C", "D", "E", "A", "F", "B"}) {
            workload.put(key);
        }
        workload.get("A");
        workload.get("B");
        workload.get("E");
    }
}

TEST(LFU, mixed_workload) {
    Workload workload;
    std::uint32_t state = 0x12345678U;
    for (int step = 0; step < 4000; ++step) {
        state = state * 1664525U + 1013904223U;
        const auto key = std::to_string((state >> 16U) % 17U);
        if ((state & 3U) == 0) {
            workload.put(key);
        } else {
            workload.access(key);
        }
    }
    for (int key = 0; key < 17; ++key) {
        workload.get(std::to_string(key));
    }
}

TEST(LFU, hot_and_cold_workload) {
    Workload workload;
    for (int step = 0; step < 200; ++step) {
        workload.access("hot-A");
        workload.access("hot-B");
        workload.put("cold-" + std::to_string(step));
        workload.access("hot-A");
    }
    for (int step = 0; step < 200; ++step) {
        workload.get("cold-" + std::to_string(step));
    }
}

} // namespace Tests
