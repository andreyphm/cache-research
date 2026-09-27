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
    EXPECT_EQ(key, "E");
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

void fill(StringCache& cache) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_load(cache, key, std::string("value-") + key);
    }
}

TEST(LFU, miss_loads_and_caches_value) {
    StringCache cache;

    expect_load(cache, "page key", "page data");
    expect_hit(cache, "page key", "page data");
}

TEST(LFU, hit_does_not_replace_value) {
    StringCache cache;

    expect_load(cache, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: " << "A";
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

    loader_calls = 0;
    for (int access = 0; access < 3; ++access) {
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

TEST(LFU, non_default_data) {
    LFU::Cache<Payload> cache;

    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(LFU, returned_value_is_a_copy) {
    StringCache cache;

    auto value = cache.fetch("A", original_load);
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
    for (const auto* key : {"A", "E", "C", "D"}) {
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

    loader_calls = 0;
    EXPECT_THROW((void)cache.fetch("E", failing_load), std::runtime_error);
    EXPECT_THROW((void)cache.fetch("E", failing_load), std::runtime_error);
    EXPECT_EQ(loader_calls, 2);
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

}
}
