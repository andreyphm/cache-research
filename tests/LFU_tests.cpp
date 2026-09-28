#include "LFU_cache.hpp"
#include "SlowGetPage.hpp"

#include <gtest/gtest.h>

#include <cstddef>

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Tests {
namespace {

constexpr std::size_t test_capacity = 4;

using StringCache = LFU::Cache<std::string, SlowGetPage<std::string>>;

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

void fill(StringCache& cache, SlowGetPage<std::string>& lower) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_load(cache, lower, key, std::string("value-") + key);
    }
}

TEST(LFU, miss_loads_and_caches_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "page key", "page data");
    expect_hit(cache, lower, "page key", "page data");
}

TEST(LFU, hit_does_not_replace_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: " << "A";
    expect_hit(cache, lower, "A", "original");
}

TEST(LFU, repeated_hit) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, lower, "A", "value-A");
    }
}

TEST(LFU, hit_at_capacity_does_not_evict) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_hit(cache, lower, "B", "value-B");
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
}

TEST(LFU, empty_key_and_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "", "");
    expect_hit(cache, lower, "", "");
}

TEST(LFU, embedded_null_key) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);

    expect_load(cache, lower, key, value);
    expect_load(cache, lower, "a", "prefix");
    expect_hit(cache, lower, key, value);
    expect_hit(cache, lower, "a", "prefix");
}

TEST(LFU, integer_data) {
    SlowGetPage<int> lower;
    LFU::Cache<int, SlowGetPage<int>> cache{lower, test_capacity};

    loader_calls = 0;
    for (int access = 0; access < 3; ++access) {
        EXPECT_EQ(fetch_with_loader(cache, lower, "zero", load_integer), 0);
        EXPECT_EQ(fetch_with_loader(cache, lower, "negative", load_integer), -42);
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
    SlowGetPage<Payload> lower;
    LFU::Cache<Payload, SlowGetPage<Payload>> cache{lower, test_capacity};

    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(LFU, returned_value_is_a_copy) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    auto value = fetch_with_loader(cache, lower, "A", original_load);
    value = "modified";
    expect_hit(cache, lower, "A", "original");
}

TEST(LFU, independent_caches) {
    SlowGetPage<std::string> lower_first;
    StringCache first{lower_first, test_capacity};
    SlowGetPage<std::string> lower_second;
    StringCache second{lower_second, test_capacity};

    expect_load(first, lower_first, "A", "first");
    expect_load(second, lower_second, "A", "second");
    expect_hit(first, lower_first, "A", "first");
    expect_hit(second, lower_second, "A", "second");
}

TEST(LFU, sequential_eviction) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    for (int key = 0; key < 40; ++key) {
        expect_load(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
    for (int key = 36; key < 40; ++key) {
        expect_hit(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
    for (int key = 0; key < 36; ++key) {
        expect_load(cache, lower, std::to_string(key), "reloaded-" + std::to_string(key));
    }
}

TEST(LFU, promotion_protects_page) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_hit(cache, lower, "A", "value-A");
    expect_load(cache, lower, "E", "value-E");
    for (const auto* key : {"A", "E", "C", "D"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "B", "reloaded-B");
}

TEST(LFU, frequency_beats_recency) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_hit(cache, lower, "A", "value-A");
    for (const auto* key : {"E", "F", "G", "H"}) {
        expect_load(cache, lower, key, std::string("value-") + key);
    }
    for (const auto* key : {"A", "F", "G", "H"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_load(cache, lower, key, std::string("reloaded-") + key);
    }
}

TEST(LFU, equal_frequency_evicts_oldest_page) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_hit(cache, lower, "A", "value-A");
    expect_hit(cache, lower, "B", "value-B");
    expect_load(cache, lower, "E", "value-E");
    expect_load(cache, lower, "F", "value-F");
    for (const auto* key : {"A", "B", "E", "F"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "C", "reloaded-C");
    expect_load(cache, lower, "D", "reloaded-D");
}

TEST(LFU, all_pages_frequent_admit_new_page) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "E", "value-E");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "A", "reloaded-A");
}

TEST(LFU, reload_uses_fresh_data_and_resets_frequency) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_load(cache, lower, "E", "value-E");
    expect_load(cache, lower, "A", "reloaded-A");
    for (const auto* key : {"F", "G", "H", "I"}) {
        expect_load(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "A", "latest-A");
    expect_hit(cache, lower, "A", "latest-A");
}

TEST(LFU, loader_exception_does_not_change_cache) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    loader_calls = 0;
    EXPECT_THROW(fetch_with_loader(cache, lower, "E", failing_load), std::runtime_error);
    EXPECT_THROW(fetch_with_loader(cache, lower, "E", failing_load), std::runtime_error);
    EXPECT_EQ(loader_calls, 2);
    expect_load(cache, lower, "E", "value-E");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "A", "reloaded-A");
}

TEST(LFU, empty_loader_is_only_needed_on_miss) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", empty_loader), "value-A");
    EXPECT_THROW(fetch_with_loader(cache, lower, "missing", empty_loader), std::bad_function_call);
    expect_load(cache, lower, "missing", "loaded");
}

}
}
