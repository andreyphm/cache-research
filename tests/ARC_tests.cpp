#include "ARC_cache.hpp"
#include "SlowGetPage.hpp"

#include <gtest/gtest.h>

#include <cstddef>

#include <functional>
#include <string>

namespace Tests {
namespace {

constexpr std::size_t test_capacity = 4;

using StringCache = ARC::Cache<std::string, SlowGetPage<std::string>>;

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

struct LoadStopped {};

std::string stopped_load(const std::string& url) {
    ++loader_calls;
    EXPECT_EQ(url, expected_key);
    throw LoadStopped{};
}

void expect_miss(StringCache& cache, SlowGetPage<std::string>& lower, const std::string& key) {
    loader_calls = 0;
    expected_key = key;
    EXPECT_THROW(fetch_with_loader(cache, lower, key, stopped_load), LoadStopped);
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

void fill(StringCache& cache, SlowGetPage<std::string>& lower) {
    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_load(cache, lower, key, std::string("value-") + key);
    }
}

TEST(ARC, miss_loads_and_caches_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "page key", "page data");
    expect_hit(cache, lower, "page key", "page data");
}

TEST(ARC, hit_does_not_replace_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: A";
    expect_hit(cache, lower, "A", "original");
}

TEST(ARC, repeated_hit) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, lower, "A", "value-A");
    }
}

TEST(ARC, empty_key_and_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "", "");
    expect_hit(cache, lower, "", "");
}

TEST(ARC, embedded_null_key) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);

    expect_load(cache, lower, key, value);
    expect_load(cache, lower, "a", "prefix");
    expect_hit(cache, lower, key, value);
    expect_hit(cache, lower, "a", "prefix");
}

TEST(ARC, integer_data) {
    SlowGetPage<int> lower;
    ARC::Cache<int, SlowGetPage<int>> cache{lower, test_capacity};

    loader_calls = 0;
    for (int access = 0; access < 3; ++access) {
        EXPECT_EQ(fetch_with_loader(cache, lower, "zero", load_integer), 0);
        EXPECT_EQ(fetch_with_loader(cache, lower, "negative", load_integer), -42);
    }
    EXPECT_EQ(loader_calls, 2);
}

TEST(ARC, non_default_data) {
    SlowGetPage<Payload> lower;
    ARC::Cache<Payload, SlowGetPage<Payload>> cache{lower, test_capacity};

    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(ARC, returned_value_is_a_copy) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    auto value = fetch_with_loader(cache, lower, "A", original_load);
    value = "modified";
    expect_hit(cache, lower, "A", "original");
}

TEST(ARC, independent_caches) {
    SlowGetPage<std::string> lower_first;
    StringCache first{lower_first, test_capacity};
    SlowGetPage<std::string> lower_second;
    StringCache second{lower_second, test_capacity};

    expect_load(first, lower_first, "A", "first");
    expect_load(second, lower_second, "A", "second");
    expect_hit(first, lower_first, "A", "first");
    expect_hit(second, lower_second, "A", "second");
}

TEST(ARC, empty_loader_is_only_needed_on_miss) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", empty_loader), "value-A");
    EXPECT_THROW(fetch_with_loader(cache, lower, "missing", empty_loader), std::bad_function_call);
    expect_load(cache, lower, "missing", "loaded");
}

TEST(ARC, fetch_at_capacity) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
}

TEST(ARC, sequential_eviction) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    for (int key = 0; key < 40; ++key) {
        expect_load(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
    for (int key = 0; key < 36; ++key) {
        expect_miss(cache, lower, std::to_string(key));
    }
    for (int key = 36; key < 40; ++key) {
        expect_hit(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(ARC, promotion_protects_page) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_hit(cache, lower, "A", "value-A");
    expect_load(cache, lower, "E", "value-E");
    expect_hit(cache, lower, "A", "value-A");
    expect_miss(cache, lower, "B");
    expect_hit(cache, lower, "E", "value-E");
}

TEST(ARC, all_pages_frequent) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "E", "value-E");
    expect_miss(cache, lower, "A");
    for (const auto* key : {"B", "C", "D", "E"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
}

TEST(ARC, ghost_lookup_is_miss) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_hit(cache, lower, "A", "value-A");
    expect_load(cache, lower, "E", "value-E");
    for (int access = 0; access < 3; ++access) {
        expect_miss(cache, lower, "B");
    }
}

TEST(ARC, reload_b1) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_hit(cache, lower, "A", "value-A");
    expect_load(cache, lower, "E", "value-E");
    const auto initial_parameter = cache.get_size_parameter();
    expect_miss(cache, lower, "B");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter);
    expect_load(cache, lower, "B", "reloaded-B");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter + 1);
    expect_hit(cache, lower, "B", "reloaded-B");
    expect_miss(cache, lower, "A");
    EXPECT_LE(cache.get_size_parameter(), test_capacity);
}

TEST(ARC, reload_b2) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (const auto* key : {"A", "B", "C", "D"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "E", "value-E");
    const auto initial_parameter = cache.get_size_parameter();
    expect_miss(cache, lower, "A");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter);
    expect_load(cache, lower, "A", "reloaded-A");
    EXPECT_EQ(cache.get_size_parameter(), initial_parameter - 1);
    expect_hit(cache, lower, "A", "reloaded-A");
    expect_miss(cache, lower, "E");
    EXPECT_LE(cache.get_size_parameter(), test_capacity);
}

TEST(ARC, loader_exception_preserves_residents) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_miss(cache, lower, "failed-load");
    expect_miss(cache, lower, "failed-load");
    for (const auto& key : {"A", "B", "C", "D"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "failed-load", "recovered");
    expect_hit(cache, lower, "failed-load", "recovered");
}

TEST(ARC, t2_hit_refreshes_recency) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (const auto* key : {"A", "B", "C", "D", "A"}) {
        expect_hit(cache, lower, key, std::string("value-") + key);
    }
    expect_load(cache, lower, "E", "value-E");
    expect_miss(cache, lower, "B");
    expect_hit(cache, lower, "A", "value-A");
}

}
}
