#include "belady_cache.hpp"
#include "SlowGetPage.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tests {
namespace {

constexpr std::size_t test_capacity = 8;

using StringCache = Belady::Cache<std::string, SlowGetPage<std::string>>;

int loader_calls = 0;
std::string expected_key;
std::string loaded_value;

std::string load_page(const std::string& url) {
    ++loader_calls;
    EXPECT_EQ(url, expected_key);
    return loaded_value;
}

std::string unexpected_load(const std::string&) {
    ++loader_calls;
    return "unexpected load";
}

std::string original_load(const std::string&) {
    return "original";
}

int load_integer(const std::string& key) {
    ++loader_calls;
    return key == "zero" ? 0 : -42;
}

std::string failing_load(const std::string& key) {
    ++loader_calls;
    EXPECT_EQ(key, expected_key);
    throw std::runtime_error("load failed");
}

void expect_load(StringCache& cache, SlowGetPage<std::string>& lower, const std::string& key,
                 const std::string& value) {
    loader_calls = 0;
    expected_key = key;
    loaded_value = value;
    EXPECT_EQ(fetch_with_loader(cache, lower, key, load_page), value);
    EXPECT_EQ(loader_calls, 1) << "expected load: " << key;
}

void expect_hit(StringCache& cache, SlowGetPage<std::string>& lower, const std::string& key,
                const std::string& value) {
    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, key, unexpected_load), value);
    EXPECT_EQ(loader_calls, 0) << "expected hit: " << key;
}

std::vector<std::string> resident_keys() {
    std::vector<std::string> keys;
    for (std::size_t index = 0; index < test_capacity; ++index) {
        keys.push_back(std::to_string(index));
    }
    return keys;
}

void fill(StringCache& cache, SlowGetPage<std::string>& lower) {
    for (const auto& key : resident_keys()) {
        expect_load(cache, lower, key, "value-" + key);
    }
}

TEST(Belady, empty_sequence) {
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, {});
}

TEST(Belady, uses_requested_capacity) {
    for (const std::size_t capacity : {1u, 2u, 3u, 11u}) {
        SCOPED_TRACE(capacity);
        std::vector<std::string> keys;
        for (std::size_t i = 0; i < capacity; ++i) {
            keys.push_back(std::to_string(i));
        }
        auto requests = keys;
        requests.push_back("new");
        requests.insert(requests.end(), keys.begin(), keys.end());

        SlowGetPage<std::string> lower;
        StringCache cache(lower, capacity, requests);
        for (const auto& key : keys) {
            expect_load(cache, lower, key, key);
        }
        expect_load(cache, lower, "new", "new");
        for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
            expect_hit(cache, lower, keys[i], keys[i]);
        }
        expect_load(cache, lower, keys.back(), keys.back());
    }
}

TEST(Belady, miss_loads_and_caches_value) {
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, {"page key", "page key"});

    expect_load(cache, lower, "page key", "page data");
    expect_hit(cache, lower, "page key", "page data");
}

TEST(Belady, hit_does_not_replace_value) {
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, {"A", "A", "A"});

    expect_load(cache, lower, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: A";
    expect_hit(cache, lower, "A", "original");
}

TEST(Belady, repeated_hit) {
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, std::vector<std::string>(101, "A"));

    expect_load(cache, lower, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, lower, "A", "value-A");
    }
}

TEST(Belady, hit_at_capacity_does_not_evict) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.insert(requests.end(), keys.begin(), keys.end());
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);
    fill(cache, lower);

    for (const auto& key : keys) {
        expect_hit(cache, lower, key, "value-" + key);
    }
}

TEST(Belady, empty_key_and_value) {
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, {"", ""});

    expect_load(cache, lower, "", "");
    expect_hit(cache, lower, "", "");
}

TEST(Belady, embedded_null_key) {
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, {key, "a", key, "a"});

    expect_load(cache, lower, key, value);
    expect_load(cache, lower, "a", "prefix");
    expect_hit(cache, lower, key, value);
    expect_hit(cache, lower, "a", "prefix");
}

TEST(Belady, integer_data) {
    SlowGetPage<int> lower;
    Belady::Cache<int, SlowGetPage<int>> cache(lower, test_capacity, {"zero", "negative", "zero", "negative"});

    loader_calls = 0;
    for (int access = 0; access < 2; ++access) {
        EXPECT_EQ(fetch_with_loader(cache, lower, "zero", load_integer), 0);
        EXPECT_EQ(fetch_with_loader(cache, lower, "negative", load_integer), -42);
    }
    EXPECT_EQ(loader_calls, 2);
}

struct Payload {
    Payload(const int value) : value_(value) {}
    int value_;
};

Payload load_payload(const std::string&) {
    ++loader_calls;
    return Payload{42};
}

