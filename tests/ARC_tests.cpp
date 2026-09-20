#include "ARC_cache.hpp"

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

using StringCache = ARC::Cache<std::string>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void insert(StringCache& cache, const std::string& key,
            const std::string& value) {
    require(cache.insert(key, value) == ARC::Status::success,
            "insert failed: " + key);
}

const std::string* expect_hit(StringCache& cache, const std::string& key,
                              const std::string& expected) {
    const std::string* data = nullptr;
    require(cache.get(key, data) == ARC::Status::success, "expected hit: " + key);
    require(data != nullptr, "hit returned null: " + key);
    require(*data == expected, "unexpected data: " + key);
    return data;
}

void expect_miss(StringCache& cache, const std::string& key) {
    const std::string sentinel = "sentinel";
    const std::string* data = &sentinel;
    require(cache.get(key, data) == ARC::Status::not_found,
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
    require(cache.get("missing", data) == ARC::Status::not_found,
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

void duplicate_preserves_value() {
    StringCache cache;
    insert(cache, "A", "original");
    insert(cache, "A", "replacement");
    expect_hit(cache, "A", "original");
    insert(cache, "A", "another replacement");
    expect_hit(cache, "A", "original");
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
    ARC::Cache<int> cache;
    require(cache.insert("zero", 0) == ARC::Status::success, "insert integer");
    require(cache.insert("negative", -42) == ARC::Status::success, "insert negative");
    const int* data = nullptr;
    require(cache.get("zero", data) == ARC::Status::success, "get zero");
    require(data != nullptr && *data == 0, "zero is a present value");
    require(cache.get("negative", data) == ARC::Status::success, "get negative");
    require(data != nullptr && *data == -42, "negative value mismatch");
}

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};

void non_default_data() {
    ARC::Cache<Payload> cache;
    require(cache.insert("A", Payload{42}) == ARC::Status::success,
            "insert non-default-constructible data");
    const Payload* data = nullptr;
    require(cache.get("A", data) == ARC::Status::success, "get payload");
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
    expect_hit(cache, "A", "value-A");
    expect_miss(cache, "B");
    expect_hit(cache, "E", "value-E");
}

void all_pages_frequent() {
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

void ghost_lookup_is_miss() {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    insert(cache, "E", "value-E");
    for (int access = 0; access < 3; ++access) {
        expect_miss(cache, "B");
    }
}

void reload_b1() {
    StringCache cache;
    fill(cache);
    expect_hit(cache, "A", "value-A");
    insert(cache, "E", "value-E");
    expect_miss(cache, "B");
    insert(cache, "B", "reloaded-B");
    expect_hit(cache, "B", "reloaded-B");
    expect_miss(cache, "A");
    require(cache.get_size_parameter() <= StringCache::capacity,
            "adaptation parameter exceeds capacity");
}

void reload_b2() {
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
    require(cache.get_size_parameter() <= StringCache::capacity,
            "adaptation parameter exceeds capacity");
}

void ghost_releases_data() {
    ARC::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    require(cache.insert("A", std::make_shared<int>(1)) == ARC::Status::success,
            "insert A");
    require(cache.insert("B", payload) == ARC::Status::success, "insert B");
    payload.reset();
    for (const auto* key : {"C", "D"}) {
        require(cache.insert(key, std::make_shared<int>(2)) == ARC::Status::success,
                "fill shared data cache");
    }
    const std::shared_ptr<int>* data = nullptr;
    require(cache.get("A", data) == ARC::Status::success, "promote A");
    require(!observer.expired(), "resident data disappeared");
    require(cache.insert("E", std::make_shared<int>(3)) == ARC::Status::success,
            "insert E");
    require(observer.expired(), "ghost entry retained its payload");
}

void destruction_releases_data() {
    std::weak_ptr<int> observer;
    {
        ARC::Cache<std::shared_ptr<int>> cache;
        auto payload = std::make_shared<int>(42);
        observer = payload;
        require(cache.insert("A", payload) == ARC::Status::success, "insert payload");
        payload.reset();
        require(!observer.expired(), "cache did not retain payload");
    }
    require(observer.expired(), "cache destruction retained payload");
}

class Workload {
public:
    void access(const std::string& key) {
        const std::string* data = nullptr;
        const auto status = cache_.get(key, data);
        if (status == ARC::Status::success) {
            const auto expected = last_loaded_.find(key);
            require(expected != last_loaded_.end(), "hit on a never-inserted key");
            require(data != nullptr && *data == expected->second,
                    "workload hit data mismatch: " + key);
        } else {
            require(status == ARC::Status::not_found, "unexpected get status");
            require(data == nullptr, "workload miss retained pointer");
            const auto value = key + "-revision-" + std::to_string(revision_++);
            insert(cache_, key, value);
            last_loaded_[key] = value;
            expect_hit(cache_, key, value);
        }
        require(cache_.get_size_parameter() <= StringCache::capacity,
                "adaptation parameter outside [0, capacity]");
    }

private:
    StringCache cache_;
    std::unordered_map<std::string, std::string> last_loaded_;
    std::size_t revision_ = 0;
};

void repeated_reloads() {
    Workload workload;
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (const auto* key : {"A", "B", "C", "D", "E", "A", "F", "B"}) {
            workload.access(key);
        }
    }
}

void mixed_workload() {
    Workload workload;
    std::uint32_t state = 0x12345678U;
    for (int step = 0; step < 2000; ++step) {
        state = state * 1664525U + 1013904223U;
        workload.access(std::to_string((state >> 16U) % 17U));
    }
}

void hot_and_cold_workload() {
    Workload workload;
    for (int step = 0; step < 200; ++step) {
        workload.access("hot-A");
        workload.access("hot-B");
        workload.access("cold-" + std::to_string(step));
        workload.access("hot-A");
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
    {"duplicate_preserves_value", duplicate_preserves_value},
    {"empty_key_and_value", empty_key_and_value},
    {"embedded_null_key", embedded_null_key},
    {"integer_data", integer_data},
    {"non_default_data", non_default_data},
    {"independent_caches", independent_caches},
    {"pointer_survives_promotion", pointer_survives_promotion},
    {"sequential_eviction", sequential_eviction},
    {"promotion_protects_page", promotion_protects_page},
    {"all_pages_frequent", all_pages_frequent},
    {"ghost_lookup_is_miss", ghost_lookup_is_miss},
    {"reload_b1", reload_b1},
    {"reload_b2", reload_b2},
    {"ghost_releases_data", ghost_releases_data},
    {"destruction_releases_data", destruction_releases_data},
    {"repeated_reloads", repeated_reloads},
    {"mixed_workload", mixed_workload},
    {"hot_and_cold_workload", hot_and_cold_workload},
};

} // namespace Tests

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: arc_tests <test_name>\nAvailable tests:\n";
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
