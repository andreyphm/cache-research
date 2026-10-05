#include "2Q_cache.hpp"
#include "SlowGetPage.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <string>

namespace Tests {
namespace {

constexpr std::size_t test_capacity = 8;
constexpr std::size_t test_kin = (test_capacity + 3) / 4;
constexpr std::size_t test_kout = (test_capacity + 1) / 2;

using StringCache = TWO_Q::Cache<std::string, SlowGetPage<std::string>>;

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

void fill(StringCache& cache, SlowGetPage<std::string>& lower) {
    for (std::size_t key = 0; key < test_capacity; ++key) {
        expect_load(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
}

void fill_frequent(StringCache& cache, SlowGetPage<std::string>& lower) {
    fill(cache, lower);

    expect_load(cache, lower, "cold", "cold");
    for (std::size_t key = 0; key < test_capacity - test_kin; ++key) {
        expect_miss(cache, lower, std::to_string(key));
        expect_load(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, miss_loads_and_caches_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "page key", "page data");
    expect_hit(cache, lower, "page key", "page data");
}

TEST(TwoQ, hit_does_not_replace_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "original");
    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: A";
    expect_hit(cache, lower, "A", "original");
}

TEST(TwoQ, repeated_hit) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "value-A");
    for (int access = 0; access < 100; ++access) {
        expect_hit(cache, lower, "A", "value-A");
    }
}

TEST(TwoQ, empty_key_and_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "", "");
    expect_hit(cache, lower, "", "");
}

TEST(TwoQ, embedded_null_key) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);

    expect_load(cache, lower, key, value);
    expect_load(cache, lower, "a", "prefix");
    expect_hit(cache, lower, key, value);
    expect_hit(cache, lower, "a", "prefix");
}

TEST(TwoQ, integer_data) {
    SlowGetPage<int> lower;
    TWO_Q::Cache<int, SlowGetPage<int>> cache{lower, test_capacity};

    loader_calls = 0;
    for (int access = 0; access < 3; ++access) {
        EXPECT_EQ(fetch_with_loader(cache, lower, "zero", load_integer), 0);
        EXPECT_EQ(fetch_with_loader(cache, lower, "negative", load_integer), -42);
    }
    EXPECT_EQ(loader_calls, 2);
}

struct Payload {
    Payload(const int value)
        : value_(value) {}
    int value_;
};

Payload load_payload(const std::string&) {
    ++loader_calls;
    return Payload{42};
}

TEST(TwoQ, non_default_data) {
    SlowGetPage<Payload> lower;
    TWO_Q::Cache<Payload, SlowGetPage<Payload>> cache{lower, test_capacity};

    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(TwoQ, returned_value_is_a_copy) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    auto value = fetch_with_loader(cache, lower, "A", original_load);
    value = "modified";
    expect_hit(cache, lower, "A", "original");
}

TEST(TwoQ, independent_caches) {
    SlowGetPage<std::string> lower_first;
    StringCache first{lower_first, test_capacity};
    SlowGetPage<std::string> lower_second;
    StringCache second{lower_second, test_capacity};

    expect_load(first, lower_first, "A", "first");
    expect_load(second, lower_second, "A", "second");
    expect_hit(first, lower_first, "A", "first");
    expect_hit(second, lower_second, "A", "second");
}

TEST(TwoQ, empty_loader_is_only_needed_on_miss) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "A", "value-A");
    const std::function<std::string(const std::string&)> empty_loader;
    EXPECT_EQ(fetch_with_loader(cache, lower, "A", empty_loader), "value-A");
    EXPECT_THROW(fetch_with_loader(cache, lower, "missing", empty_loader), std::bad_function_call);
    expect_load(cache, lower, "missing", "loaded");
}

TEST(TwoQ, fetch_at_capacity) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (std::size_t key = 0; key < test_capacity; ++key) {
        expect_hit(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, sequential_eviction) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    for (int key = 0; key < 40; ++key) {
        expect_load(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
    for (std::size_t key = 0; key < 40 - test_capacity; ++key) {
        expect_miss(cache, lower, std::to_string(key));
    }
    for (std::size_t key = 40 - test_capacity; key < 40; ++key) {
        expect_hit(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
}

TEST(TwoQ, a1in_hit_preserves_fifo) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (int access = 0; access < 10; ++access) {
        expect_hit(cache, lower, "0", "value-0");
    }
    expect_load(cache, lower, "new", "new");
    expect_miss(cache, lower, "0");
    expect_hit(cache, lower, "1", "value-1");
}

TEST(TwoQ, ghost_lookup_is_miss) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_load(cache, lower, "new", "new");
    for (int access = 0; access < 10; ++access) {
        expect_miss(cache, lower, "0");
    }
}

TEST(TwoQ, ghost_reload_promotes_page) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_load(cache, lower, "new", "new");
    expect_miss(cache, lower, "0");
    expect_load(cache, lower, "0", "reloaded");
    expect_hit(cache, lower, "0", "reloaded");
    expect_miss(cache, lower, "1");
    for (int key = 0; key < 40; ++key) {
        expect_load(cache, lower, "scan-" + std::to_string(key), "scan");
    }
    expect_hit(cache, lower, "0", "reloaded");
}

TEST(TwoQ, forgotten_ghost_returns_to_a1in) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (std::size_t key = 0; key <= test_kout; ++key) {
        expect_load(cache, lower, "scan-" + std::to_string(key), "scan");
    }
    expect_load(cache, lower, "0", "reloaded");
    expect_hit(cache, lower, "0", "reloaded");
    for (std::size_t key = 0; key < test_capacity; ++key) {
        expect_load(cache, lower, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, lower, "0");
}

TEST(TwoQ, ghost_hit_preserves_fifo) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_load(cache, lower, "first", "first");
    for (std::size_t key = 0; key < test_kout; ++key) {
        expect_miss(cache, lower, "0");
        expect_load(cache, lower, "scan-" + std::to_string(key), "scan");
    }
    expect_load(cache, lower, "0", "reloaded");
    for (std::size_t key = 0; key < test_capacity; ++key) {
        expect_load(cache, lower, "next-" + std::to_string(key), "next");
    }
    expect_miss(cache, lower, "0");
}