TEST(Belady, non_default_data) {
    SlowGetPage<Payload> lower;
    Belady::Cache<Payload, SlowGetPage<Payload>> cache(lower, test_capacity, {"A", "A"});

    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(Belady, returned_value_is_a_copy) {
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, {"A", "A"});

    auto value = fetch_with_loader(cache, lower, "A", original_load);
    value = "modified";
    expect_hit(cache, lower, "A", "original");
}

TEST(Belady, independent_caches) {
    SlowGetPage<std::string> lower_first;
    StringCache first(lower_first, test_capacity, {"A", "A"});
    SlowGetPage<std::string> lower_second;
    StringCache second(lower_second, test_capacity, {"A", "A"});

    expect_load(first, lower_first, "A", "first");
    expect_load(second, lower_second, "A", "second");
    expect_hit(first, lower_first, "A", "first");
    expect_hit(second, lower_second, "A", "second");
}

TEST(Belady, request_sequence_is_not_borrowed) {
    std::vector<std::string> requests = {"A", "A"};
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);

    requests.assign(20, "B");
    expect_load(cache, lower, "A", "value-A");
    expect_hit(cache, lower, "A", "value-A");
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
        SlowGetPage<std::string> lower;
        StringCache cache(lower, test_capacity, requests);
        fill(cache, lower);
        expect_load(cache, lower, "new", "value-new");
        for (const auto& key : keys) {
            if (key != keys[victim]) {
                expect_hit(cache, lower, key, "value-" + key);
            }
        }
        expect_load(cache, lower, keys[victim], "reloaded");
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
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);
    fill(cache, lower);

    expect_hit(cache, lower, keys.front(), "value-" + keys.front());
    expect_load(cache, lower, "new", "value-new");
    for (auto key = keys.begin() + 1; key != keys.end(); ++key) {
        expect_hit(cache, lower, *key, "value-" + *key);
    }
    expect_load(cache, lower, keys.front(), "reloaded");
    expect_hit(cache, lower, keys.front(), "reloaded");
}

TEST(Belady, never_used_again_is_evicted_first) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back(keys.back());
    requests.push_back("new");
    requests.insert(requests.end(), keys.begin(), keys.end() - 1);
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);
    fill(cache, lower);

    expect_hit(cache, lower, keys.back(), "value-" + keys.back());
    expect_load(cache, lower, "new", "value-new");
    for (auto key = keys.begin(); key != keys.end() - 1; ++key) {
        expect_hit(cache, lower, *key, "value-" + *key);
    }
}

TEST(Belady, incoming_page_is_admitted_before_its_distant_reuse) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back("new");
    requests.insert(requests.end(), keys.begin(), keys.end());
    requests.push_back("new");
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);
    fill(cache, lower);

    expect_load(cache, lower, "new", "value-new");
    for (auto key = keys.begin(); key != keys.end() - 1; ++key) {
        expect_hit(cache, lower, *key, "value-" + *key);
    }
    expect_load(cache, lower, keys.back(), "reloaded");
    expect_hit(cache, lower, "new", "value-new");
}

TEST(Belady, sequential_eviction) {
    std::vector<std::string> requests;
    for (int key = 0; key < 100; ++key) {
        requests.push_back(std::to_string(key));
    }
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);

    for (const auto& key : requests) {
        expect_load(cache, lower, key, "value-" + key);
    }
}

TEST(Belady, hot_pages_survive_cold_scan) {
    std::vector<std::string> requests;
    for (int step = 0; step < 200; ++step) {
        requests.push_back("hot-A");
        requests.push_back("hot-B");
        requests.push_back("cold-" + std::to_string(step));
    }
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);

    for (int step = 0; step < 200; ++step) {
        for (const auto* key : {"hot-A", "hot-B"}) {
            if (step == 0) {
                expect_load(cache, lower, key, key);
            } else {
                expect_hit(cache, lower, key, key);
            }
        }
        const auto key = "cold-" + std::to_string(step);
        expect_load(cache, lower, key, key);
    }
}

TEST(Belady, loader_exception_preserves_resident_data) {
    const auto keys = resident_keys();
    auto requests = keys;
    requests.push_back("failed");
    requests.insert(requests.end(), keys.begin(), keys.end());
    requests.push_back("failed");
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, requests);
    fill(cache, lower);

    loader_calls = 0;
    expected_key = "failed";
    EXPECT_THROW(fetch_with_loader(cache, lower, "failed", failing_load), std::runtime_error);
    EXPECT_EQ(loader_calls, 1);
    for (const auto& key : keys) {
        expect_hit(cache, lower, key, "value-" + key);
    }
    expect_load(cache, lower, "failed", "loaded");
}

TEST(Belady, empty_loader_is_only_needed_on_miss) {
    SlowGetPage<std::string> lower;
    StringCache cache(lower, test_capacity, {"A", "A", "missing", "missing"});

    expect_load(cache, lower, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", empty_loader), "value-A");
    EXPECT_THROW(fetch_with_loader(cache, lower, "missing", empty_loader), std::bad_function_call);
    expect_load(cache, lower, "missing", "loaded");
}

}
}
