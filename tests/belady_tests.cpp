#include "belady_cache.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tests {
namespace {

using StringCache = Belady::Cache<std::string>;

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

void expect_load(StringCache& cache, const std::string& key,
                 const std::string& value) {
    loader_calls = 0;
    expected_key = key;
    loaded_value = value;
    EXPECT_EQ(cache.fetch(key, load_page), value);
    EXPECT_EQ(loader_calls, 1) << "expected load: " << key;
}

void expect_hit(StringCache& cache, const std::string& key,
                const std::string& value) {
    loader_calls = 0;
    EXPECT_EQ(cache.fetch(key, unexpected_load), value);
    EXPECT_EQ(loader_calls, 0) << "expected hit: " << key;
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
    StringCache cache({"page key", "page key"});

    expect_load(cache, "page key", "page data");
    expect_hit(cache, "page key", "page data");
}

TEST(Belady, hit_does_not_replace_value) {
    StringCache cache({"A", "A", "A"});

    expect_load(cache, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: A";
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

    loader_calls = 0;
    for (int access = 0; access < 2; ++access) {
        EXPECT_EQ(cache.fetch("zero", load_integer), 0);
        EXPECT_EQ(cache.fetch("negative", load_integer), -42);
    }
    EXPECT_EQ(loader_calls, 2);
}

struct Payload {
    Payload(int value) : value_(value) {}
    int value_;
};

Payload load_payload(const std::string&) {
    ++loader_calls;
    return Payload{42};
}

TEST(Belady, non_default_data) {
    Belady::Cache<Payload> cache({"A", "A"});

    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(Belady, returned_value_is_a_copy) {
    StringCache cache({"A", "A"});

    auto value = cache.fetch("A", original_load);
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

    loader_calls = 0;
    expected_key = "failed";
    EXPECT_THROW((void)cache.fetch("failed", failing_load), std::runtime_error);
    EXPECT_EQ(loader_calls, 1);
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

}
}
