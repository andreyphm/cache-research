#include "belady_cache.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Tests {
namespace {

using StringCache = Belady::Cache<std::string>;

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

std::vector<std::string> resident_keys() {
    std::vector<std::string> keys;
    for (std::size_t index = 0; index < StringCache::capacity; ++index) {
        keys.push_back(std::to_string(index));
    }
    return keys;
}

void fill(StringCache& cache) {
    for (const auto& key : resident_keys()) {
        expect_load(cache, key, "value-" + key);
    }
}

TEST(Belady, empty_sequence) {
    StringCache cache({});
}

TEST(Belady, miss_loads_and_caches_value) {
    StringCache cache({"missing", "missing"});
    expect_load(cache, "missing", "loaded");
    expect_hit(cache, "missing", "loaded");
}

TEST(Belady, hit_does_not_replace_value) {
    StringCache cache({"A", "A", "A"});
    expect_load(cache, "A", "original");
    EXPECT_EQ(cache.fetch("A", [](const std::string&) {
        ADD_FAILURE() << "loader called for a resident page";
        return std::string("replacement");
    }), "original");
    expect_hit(cache, "A", "original");
}

TEST(Belady, repeated_hit) {
    StringCache cache(std::vector<std::string>(101, "A"));
    expect_load(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(Belady, hit_at_capacity_does_not_evict) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.insert(requests.end(), keys.begin(), keys.end());
    StringCache cache(requests);
    fill(cache);
    for (const auto& key : keys) {
        expect_hit(cache, key, "value-" + key);
    }
}

TEST(Belady, empty_key_and_value) {
    StringCache cache({"", ""});
    expect_load(cache, "", "");
    expect_hit(cache, "", "");
}

TEST(Belady, embedded_null_key) {
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    StringCache cache({key, "a", key, "a"});
    expect_load(cache, key, value);
    expect_load(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(Belady, integer_data) {
    Belady::Cache<int> cache({"zero", "negative", "zero", "negative"});
    int calls = 0;
    const auto loader = [&](const std::string& key) {
        ++calls;
        return key == "zero" ? 0 : -42;
    };
    for (int access = 0; access < 2; ++access) {
        EXPECT_EQ(cache.fetch("zero", loader), 0);
        EXPECT_EQ(cache.fetch("negative", loader), -42);
    }
    EXPECT_EQ(calls, 2);
}

struct Payload {
    explicit Payload(int value) : value_(value) {}
    int value_;
};

TEST(Belady, non_default_data) {
    Belady::Cache<Payload> cache({"A", "A"});
    int calls = 0;
    const auto loader = [&](const std::string&) {
        ++calls;
        return Payload{42};
    };
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(cache.fetch("A", loader).value_, 42);
    EXPECT_EQ(calls, 1);
}

TEST(Belady, returned_value_is_a_copy) {
    StringCache cache({"A", "A"});
    auto value = cache.fetch("A", [](const std::string&) {
        return std::string("original");
    });
    value = "modified";
    expect_hit(cache, "A", "original");
}

TEST(Belady, independent_caches) {
    StringCache first({"A", "A"});
    StringCache second({"A", "A"});
    expect_load(first, "A", "first");
    expect_load(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(Belady, request_sequence_is_not_borrowed) {
    std::vector<std::string> requests = {"A", "A"};
    StringCache cache(requests);
    requests.assign(20, "B");
    expect_load(cache, "A", "value-A");
    expect_hit(cache, "A", "value-A");
}

TEST(Belady, evicts_farthest_next_use) {
    const auto keys = resident_keys();
    for (std::size_t victim = 0; victim < keys.size(); ++victim) {
        SCOPED_TRACE("victim=" + keys[victim]);
        auto requests = keys;
        requests.push_back("new");
        for (const auto& key : keys) {
            if (key != keys[victim]) {
                requests.push_back(key);
            }
        }
        requests.push_back(keys[victim]);
        StringCache cache(requests);
        fill(cache);
        expect_load(cache, "new", "value-new");
        for (const auto& key : keys) {
            if (key != keys[victim]) {
                expect_hit(cache, key, "value-" + key);
            }
        }
        expect_load(cache, keys[victim], "reloaded");
    }
}

TEST(Belady, hit_updates_next_use_before_eviction) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back(keys.front());
    requests.push_back("new");
    requests.insert(requests.end(), keys.begin() + 1, keys.end());
    requests.push_back(keys.front());
    requests.push_back(keys.front());
    StringCache cache(requests);
    fill(cache);
    expect_hit(cache, keys.front(), "value-" + keys.front());
    expect_load(cache, "new", "value-new");
    for (auto key = keys.begin() + 1; key != keys.end(); ++key) {
        expect_hit(cache, *key, "value-" + *key);
    }
    expect_load(cache, keys.front(), "reloaded");
    expect_hit(cache, keys.front(), "reloaded");
}

TEST(Belady, never_used_again_is_evicted_first) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back(keys.back());
    requests.push_back("new");
    requests.insert(requests.end(), keys.begin(), keys.end() - 1);
    StringCache cache(requests);
    fill(cache);
    expect_hit(cache, keys.back(), "value-" + keys.back());
    expect_load(cache, "new", "value-new");
    for (auto key = keys.begin(); key != keys.end() - 1; ++key) {
        expect_hit(cache, *key, "value-" + *key);
    }
}

TEST(Belady, incoming_page_is_admitted_before_its_distant_reuse) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back("new");
    requests.insert(requests.end(), keys.begin(), keys.end());
    requests.push_back("new");
    StringCache cache(requests);
    fill(cache);
    expect_load(cache, "new", "value-new");
    for (auto key = keys.begin(); key != keys.end() - 1; ++key) {
        expect_hit(cache, *key, "value-" + *key);
    }
    expect_load(cache, keys.back(), "reloaded");
    expect_hit(cache, "new", "value-new");
}

TEST(Belady, sequential_eviction) {
    std::vector<std::string> requests;
    for (int key = 0; key < 100; ++key) {
        requests.push_back(std::to_string(key));
    }
    StringCache cache(requests);
    for (const auto& key : requests) {
        expect_load(cache, key, "value-" + key);
    }
}

TEST(Belady, hot_pages_survive_cold_scan) {
    std::vector<std::string> requests;
    for (int step = 0; step < 200; ++step) {
        requests.push_back("hot-A");
        requests.push_back("hot-B");
        requests.push_back("cold-" + std::to_string(step));
    }
    StringCache cache(requests);
    for (int step = 0; step < 200; ++step) {
        for (const auto* key : {"hot-A", "hot-B"}) {
            if (step == 0) {
                expect_load(cache, key, key);
            } else {
                expect_hit(cache, key, key);
            }
        }
        const auto key = "cold-" + std::to_string(step);
        expect_load(cache, key, key);
    }
}

TEST(Belady, loader_exception_preserves_resident_data) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back("failed");
    requests.insert(requests.end(), keys.begin(), keys.end());
    requests.push_back("failed");
    StringCache cache(requests);
    fill(cache);
    int calls = 0;
    EXPECT_THROW((void)cache.fetch("failed", [&](const std::string& key) -> std::string {
        ++calls;
        EXPECT_EQ(key, "failed");
        throw std::runtime_error("load failed");
    }), std::runtime_error);
    EXPECT_EQ(calls, 1);
    for (const auto& key : keys) {
        expect_hit(cache, key, "value-" + key);
    }
    expect_load(cache, "failed", "loaded");
}

TEST(Belady, empty_loader_is_only_needed_on_miss) {
    StringCache cache({"A", "A", "missing", "missing"});
    expect_load(cache, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(cache.fetch("A", empty_loader), "value-A");
    EXPECT_THROW((void)cache.fetch("missing", empty_loader), std::bad_function_call);
    expect_load(cache, "missing", "loaded");
}

TEST(Belady, eviction_releases_data) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back("new");
    requests.insert(requests.end(), keys.begin() + 1, keys.end());
    Belady::Cache<std::shared_ptr<int>> cache(requests);
    std::weak_ptr<int> observer;
    (void)cache.fetch(keys.front(), [&](const std::string&) {
        auto payload = std::make_shared<int>(42);
        observer = payload;
        return payload;
    });
    const auto loader = [](const std::string&) { return std::make_shared<int>(1); };
    for (auto key = keys.begin() + 1; key != keys.end(); ++key) {
        (void)cache.fetch(*key, loader);
    }
    ASSERT_FALSE(observer.expired());
    (void)cache.fetch("new", loader);
    EXPECT_TRUE(observer.expired());
}

TEST(Belady, destruction_releases_data) {
    std::weak_ptr<int> observer;
    {
        Belady::Cache<std::shared_ptr<int>> cache({"A"});
        (void)cache.fetch("A", [&](const std::string&) {
            auto payload = std::make_shared<int>(42);
            observer = payload;
            return payload;
        });
        ASSERT_FALSE(observer.expired());
    }
    EXPECT_TRUE(observer.expired());
}

std::size_t minimum_misses(const std::vector<unsigned>& requests, std::size_t pos,
                           std::uint64_t resident,
                           std::unordered_map<std::uint64_t, std::size_t>& memo) {
    if (pos == requests.size()) {
        return 0;
    }
    const std::uint64_t state = (static_cast<std::uint64_t>(pos) << 16U) | resident;
    const auto found = memo.find(state);
    if (found != memo.end()) {
        return found->second;
    }

    const std::uint64_t page = std::uint64_t{1} << requests[pos];
    std::size_t result;
    if (resident & page) {
        result = minimum_misses(requests, pos + 1, resident, memo);
    } else if (static_cast<std::size_t>(std::popcount(resident)) < StringCache::capacity) {
        result = 1 + minimum_misses(requests, pos + 1, resident | page, memo);
    } else {
        result = requests.size();
        for (std::uint64_t remaining = resident; remaining != 0; remaining &= remaining - 1) {
            const std::uint64_t victim = remaining & (~remaining + 1);
            result = std::min(result, 1 + minimum_misses(requests, pos + 1,
                                                       (resident ^ victim) | page, memo));
        }
    }
    memo.emplace(state, result);
    return result;
}

TEST(Belady, mixed_workload_matches_exhaustive_optimum) {
    static_assert(StringCache::capacity + 4 <= 16);
    std::uint32_t state = 0x12345678U;
    for (int trial = 0; trial < 100; ++trial) {
        SCOPED_TRACE("trial=" + std::to_string(trial));
        std::vector<unsigned> input;
        for (unsigned key = 0; key < StringCache::capacity; ++key) {
            input.push_back(key);
        }
        for (int step = 0; step < 24; ++step) {
            state = state * 1664525U + 1013904223U;
            input.push_back((state >> 16U) % (StringCache::capacity + 4));
        }
        std::vector<std::string> requests;
        for (unsigned key : input) {
            requests.push_back(std::to_string(key));
        }
        StringCache cache(requests);
        std::size_t misses = 0;
        for (const auto& key : requests) {
            EXPECT_EQ(cache.fetch(key, [&](const std::string& url) {
                ++misses;
                EXPECT_EQ(url, key);
                return "value-" + url;
            }), "value-" + key);
        }
        std::unordered_map<std::uint64_t, std::size_t> memo;
        EXPECT_EQ(misses, minimum_misses(input, 0, 0, memo));
    }
}

}
}
