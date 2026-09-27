#include "LIRS_cache.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>

namespace Tests {
namespace {

using StringCache = LIRS::Cache<std::string>;

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

void expect_load(StringCache& cache, const std::string& key,
                 const std::string& value) {
    loader_calls = 0;
    expected_key = key;
    loaded_value = value;
    EXPECT_EQ(cache.fetch(key, load_page), value);
    EXPECT_EQ(loader_calls, 1) << "expected load: " << key;
}

void expect_load(StringCache& cache, const std::string& key) {
    expect_load(cache, key, value_for(key));
}

void expect_hit(StringCache& cache, const std::string& key,
                const std::string& value) {
    loader_calls = 0;
    EXPECT_EQ(cache.fetch(key, unexpected_load), value);
    EXPECT_EQ(loader_calls, 0) << "expected hit: " << key;
}

void expect_hit(StringCache& cache, const std::string& key) {
    expect_hit(cache, key, value_for(key));
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

void fill_lir(StringCache& cache) {
    for (std::size_t i = 0; i < StringCache::lir_capacity; ++i) {
        expect_load(cache, lir_key(i));
    }
}

void touch_lir(StringCache& cache, std::size_t first = 0) {
    for (std::size_t i = first; i < StringCache::lir_capacity; ++i) {
        expect_hit(cache, lir_key(i));
    }
}

class LirsTransitions : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(StringCache::hir_capacity, 1u);
        ASSERT_GE(StringCache::lir_capacity, 2u);
        ASSERT_EQ(StringCache::capacity,
                  StringCache::lir_capacity + StringCache::hir_capacity);
        fill_lir(cache);
        expect_load(cache, "hir");
    }

