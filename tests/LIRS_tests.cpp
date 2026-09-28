#include "LIRS_cache.hpp"
#include "SlowGetPage.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>

namespace Tests {
namespace {

constexpr std::size_t test_capacity = 8;
constexpr std::size_t test_lir_capacity = test_capacity - 1;

using StringCache = LIRS::Cache<std::string, SlowGetPage<std::string>>;

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

std::string lir_key(std::size_t index) {
    return "lir-" + std::to_string(index);
}

std::string value_for(const std::string& key) {
    return "value-" + key;
}

void expect_load(StringCache& cache, SlowGetPage<std::string>& lower, const std::string& key,
                 const std::string& value) {
    loader_calls = 0;
    expected_key = key;
    loaded_value = value;
    EXPECT_EQ(fetch_with_loader(cache, lower, key, load_page), value);
    EXPECT_EQ(loader_calls, 1) << "expected load: " << key;
}

void expect_load(StringCache& cache, SlowGetPage<std::string>& lower, const std::string& key) {
    expect_load(cache, lower, key, value_for(key));
}

void expect_hit(StringCache& cache, SlowGetPage<std::string>& lower, const std::string& key,
                const std::string& value) {
    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, key, unexpected_load), value);
    EXPECT_EQ(loader_calls, 0) << "expected hit: " << key;
}

void expect_hit(StringCache& cache, SlowGetPage<std::string>& lower, const std::string& key) {
    expect_hit(cache, lower, key, value_for(key));
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

void fill_lir(StringCache& cache, SlowGetPage<std::string>& lower) {
    for (std::size_t i = 0; i < test_lir_capacity; ++i) {
        expect_load(cache, lower, lir_key(i));
    }
}

void touch_lir(StringCache& cache, SlowGetPage<std::string>& lower, std::size_t first = 0) {
    for (std::size_t i = first; i < test_lir_capacity; ++i) {
        expect_hit(cache, lower, lir_key(i));
    }
}

TEST(LIRS, miss_loads_and_caches_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "page key", "page data");
    expect_hit(cache, lower, "page key", "page data");
}

TEST(LIRS, hit_does_not_replace_value) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "page", "original");
    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "page", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: page";
    expect_hit(cache, lower, "page", "original");
}

TEST(LIRS, repeated_hits_do_not_consume_capacity) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, lir_key(0));
    for (int i = 0; i < 100; ++i) {
        expect_hit(cache, lower, lir_key(0));
    }
    for (std::size_t i = 1; i < test_lir_capacity; ++i) {
        expect_load(cache, lower, lir_key(i));
    }
    touch_lir(cache, lower);
}

TEST(LIRS, empty_key_and_empty_value_are_cached) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "", "");
    expect_hit(cache, lower, "", "");
}

TEST(LIRS, embedded_nulls_are_preserved) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);

    expect_load(cache, lower, key, value);
    expect_load(cache, lower, "a", "prefix");
    expect_hit(cache, lower, key, value);
    expect_hit(cache, lower, "a", "prefix");
}

TEST(LIRS, integer_zero_is_a_resident_value) {
    SlowGetPage<int> lower;
    LIRS::Cache<int, SlowGetPage<int>> cache{lower, test_capacity};

    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "zero", load_integer), 0);
    EXPECT_EQ(fetch_with_loader(cache, lower, "zero", load_integer), 0);
    EXPECT_EQ(loader_calls, 1);
}

struct Payload {
    Payload(int value) : value_(value) {}
    int value_;
};

Payload load_payload(const std::string&) {
    ++loader_calls;
    return Payload{42};
}

