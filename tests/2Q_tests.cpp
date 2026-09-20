#include "2Q_cache.hpp"

#include <algorithm>
#include <deque>
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

using StringCache = TWO_Q::Cache<std::string>;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void insert(StringCache& cache, const std::string& key,
            const std::string& value) {
    require(cache.insert(key, value) == TWO_Q::Status::success,
            "insert failed: " + key);
}

const std::string* expect_hit(StringCache& cache, const std::string& key,
                              const std::string& expected) {
    const std::string* data = nullptr;
    require(cache.get(key, data) == TWO_Q::Status::success, "expected hit: " + key);
    require(data != nullptr, "hit returned null: " + key);
    require(*data == expected, "unexpected data: " + key);
    return data;
}

void expect_miss(StringCache& cache, const std::string& key) {
    const std::string sentinel = "sentinel";
    const std::string* data = &sentinel;
    require(cache.get(key, data) == TWO_Q::Status::not_found,
            "expected miss: " + key);
    require(data == nullptr, "miss did not clear pointer: " + key);
}

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};

void empty_cache() {
    StringCache cache;
    expect_miss(cache, "missing");
    expect_miss(cache, "");
}

void miss_clears_pointer() {
    StringCache cache;
    insert(cache, "A", "value-A");
    const auto* data = expect_hit(cache, "A", "value-A");
    require(cache.get("missing", data) == TWO_Q::Status::not_found,
            "unknown key must miss");
    require(data == nullptr, "miss retained previous hit pointer");
    expect_hit(cache, "A", "value-A");
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
    TWO_Q::Cache<int> cache;
    require(cache.insert("zero", 0) == TWO_Q::Status::success, "insert integer");
    require(cache.insert("negative", -42) == TWO_Q::Status::success, "insert negative");
    const int* data = nullptr;
    require(cache.get("zero", data) == TWO_Q::Status::success, "get zero");
    require(data != nullptr && *data == 0, "zero is a present value");
    require(cache.get("negative", data) == TWO_Q::Status::success, "get negative");
    require(data != nullptr && *data == -42, "negative value mismatch");
}