    StringCache cache;
};

TEST(LIRS, miss_loads_and_caches_value) {
    StringCache cache;

    expect_load(cache, "page key", "page data");
    expect_hit(cache, "page key", "page data");
}

TEST(LIRS, hit_does_not_replace_value) {
    StringCache cache;

    expect_load(cache, "page", "original");
    loader_calls = 0;
    EXPECT_EQ(cache.fetch("page", unexpected_load), "original");
    EXPECT_EQ(loader_calls, 0) << "expected hit: page";
    expect_hit(cache, "page", "original");
}

TEST(LIRS, repeated_hits_do_not_consume_capacity) {
    StringCache cache;

    expect_load(cache, lir_key(0));
    for (int i = 0; i < 100; ++i) {
        expect_hit(cache, lir_key(0));
    }
    for (std::size_t i = 1; i < StringCache::lir_capacity; ++i) {
        expect_load(cache, lir_key(i));
    }
    touch_lir(cache);
}

TEST(LIRS, empty_key_and_empty_value_are_cached) {
    StringCache cache;

    expect_load(cache, "", "");
    expect_hit(cache, "", "");
}

TEST(LIRS, embedded_nulls_are_preserved) {
    StringCache cache;
    const std::string key("a\0b", 3);
    const std::string value("x\0y", 3);

    expect_load(cache, key, value);
    expect_load(cache, "a", "prefix");
    expect_hit(cache, key, value);
    expect_hit(cache, "a", "prefix");
}

TEST(LIRS, integer_zero_is_a_resident_value) {
    LIRS::Cache<int> cache;

    loader_calls = 0;
    EXPECT_EQ(cache.fetch("zero", load_integer), 0);
    EXPECT_EQ(cache.fetch("zero", load_integer), 0);
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
    LIRS::Cache<Payload> cache;

    loader_calls = 0;
    EXPECT_EQ(cache.fetch("page", load_payload).value_, 42);
    EXPECT_EQ(cache.fetch("page", load_payload).value_, 42);
    EXPECT_EQ(loader_calls, 1);
}

TEST(LIRS, returned_string_is_a_copy) {
    StringCache cache;

    auto value = cache.fetch("page", original_load);
    value.assign("modified");
    expect_hit(cache, "page", "original");
}

TEST(LIRS, caches_are_independent) {
    StringCache first;
    StringCache second;

    expect_load(first, "page", "first");
    expect_load(second, "page", "second");
    expect_hit(first, "page", "first");
    expect_hit(second, "page", "second");
}

TEST(LIRS, failed_load_can_be_retried) {
    StringCache cache;

    loader_calls = 0;
    expected_key = "page";
    EXPECT_THROW((void)cache.fetch("page", failing_load), std::runtime_error);
    EXPECT_EQ(loader_calls, 1);
    expect_load(cache, "page");
    expect_hit(cache, "page");
}

TEST(LIRS, failed_load_does_not_consume_lir_slot) {
    StringCache cache;

    expect_miss(cache, "missing");
    fill_lir(cache);
    for (int i = 0; i < 20; ++i) {
        expect_load(cache, "scan-" + std::to_string(i));
    }
    touch_lir(cache);
}

TEST(LIRS, hit_accepts_an_empty_loader) {
    StringCache cache;

    expect_load(cache, "page");
    const std::function<std::string(const std::string&)> empty;
    EXPECT_EQ(cache.fetch("page", empty), value_for("page"));
}

TEST_F(LirsTransitions, scan_preserves_lir_pages) {
    for (int i = 0; i < 100; ++i) {
        expect_load(cache, "scan-" + std::to_string(i));
    }
    expect_miss(cache, "hir");
    expect_miss(cache, "scan-98");
    touch_lir(cache);
    expect_hit(cache, "scan-99");
}

TEST_F(LirsTransitions, resident_hir_in_stack_promotes) {
    expect_hit(cache, "hir");
    expect_load(cache, "next");
    expect_miss(cache, lir_key(0));
    touch_lir(cache, 1);
    expect_hit(cache, "hir");
    expect_hit(cache, "next");
}

TEST_F(LirsTransitions, lir_hit_changes_the_next_demotion_candidate) {
    expect_hit(cache, lir_key(0));
    expect_hit(cache, "hir");
    expect_load(cache, "next");
    expect_miss(cache, lir_key(1));
    expect_hit(cache, lir_key(0));
    touch_lir(cache, 2);
    expect_hit(cache, "hir");
}

TEST_F(LirsTransitions, demotion_keeps_data_until_eviction) {
    expect_hit(cache, "hir");
    expect_hit(cache, lir_key(0));
    expect_hit(cache, "hir");
    touch_lir(cache, 1);
}

TEST_F(LirsTransitions, pruning_keeps_resident_hir_data) {
    touch_lir(cache);
    expect_hit(cache, "hir");
}

TEST_F(LirsTransitions, hir_outside_stack_is_not_promoted_on_first_hit) {
    touch_lir(cache);
    expect_hit(cache, "hir");
    expect_load(cache, "next");
    expect_miss(cache, "hir");
    touch_lir(cache);
}

TEST_F(LirsTransitions, hir_outside_stack_promotes_on_second_hit) {
    touch_lir(cache);
    expect_hit(cache, "hir");
    expect_hit(cache, "hir");
    expect_load(cache, "next");
    expect_miss(cache, lir_key(0));
    expect_hit(cache, "hir");
    touch_lir(cache, 1);
}

TEST_F(LirsTransitions, demoted_lir_needs_two_hits_to_regain_protection) {
    expect_hit(cache, "hir");
    expect_hit(cache, lir_key(0));
    expect_hit(cache, lir_key(0));
    expect_load(cache, "next");
    expect_miss(cache, lir_key(1));
    expect_hit(cache, lir_key(0));
    expect_hit(cache, "hir");
    touch_lir(cache, 2);
}

TEST_F(LirsTransitions, repeated_promotions_choose_successive_oldest_lir) {
    expect_hit(cache, "hir");
    expect_load(cache, "next");
    expect_hit(cache, "next");
    expect_load(cache, "last");
    expect_miss(cache, lir_key(0));
    expect_miss(cache, lir_key(1));
    touch_lir(cache, 2);
    expect_hit(cache, "hir");
    expect_hit(cache, "next");
    expect_hit(cache, "last");
}

TEST_F(LirsTransitions, failed_load_in_full_cache_does_not_evict_or_reorder) {
    expect_miss(cache, "missing");
    expect_miss(cache, "missing");
    expect_hit(cache, "hir");
    expect_load(cache, "next");
    expect_miss(cache, lir_key(0));
    touch_lir(cache, 1);
    expect_hit(cache, "hir");
}

TEST_F(LirsTransitions, ghost_reload_promotes_and_uses_fresh_data) {
    expect_load(cache, "next");
    expect_load(cache, "hir", "reloaded");
    expect_miss(cache, "next");
    expect_load(cache, "last");
    expect_miss(cache, lir_key(0));
    expect_hit(cache, "hir", "reloaded");
    touch_lir(cache, 1);
}

TEST_F(LirsTransitions, failed_ghost_load_preserves_history_and_residents) {
    expect_load(cache, "next");
    expect_miss(cache, "hir");
    expect_miss(cache, "hir");
    expect_hit(cache, "next");
    expect_load(cache, "hir", "reloaded");
    expect_miss(cache, lir_key(0));
    expect_load(cache, "last");
    expect_miss(cache, lir_key(1));
    expect_hit(cache, "hir", "reloaded");
    expect_hit(cache, "next");
    touch_lir(cache, 2);
}

TEST_F(LirsTransitions, pruned_ghost_reloads_as_hir) {
    expect_load(cache, "next");
    touch_lir(cache);
    expect_load(cache, "hir", "reloaded");
    expect_load(cache, "last");
    expect_miss(cache, "hir");
    touch_lir(cache);
}

TEST_F(LirsTransitions, eviction_outside_stack_does_not_leave_promoting_history) {
    touch_lir(cache);
    expect_load(cache, "next");
    expect_load(cache, "hir");
    expect_load(cache, "last");
    expect_miss(cache, "hir");
    touch_lir(cache);
}

}
}