TEST(TwoQ, newest_ghost_survives_history_limit) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (std::size_t key = 0; key <= test_kout; ++key) {
        expect_load(cache, lower, "scan-" + std::to_string(key), "scan");
    }
    const auto key = std::to_string(test_kout);
    expect_miss(cache, lower, key);
    expect_load(cache, lower, key, "reloaded");
    for (std::size_t index = 0; index < test_capacity; ++index) {
        expect_load(cache, lower, "next-" + std::to_string(index), "next");
    }
    expect_hit(cache, lower, key, "reloaded");
}

TEST(TwoQ, ghost_reload_at_kin_evicts_am) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_frequent(cache, lower);
    expect_miss(cache, lower, "6");
    expect_load(cache, lower, "6", "reloaded");
    expect_miss(cache, lower, "0");
    expect_hit(cache, lower, "6", "reloaded");
    expect_hit(cache, lower, "7", "value-7");
    expect_hit(cache, lower, "cold", "cold");
}

TEST(TwoQ, oldest_ghost_reload_at_full_history_preserves_other_ghosts) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    for (std::size_t key = 0; key < test_kout; ++key) {
        expect_load(cache, lower, "scan-" + std::to_string(key), "scan");
    }

    for (std::size_t key = 0; key < test_kout; ++key) {
        expect_load(cache, lower, std::to_string(key), "fresh-" + std::to_string(key));
    }
    for (std::size_t key = 0; key < 2 * test_capacity; ++key) {
        expect_load(cache, lower, "next-" + std::to_string(key), "next");
    }
    for (std::size_t key = 0; key < test_kout; ++key) {
        expect_hit(cache, lower, std::to_string(key), "fresh-" + std::to_string(key));
    }
}

TEST(TwoQ, failed_ghost_reload_preserves_residents_and_promotion) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_frequent(cache, lower);
    expect_miss(cache, lower, "6");
    expect_miss(cache, lower, "6");
    for (std::size_t key = 0; key < test_capacity - test_kin; ++key) {
        expect_hit(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
    expect_hit(cache, lower, "7", "value-7");
    expect_hit(cache, lower, "cold", "cold");

    expect_load(cache, lower, "6", "recovered");
    expect_miss(cache, lower, "0");
    for (std::size_t key = 0; key < test_capacity; ++key) {
        expect_load(cache, lower, "scan-" + std::to_string(key), "scan");
    }
    expect_hit(cache, lower, "6", "recovered");
}

TEST(TwoQ, evicted_am_page_reloads_into_a1in) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_frequent(cache, lower);
    expect_load(cache, lower, "6", "reloaded-6");
    expect_load(cache, lower, "0", "reloaded-0");
    expect_hit(cache, lower, "0", "reloaded-0");

    for (std::size_t key = 0; key < test_capacity; ++key) {
        expect_load(cache, lower, "scan-" + std::to_string(key), "scan");
    }
    expect_miss(cache, lower, "0");
    expect_hit(cache, lower, "6", "reloaded-6");
}

TEST(TwoQ, loader_exception_preserves_residents) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    fill(cache, lower);

    expect_miss(cache, lower, "failed-load");
    expect_miss(cache, lower, "failed-load");
    for (std::size_t key = 0; key < test_capacity; ++key) {
        expect_hit(cache, lower, std::to_string(key), "value-" + std::to_string(key));
    }
    expect_load(cache, lower, "failed-load", "recovered");
    expect_hit(cache, lower, "failed-load", "recovered");
}

TEST(TwoQ, am_hit_refreshes_recency) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_frequent(cache, lower);
    expect_hit(cache, lower, "0", "value-0");
    expect_load(cache, lower, "new", "new");
    expect_miss(cache, lower, "1");
    expect_hit(cache, lower, "0", "value-0");
}

}
}