void non_default_data() {
    TWO_Q::Cache<Payload> cache;
    require(cache.insert("A", Payload{42}) == TWO_Q::Status::success,
            "insert non-default-constructible data");
    const Payload* data = nullptr;
    require(cache.get("A", data) == TWO_Q::Status::success, "get payload");
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

void pointer_survives_update() {
    StringCache cache;
    insert(cache, "A", "original");
    const auto* saved = expect_hit(cache, "A", "original");
    insert(cache, "A", "replacement");
    require(expect_hit(cache, "A", "replacement") == saved,
            "update changed the resident data address");
}

void update_releases_old_data() {
    TWO_Q::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    require(cache.insert("A", payload) == TWO_Q::Status::success, "insert payload");
    payload.reset();
    require(!observer.expired(), "cache did not retain payload");
    require(cache.insert("A", std::make_shared<int>(7)) == TWO_Q::Status::success,
            "update payload");
    require(observer.expired(), "update retained old payload");
    const std::shared_ptr<int>* data = nullptr;
    require(cache.get("A", data) == TWO_Q::Status::success, "get updated payload");
    require(data != nullptr && *data && **data == 7, "updated payload mismatch");
}

void destruction_releases_data() {
    std::weak_ptr<int> observer;
    {
        TWO_Q::Cache<std::shared_ptr<int>> cache;
        auto payload = std::make_shared<int>(42);
        observer = payload;
        require(cache.insert("A", payload) == TWO_Q::Status::success, "insert payload");
        payload.reset();
        require(!observer.expired(), "cache did not retain payload");
    }
    require(observer.expired(), "cache destruction retained payload");
}

void fill(StringCache& cache) {
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        insert(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

void fill_frequent(StringCache& cache) {
    fill(cache);
    insert(cache, "cold", "cold");
    for (std::size_t key = 0; key < StringCache::capacity - StringCache::kin; ++key) {
        expect_miss(cache, std::to_string(key));
        insert(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

void insert_and_get() {
    StringCache cache;
    fill(cache);
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

void duplicate_at_capacity() {
    StringCache cache;
    fill(cache);
    insert(cache, "0", "updated");
    expect_hit(cache, "0", "updated");
    for (std::size_t key = 1; key < StringCache::capacity; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

void sequential_eviction() {
    StringCache cache;
    for (int key = 0; key < 40; ++key) {
        insert(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    for (std::size_t key = 0; key < 40 - StringCache::capacity; ++key) {
        expect_miss(cache, std::to_string(key));
    }
    for (std::size_t key = 40 - StringCache::capacity; key < 40; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

void a1in_hit_preserves_fifo() {
    StringCache cache;
    fill(cache);
    for (int access = 0; access < 10; ++access) {
        expect_hit(cache, "0", "value-0");
    }
    insert(cache, "new", "new");
    expect_miss(cache, "0");
    expect_hit(cache, "1", "value-1");
}

void a1in_update_preserves_fifo() {
    StringCache cache;
    fill(cache);
    insert(cache, "0", "updated");
    insert(cache, "new", "new");
    expect_miss(cache, "0");
    expect_hit(cache, "1", "value-1");
}

void ghost_lookup_is_miss() {
    StringCache cache;
    fill(cache);
    insert(cache, "new", "new");
    for (int access = 0; access < 10; ++access) {
        expect_miss(cache, "0");
    }
}

void ghost_reload_promotes_page() {
    StringCache cache;
    fill(cache);
    insert(cache, "new", "new");
    expect_miss(cache, "0");
    insert(cache, "0", "reloaded");
    expect_hit(cache, "0", "reloaded");
    expect_miss(cache, "1");
    for (int key = 0; key < 40; ++key) {
        insert(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_hit(cache, "0", "reloaded");
}

void forgotten_ghost_returns_to_a1in() {
    StringCache cache;
    fill(cache);
    for (std::size_t key = 0; key <= StringCache::kout; ++key) {
        insert(cache, "scan-" + std::to_string(key), "scan");
    }
    insert(cache, "0", "reloaded");
    expect_hit(cache, "0", "reloaded");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        insert(cache, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, "0");
}

void ghost_hit_preserves_fifo() {
    StringCache cache;
    fill(cache);
    insert(cache, "first", "first");
    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_miss(cache, "0");
        insert(cache, "scan-" + std::to_string(key), "scan");
    }
    insert(cache, "0", "reloaded");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        insert(cache, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, "0");
}

void newest_ghost_survives_history_limit() {
    StringCache cache;
    fill(cache);
    for (std::size_t key = 0; key <= StringCache::kout; ++key) {
        insert(cache, "scan-" + std::to_string(key), "scan");
    }
    const auto key = std::to_string(StringCache::kout);
    expect_miss(cache, key);
    insert(cache, key, "reloaded");
    for (std::size_t index = 0; index < StringCache::capacity; ++index) {
        insert(cache, "next-" + std::to_string(index), "next");
    }
    expect_hit(cache, key, "reloaded");
}

void kin_boundary_evicts_am() {
    StringCache cache;
    fill_frequent(cache);
    insert(cache, "new", "new");
    expect_miss(cache, "0");
    expect_hit(cache, "1", "value-1");
    expect_hit(cache, "7", "value-7");
    expect_hit(cache, "cold", "cold");
    insert(cache, "next", "next");
    expect_miss(cache, "7");
    expect_hit(cache, "cold", "cold");
    expect_hit(cache, "1", "value-1");
}

void am_hit_refreshes_recency() {
    StringCache cache;
    fill_frequent(cache);
    expect_hit(cache, "0", "value-0");
    insert(cache, "new", "new");
    expect_miss(cache, "1");
    expect_hit(cache, "0", "value-0");
}

void am_update_refreshes_recency() {
    StringCache cache;
    fill_frequent(cache);
    insert(cache, "0", "updated");
    insert(cache, "new", "new");
    expect_miss(cache, "1");
    expect_hit(cache, "0", "updated");
}

void ghost_reload_at_kin_evicts_am() {
    StringCache cache;
    fill_frequent(cache);
    expect_miss(cache, "6");
    insert(cache, "6", "reloaded");
    expect_miss(cache, "0");
    expect_hit(cache, "6", "reloaded");
    expect_hit(cache, "7", "value-7");
    expect_hit(cache, "cold", "cold");
}

void pointer_survives_am_hit() {
    StringCache cache;
    fill_frequent(cache);
    const auto* saved = expect_hit(cache, "0", "value-0");
    expect_hit(cache, "1", "value-1");
    require(expect_hit(cache, "0", "value-0") == saved,
            "frequent hit changed the resident data address");
    insert(cache, "new", "new");
    require(expect_hit(cache, "0", "value-0") == saved,
            "eviction of another page changed the resident data address");
}

void ghost_releases_data() {
    TWO_Q::Cache<std::shared_ptr<int>> cache;
    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> observer = payload;
    require(cache.insert("0", payload) == TWO_Q::Status::success, "insert payload");
    payload.reset();
    for (std::size_t key = 1; key < StringCache::capacity; ++key) {
        require(cache.insert(std::to_string(key), std::make_shared<int>(1)) ==
                    TWO_Q::Status::success, "fill shared data cache");
    }
    require(!observer.expired(), "resident data disappeared");
    require(cache.insert("new", std::make_shared<int>(2)) == TWO_Q::Status::success,
            "insert new payload");
    require(observer.expired(), "ghost entry retained its payload");
    const std::shared_ptr<int>* data = nullptr;
    require(cache.get("0", data) == TWO_Q::Status::not_found, "ghost must miss");
    require(data == nullptr, "ghost returned a pointer");
    require(cache.insert("0", std::make_shared<int>(7)) == TWO_Q::Status::success,
            "reload payload");
    require(cache.get("0", data) == TWO_Q::Status::success, "get reloaded payload");
    require(data != nullptr && *data && **data == 7, "reloaded payload mismatch");
}

class Workload {
public:
    void put(const std::string& key) {
        const auto value = key + "-revision-" + std::to_string(revision_++);
        insert(cache_, key, value);
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
                    require(!frequent_.empty(), "reference frequent queue is empty");
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
            expect_miss(cache_, key);
            return false;
        }
        expect_hit(cache_, key, found->second);
        refresh_frequent(key);
        return true;
    }

    void access(const std::string& key) {
        if (!get(key)) {
            put(key);
            get(key);
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
};

void repeated_reloads() {
    Workload workload;
    for (int cycle = 0; cycle < 100; ++cycle) {
        for (int key = 0; key < 12; ++key) {
            workload.access(std::to_string(key));
        }
    }
}

void mixed_workload() {
    Workload workload;
    std::uint32_t state = 0x12345678U;
    for (int step = 0; step < 4000; ++step) {
        state = state * 1664525U + 1013904223U;
        const auto key = std::to_string((state >> 16U) % 23U);
        if ((state & 3U) == 0) {
            workload.put(key);
        } else {
            workload.access(key);
        }
        for (int candidate = 0; candidate < 23; ++candidate) {
            workload.get(std::to_string(candidate));
        }
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
    {"repeated_hit", repeated_hit},
    {"duplicate_updates_value", duplicate_updates_value},
    {"empty_key_and_value", empty_key_and_value},
    {"embedded_null_key", embedded_null_key},
    {"integer_data", integer_data},
    {"non_default_data", non_default_data},
    {"independent_caches", independent_caches},
    {"pointer_survives_update", pointer_survives_update},
    {"update_releases_old_data", update_releases_old_data},
    {"destruction_releases_data", destruction_releases_data},
    {"insert_and_get", insert_and_get},
    {"duplicate_at_capacity", duplicate_at_capacity},
    {"sequential_eviction", sequential_eviction},
    {"a1in_hit_preserves_fifo", a1in_hit_preserves_fifo},
    {"a1in_update_preserves_fifo", a1in_update_preserves_fifo},
    {"ghost_lookup_is_miss", ghost_lookup_is_miss},
    {"ghost_reload_promotes_page", ghost_reload_promotes_page},
    {"forgotten_ghost_returns_to_a1in", forgotten_ghost_returns_to_a1in},
    {"ghost_hit_preserves_fifo", ghost_hit_preserves_fifo},
    {"newest_ghost_survives_history_limit", newest_ghost_survives_history_limit},
    {"kin_boundary_evicts_am", kin_boundary_evicts_am},
    {"am_hit_refreshes_recency", am_hit_refreshes_recency},
    {"am_update_refreshes_recency", am_update_refreshes_recency},
    {"ghost_reload_at_kin_evicts_am", ghost_reload_at_kin_evicts_am},
    {"pointer_survives_am_hit", pointer_survives_am_hit},
    {"ghost_releases_data", ghost_releases_data},
    {"repeated_reloads", repeated_reloads},
    {"mixed_workload", mixed_workload},
    {"hot_and_cold_workload", hot_and_cold_workload},
};

}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: 2q_tests <test_name>\nAvailable tests:\n";
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
