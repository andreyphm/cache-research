#include "LFU_cache.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Tests {

using StringCache = LFU::Cache<std::string>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void insert(StringCache& cache, const std::string& key,
            const std::string& value) {
    require(cache.insert(key, value) == LFU::Status::success,
            "insert failed: " + key);
}

const std::string* expect_hit(StringCache& cache, const std::string& key,
                              const std::string& expected) {
    const std::string* data = nullptr;
    require(cache.get(key, data) == LFU::Status::success, "expected hit: " + key);
    require(data != nullptr, "hit returned null: " + key);
    require(*data == expected, "unexpected data: " + key);
    return data;
}

void expect_miss(StringCache& cache, const std::string& key) {
    const std::string sentinel = "sentinel";
    const std::string* data = &sentinel;
    require(cache.get(key, data) == LFU::Status::not_found,
            "expected miss: " + key);
    require(data == nullptr, "miss did not clear pointer: " + key);
}

void fill(StringCache& cache) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        insert(cache, key, std::string("value-") + key);
    }
}

void empty_cache() {
    StringCache cache;
    expect_miss(cache, "missing");
    expect_miss(cache, "");
}

void miss_clears_pointer() {
    StringCache cache;
    insert(cache, "A", "value-A");
    const auto* data = expect_hit(cache, "A", "value-A");
    require(cache.get("missing", data) == LFU::Status::not_found,
            "unknown key must miss");
    require(data == nullptr, "miss retained previous hit pointer");
    expect_hit(cache, "A", "value-A");
}

