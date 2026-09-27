#include "2Q_cache.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <string>

namespace Tests {
namespace {

using StringCache = TWO_Q::Cache<std::string>;

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
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

void fill_frequent(StringCache& cache) {
    fill(cache);

    expect_load(cache, "cold", "cold");
    for (std::size_t key = 0; key < StringCache::capacity - StringCache::kin; ++key) {
        expect_miss(cache, std::to_string(key));
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, miss_loads_and_caches_value) {
    StringCache cache;

    expect_load(cache, "page key", "page data");
    expect_hit(cache, "page key", "page data");
}

TEST(TwoQ, hit_does_not_replace_value) {
    StringCache cache;

    expect_load(cache, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: A";
    expect_hit(cache, "A", "original");
}

TEST(TwoQ, repeated_hit) {
    StringCache cache;

    expect_load(cache, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, "A", "value-A");
    }
}

TEST(TwoQ, empty_key_and_value) {
    StringCache cache;

    expect_load(cache, "", "");
    expect_hit(cache, "", "");
}

TEST(TwoQ, embedded_null_key) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);

    expect_load(cache, key, value);
    expect_load(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(TwoQ, integer_data) {
    TWO_Q::Cache<int> cache;

    loader_calls = 0;
    for (int access = 0; access < 3; ++access) {
        EXPECT_EQ(cache.fetch("zero", load_integer), 0);
        EXPECT_EQ(cache.fetch("negative", load_integer), -42);
    }
    EXPECT_EQ(loader_calls, 2);
}

TEST(TwoQ, non_default_data) {
    TWO_Q::Cache<Payload> cache;

    loader_calls = 0;
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(cache.fetch("A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(TwoQ, returned_value_is_a_copy) {
    StringCache cache;

    auto value = cache.fetch("A", original_load);
    value = "modified";
    expect_hit(cache, "A", "original");
}

TEST(TwoQ, independent_caches) {
    StringCache first;
    StringCache second;

    expect_load(first, "A", "first");
    expect_load(second, "A", "second");
    expect_hit(first, "A", "first");
    expect_hit(second, "A", "second");
}

TEST(TwoQ, empty_loader_is_only_needed_on_miss) {
    StringCache cache;

    expect_load(cache, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(cache.fetch("A", empty_loader), "value-A");
    EXPECT_THROW((void)cache.fetch("missing", empty_loader), std::bad_function_call);
    expect_load(cache, "missing", "loaded");
}

TEST(TwoQ, fetch_at_capacity) {
    StringCache cache;
    fill(cache);

    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, sequential_eviction) {
    StringCache cache;

    for (int key = 0; key < 40; ++key) {
        expect_load(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    for (std::size_t key = 0; key < 40 - StringCache::capacity; ++key) {
        expect_miss(cache, std::to_string(key));
    }
    for (std::size_t key = 40 - StringCache::capacity; key < 40; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, a1in_hit_preserves_fifo) {
    StringCache cache;
    fill(cache);

    for (int access = 0; access < 10; ++access) {
        expect_hit(cache, "0", "value-0");
    }
    expect_load(cache, "new", "new");
    expect_miss(cache, "0");
    expect_hit(cache, "1", "value-1");
}

TEST(TwoQ, ghost_lookup_is_miss) {
    StringCache cache;
    fill(cache);

    expect_load(cache, "new", "new");
    for (int access = 0; access < 10; ++access) {
        expect_miss(cache, "0");
    }
}

TEST(TwoQ, ghost_reload_promotes_page) {
    StringCache cache;
    fill(cache);

    expect_load(cache, "new", "new");
    expect_miss(cache, "0");
    expect_load(cache, "0", "reloaded");
    expect_hit(cache, "0", "reloaded");
    expect_miss(cache, "1");
    for (int key = 0; key < 40; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_hit(cache, "0", "reloaded");
}

TEST(TwoQ, forgotten_ghost_returns_to_a1in) {
    StringCache cache;
    fill(cache);

    for (std::size_t key = 0; key <= StringCache::kout; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_load(cache, "0", "reloaded");
    expect_hit(cache, "0", "reloaded");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, "0");
}

TEST(TwoQ, ghost_hit_preserves_fifo) {
    StringCache cache;
    fill(cache);

    expect_load(cache, "first", "first");
    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_miss(cache, "0");
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_load(cache, "0", "reloaded");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, "0");
}

TEST(TwoQ, newest_ghost_survives_history_limit) {
    StringCache cache;
    fill(cache);

    for (std::size_t key = 0; key <= StringCache::kout; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    const auto key = std::to_string(StringCache::kout);
    expect_miss(cache, key);
    expect_load(cache, key, "reloaded");
    for (std::size_t index = 0; index < StringCache::capacity; ++index) {
        expect_load(cache, "next-" + std::to_string(index), "next");
    }
    expect_hit(cache, key, "reloaded");
}

TEST(TwoQ, ghost_reload_at_kin_evicts_am) {
    StringCache cache;

    fill_frequent(cache);
    expect_miss(cache, "6");
    expect_load(cache, "6", "reloaded");
    expect_miss(cache, "0");
    expect_hit(cache, "6", "reloaded");
    expect_hit(cache, "7", "value-7");
    expect_hit(cache, "cold", "cold");
}

TEST(TwoQ, oldest_ghost_reload_at_full_history_preserves_other_ghosts) {
    StringCache cache;
    fill(cache);

    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }

    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_load(cache, std::to_string(key), "fresh-" + std::to_string(key));
    }
    for (std::size_t key = 0; key < 2 * StringCache::capacity; ++key) {
        expect_load(cache, "next-" + std::to_string(key), "next");
    }
    for (std::size_t key = 0; key < StringCache::kout; ++key) {
        expect_hit(cache, std::to_string(key), "fresh-" + std::to_string(key));
    }
}

TEST(TwoQ, failed_ghost_reload_preserves_residents_and_promotion) {
    StringCache cache;

    fill_frequent(cache);
    expect_miss(cache, "6");
    expect_miss(cache, "6");
    for (std::size_t key = 0; key < StringCache::capacity - StringCache::kin; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    expect_hit(cache, "7", "value-7");
    expect_hit(cache, "cold", "cold");

    expect_load(cache, "6", "recovered");
    expect_miss(cache, "0");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_hit(cache, "6", "recovered");
}

TEST(TwoQ, evicted_am_page_reloads_into_a1in) {
    StringCache cache;

    fill_frequent(cache);
    expect_load(cache, "6", "reloaded-6");
    expect_load(cache, "0", "reloaded-0");
    expect_hit(cache, "0", "reloaded-0");

    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_load(cache, "scan-" + std::to_string(key), "scan");
    }
    expect_miss(cache, "0");
    expect_hit(cache, "6", "reloaded-6");
}

TEST(TwoQ, loader_exception_preserves_residents) {
    StringCache cache;
    fill(cache);

    expect_miss(cache, "failed-load");
    expect_miss(cache, "failed-load");
    for (std::size_t key = 0; key < StringCache::capacity; ++key) {
        expect_hit(cache, std::to_string(key), "value-" + std::to_string(key));
    }
    expect_load(cache, "failed-load", "recovered");
    expect_hit(cache, "failed-load", "recovered");
}

TEST(TwoQ, am_hit_refreshes_recency) {
    StringCache cache;

    fill_frequent(cache);
    expect_hit(cache, "0", "value-0");
    expect_load(cache, "new", "new");
    expect_miss(cache, "1");
    expect_hit(cache, "0", "value-0");
}

}
}