TEST(LIRS, data_need_not_be_default_constructible) {
    SlowGetPage<Payload> lower;
    LIRS::Cache<Payload, SlowGetPage<Payload>> cache{lower, test_capacity};

    loader_calls = 0;
    EXPECT_EQ(fetch_with_loader(cache, lower, "page", load_payload).value_, 42);
    EXPECT_EQ(fetch_with_loader(cache, lower, "page", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(LIRS, returned_string_is_a_copy) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    auto value = fetch_with_loader(cache, lower, "page", original_load);
    value.assign("modified");
    expect_hit(cache, lower, "page", "original");
}

TEST(LIRS, caches_are_independent) {
    SlowGetPage<std::string> lower_first;
    StringCache first{lower_first, test_capacity};
    SlowGetPage<std::string> lower_second;
    StringCache second{lower_second, test_capacity};

    expect_load(first, lower_first, "page", "first");
    expect_load(second, lower_second, "page", "second");
    expect_hit(first, lower_first, "page", "first");
    expect_hit(second, lower_second, "page", "second");
}

TEST(LIRS, failed_load_can_be_retried) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    loader_calls = 0;
    expected_key = "page";
    EXPECT_THROW(fetch_with_loader(cache, lower, "page", failing_load), std::runtime_error);
    EXPECT_EQ(loader_calls, 1);
    expect_load(cache, lower, "page");
    expect_hit(cache, lower, "page");
}

TEST(LIRS, failed_load_does_not_consume_lir_slot) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_miss(cache, lower, "missing");
    fill_lir(cache, lower);
    for (int i = 0; i < 20; ++i) {
        expect_load(cache, lower, "scan-" + std::to_string(i));
    }
    touch_lir(cache, lower);
}

TEST(LIRS, hit_accepts_an_empty_loader) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    expect_load(cache, lower, "page");
    const std::function<std::string(const std::string&)> empty;
    EXPECT_EQ(fetch_with_loader(cache, lower, "page", empty), value_for("page"));
}

TEST(LIRS, scan_preserves_lir_pages) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    for (int i = 0; i < 100; ++i) {
        expect_load(cache, lower, "scan-" + std::to_string(i));
    }
    expect_miss(cache, lower, "hir");
    expect_miss(cache, lower, "scan-98");
    touch_lir(cache, lower);
    expect_hit(cache, lower, "scan-99");
}

TEST(LIRS, resident_hir_in_stack_promotes) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    expect_hit(cache, lower, "hir");
    expect_load(cache, lower, "next");
    expect_miss(cache, lower, lir_key(0));
    touch_lir(cache, lower, 1);
    expect_hit(cache, lower, "hir");
    expect_hit(cache, lower, "next");
}

TEST(LIRS, lir_hit_changes_the_next_demotion_candidate) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    expect_hit(cache, lower, lir_key(0));
    expect_hit(cache, lower, "hir");
    expect_load(cache, lower, "next");
    expect_miss(cache, lower, lir_key(1));
    expect_hit(cache, lower, lir_key(0));
    touch_lir(cache, lower, 2);
    expect_hit(cache, lower, "hir");
}

TEST(LIRS, demotion_keeps_data_until_eviction) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    expect_hit(cache, lower, "hir");
    expect_hit(cache, lower, lir_key(0));
    expect_hit(cache, lower, "hir");
    touch_lir(cache, lower, 1);
}

TEST(LIRS, pruning_keeps_resident_hir_data) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    touch_lir(cache, lower);
    expect_hit(cache, lower, "hir");
}

TEST(LIRS, hir_outside_stack_is_not_promoted_on_first_hit) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    touch_lir(cache, lower);
    expect_hit(cache, lower, "hir");
    expect_load(cache, lower, "next");
    expect_miss(cache, lower, "hir");
    touch_lir(cache, lower);
}

TEST(LIRS, hir_outside_stack_promotes_on_second_hit) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    touch_lir(cache, lower);
    expect_hit(cache, lower, "hir");
    expect_hit(cache, lower, "hir");
    expect_load(cache, lower, "next");
    expect_miss(cache, lower, lir_key(0));
    expect_hit(cache, lower, "hir");
    touch_lir(cache, lower, 1);
}

TEST(LIRS, demoted_lir_needs_two_hits_to_regain_protection) {
    SlowGetPage<std::string> lower;
    StringCache cache{lower, test_capacity};

    fill_lir(cache, lower);
    expect_load(cache, lower, "hir");
    expect_hit(cache, lower, "hir");
    expect_hit(cache, lower, lir_key(0));
    expect_hit(cache, lower, lir_key(0));
    expect_load(cache, lower, "next");
    expect_miss(cache, lower, lir_key(1));
    expect_hit(cache, lower, lir_key(0));
    expect_hit(cache, lower, "hir");
    touch_lir(cache, lower, 2);
}

}
}