void insert_and_get() {
    StringCache cache;
    fill(cache);
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

void repeated_hit() {
    StringCache cache;
    insert(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

void duplicate_updates_value() {
    StringCache cache;
    insert(cache, "A", "original");
    insert(cache, "A", "replacement");
    expect_hit(cache, "A", "replacement");
    insert(cache, "A", "");
    expect_hit(cache, "A", "");
}

void duplicate_promotes_page() {
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

void duplicate_at_capacity() {
    StringCache cache;
    fill(cache);
    insert(cache, "B", "updated-B");
    for (const auto* key : {"A", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_hit(cache, "B", "updated-B");
}

void empty_key_and_value() {
    StringCache cache;
    insert(cache, "", "");
    expect_hit(cache, "", "");
    expect_miss(cache, "different");
}

void embedded_null_key() {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    insert(cache, key, value);
    insert(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

void integer_data() {
    LFU::Cache<int> cache;
    require(cache.insert("zero", 0) == LFU::Status::success, "insert integer");
    require(cache.insert("negative", -42) == LFU::Status::success, "insert negative");
    const int* data = nullptr;
    require(cache.get("zero", data) == LFU::Status::success, "get zero");
    require(data != nullptr && *data == 0, "zero is a present value");
    require(cache.get("negative", data) == LFU::Status::success, "get negative");
    require(data != nullptr && *data == -42, "negative value mismatch");
}

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};

void non_default_data() {
    LFU::Cache<Payload> cache;
    require(cache.insert("A", Payload{42}) == LFU::Status::success,
            "insert non-default-constructible data");
    const Payload* data = nullptr;
    require(cache.get("A", data) == LFU::Status::success, "get payload");
    require(data != nullptr && data->value_ == 42, "payload mismatch");
}

void independent_caches() {
    StringCache first;
    StringCache second;
    insert(first, "A", "first");
    expect_miss(second, "A");
    insert(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

void pointer_survives_promotion() {
    StringCache cache;
    fill(cache);
    const auto* saved = expect_hit(cache, "A", "value-A");
    expect_hit(cache, "B", "value-B");
    require(expect_hit(cache, "A", "value-A") == saved,
            "promotion changed the resident data address");
    insert(cache, "E", "value-E");
    require(expect_hit(cache, "A", "value-A") == saved,
            "eviction of another page changed the resident data address");
}

void pointer_survives_update() {
    StringCache cache;
    insert(cache, "A", "original");
    const auto* saved = expect_hit(cache, "A", "original");
    insert(cache, "A", "replacement");
    require(expect_hit(cache, "A", "replacement") == saved,
            "update changed the resident data address");
}

void sequential_eviction() {
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

void promotion_protects_page() {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    insert(cache, "E", "value-E");
    expect_miss(cache, "B");
    for (const auto* key : {"A", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

void frequency_beats_recency() {
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

void equal_frequency_uses_recency() {
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

void all_pages_frequent_reject_new_page() {
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

void misses_do_not_change_eviction() {
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

void reload_resets_frequency() {
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

void eviction_releases_data() {
    LFU::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    require(cache.insert("A", payload) == LFU::Status::success, "insert payload");
    payload.reset();
    for (const auto* key : {"B", "C", "D"}) {
        require(cache.insert(key, std::make_shared<int>(1)) == LFU::Status::success,
                "fill shared data cache");
    }
    require(!observer.expired(), "resident data disappeared");
    require(cache.insert("E", std::make_shared<int>(2)) == LFU::Status::success,
            "insert E");
    require(observer.expired(), "evicted entry retained its payload");
}

void update_releases_old_data() {
    LFU::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    require(cache.insert("A", payload) == LFU::Status::success, "insert payload");
    payload.reset();
    require(!observer.expired(), "cache did not retain payload");
    require(cache.insert("A", std::make_shared<int>(7)) == LFU::Status::success,
            "update payload");
    require(observer.expired(), "update retained old payload");
    const std::shared_ptr<int>* data = nullptr;
    require(cache.get("A", data) == LFU::Status::success, "get updated payload");
    require(data != nullptr && *data && **data == 7, "updated payload mismatch");
}

void rejected_insert_releases_data() {
    LFU::Cache<std::shared_ptr<int>> cache;
    for (const auto* key : {"A", "B", "C", "D"}) {
        require(cache.insert(key, std::make_shared<int>(1)) == LFU::Status::success,
                "fill shared data cache");
        const std::shared_ptr<int>* data = nullptr;
        require(cache.get(key, data) == LFU::Status::success, "promote resident");
    }
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    require(cache.insert("E", payload) == LFU::Status::success, "insert candidate");
    payload.reset();
    require(observer.expired(), "rejected entry retained its payload");
    const std::shared_ptr<int>* data = nullptr;
    require(cache.get("E", data) == LFU::Status::not_found, "candidate must miss");
    require(data == nullptr, "rejected entry returned a pointer");
}

void destruction_releases_data() {
    std::weak_ptr<int> observer;
    {
        LFU::Cache<std::shared_ptr<int>> cache;
        auto payload = std::make_shared<int>(42);
        observer = payload;
        require(cache.insert("A", payload) == LFU::Status::success, "insert payload");
        payload.reset();
        require(!observer.expired(), "cache did not retain payload");
    }
    require(observer.expired(), "cache destruction retained payload");
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

void repeated_reloads() {
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

void mixed_workload() {
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

void hot_and_cold_workload() {
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

struct TestCase {
    std::string_view name_;
    void (*run_)();
};

constexpr TestCase test_cases[] = {
    {"empty_cache", empty_cache},
    {"miss_clears_pointer", miss_clears_pointer},
    {"insert_and_get", insert_and_get},
    {"repeated_hit", repeated_hit},
    {"duplicate_updates_value", duplicate_updates_value},
    {"duplicate_promotes_page", duplicate_promotes_page},
    {"duplicate_at_capacity", duplicate_at_capacity},
    {"empty_key_and_value", empty_key_and_value},
    {"embedded_null_key", embedded_null_key},
    {"integer_data", integer_data},
    {"non_default_data", non_default_data},
    {"independent_caches", independent_caches},
    {"pointer_survives_promotion", pointer_survives_promotion},
    {"pointer_survives_update", pointer_survives_update},
    {"sequential_eviction", sequential_eviction},
    {"promotion_protects_page", promotion_protects_page},
    {"frequency_beats_recency", frequency_beats_recency},
    {"equal_frequency_uses_recency", equal_frequency_uses_recency},
    {"all_pages_frequent_reject_new_page", all_pages_frequent_reject_new_page},
    {"misses_do_not_change_eviction", misses_do_not_change_eviction},
    {"reload_resets_frequency", reload_resets_frequency},
    {"eviction_releases_data", eviction_releases_data},
    {"update_releases_old_data", update_releases_old_data},
    {"rejected_insert_releases_data", rejected_insert_releases_data},
    {"destruction_releases_data", destruction_releases_data},
    {"repeated_reloads", repeated_reloads},
    {"mixed_workload", mixed_workload},
    {"hot_and_cold_workload", hot_and_cold_workload},
};

} // namespace Tests

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: lfu_tests <test_name>\nAvailable tests:\n";
        for (const auto& test_case : Tests::test_cases) {
            std::cerr << "  " << test_case.name_ << '\n';
        }
        return EXIT_FAILURE;
    }

    for (const auto& test_case : Tests::test_cases) {
        if (test_case.name_ == argv[1]) {
            try {
                test_case.run_();
                std::cout << "PASS: " << test_case.name_ << '\n';
                return EXIT_SUCCESS;
            } catch (const std::exception& error) {
                std::cerr << "FAIL: " << test_case.name_ << ": " << error.what() << '\n';
                return EXIT_FAILURE;
            }
        }
    }
    std::cerr << "Unknown test: " << argv[1] << '\n';
    return EXIT_FAILURE;
}
