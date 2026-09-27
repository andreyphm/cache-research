#include "ARC_cache.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace Tests {
namespace {

using StringCache = ARC::Cache<std::string>;

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

struct LoadStopped {};

std::string stopped_load(const std::string& url) {
    ++loader_calls;
    EXPECT_EQ(url, expected_key);
    throw LoadStopped{};
}

void expect_miss(StringCache& cache, const std::string& key) {
    loader_calls = 0;
    expected_key = key;
    EXPECT_THROW((void)cache.fetch(key, stopped_load), LoadStopped);
    EXPECT_EQ(loader_calls, 1) << "expected miss: " << key;
}

struct Payload {
    Payload(int value) : value_(value) {}
    int value_;
};

Payload load_payload(const std::string&) {
    ++loader_calls;
    return Payload{42};
}

void fill(StringCache& cache) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_load(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, miss_loads_and_caches_value) {
    StringCache cache;

    expect_load(cache, "page key", "page data");
    expect_hit(cache, "page key", "page data");
}

TEST(ARC, hit_does_not_replace_value) {
    StringCache cache;

    expect_load(cache, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: A";
    expect_hit(cache, "A", "original");
}

TEST(ARC, repeated_hit) {
    StringCache cache;

    expect_load(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(ARC, empty_key_and_value) {
    StringCache cache;

    expect_load(cache, "", "");
    expect_hit(cache, "", "");
}

TEST(ARC, embedded_null_key) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);

    expect_load(cache, key, value);
    expect_load(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(ARC, integer_data) {
    ARC::Cache<int> cache;

    loader_calls = 0;
    for (int access = 0; access < 3; ++access) {
        EXPECT_EQ(cache.fetch("zero", load_integer), 0);
        EXPECT_EQ(cache.fetch("negative", load_integer), -42);
    }
    EXPECT_EQ(loader_calls, 2);
}

TEST(ARC, non_default_data) {
    ARC::Cache<Payload> cache;

    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(ARC, returned_value_is_a_copy) {
    StringCache cache;

    auto value = cache.fetch("A", original_load);
    value = "modified";
    expect_hit(cache, "A", "original");
}

TEST(ARC, independent_caches) {
    StringCache first;
    StringCache second;

    expect_load(first, "A", "first");
    expect_load(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(ARC, empty_loader_is_only_needed_on_miss) {
    StringCache cache;

    expect_load(cache, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(cache.fetch("A", empty_loader), "value-A");
    EXPECT_THROW((void)cache.fetch("missing", empty_loader), std::bad_function_call);
    expect_load(cache, "missing", "loaded");
}

TEST(ARC, fetch_at_capacity) {
    StringCache cache;
    fill(cache);

    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, sequential_eviction) {
    StringCache cache;

    for (int key = 0; key < 40; ++key) {
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
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
    expect_load(cache, "E", "value-E");
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
    expect_load(cache, "E", "value-E");
    expect_miss(cache, "A");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
}

TEST(ARC, ghost_lookup_is_miss) {
    StringCache cache;
    fill(cache);

    expect_hit(cache, "A", "value-A");
    expect_load(cache, "E", "value-E");
    for (int access = 0; access < 3; ++access) {
        expect_miss(cache, "B");
    }
}

TEST(ARC, reload_b1) {
    StringCache cache;
    fill(cache);

    expect_hit(cache, "A", "value-A");
    expect_load(cache, "E", "value-E");
    const auto initial_parameter = cache.get_size_parameter();
    expect_miss(cache, "B");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter);
    expect_load(cache, "B", "reloaded-B");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter + 1);
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
    expect_load(cache, "E", "value-E");
    const auto initial_parameter = cache.get_size_parameter();
    expect_miss(cache, "A");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter);
    expect_load(cache, "A", "reloaded-A");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter - 1);
    expect_hit(cache, "A", "reloaded-A");
    expect_miss(cache, "E");
    ASSERT_LE(cache.get_size_parameter(), StringCache::capacity)
        << "adaptation parameter exceeds capacity";
}

TEST(ARC, loader_exception_preserves_residents) {
    StringCache cache;
    fill(cache);

    expect_miss(cache, "failed-load");
    expect_miss(cache, "failed-load");
    for (const auto& key : {"A", "B", "C", "D"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "failed-load", "recovered");
    expect_hit(cache, "failed-load", "recovered");
}

TEST(ARC, t2_hit_refreshes_recency) {
    StringCache cache;
    fill(cache);

    for (const auto* key : {"A", "B", "C", "D", "A"}) {
        expect_hit(cache, key, std::string("value-") + key);
    }
    expect_load(cache, "E", "value-E");
    expect_miss(cache, "B");
    expect_hit(cache, "A", "value-A");
}

}
}
