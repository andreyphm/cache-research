#include "LIRS_cache.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tests {
namespace {

using StringCache = LIRS::Cache<std::string>;

std::string lir_key(std::size_t index) {
    return "lir-" + std::to_string(index);
}

std::string value_for(const std::string& key) {
    return "value-" + key;
}

void expect_load(StringCache& cache, const std::string& key,
                 const std::string& value) {
    int calls = 0;
    EXPECT_EQ(cache.fetch(key, [&](const std::string& url) {
        ++calls;
        EXPECT_EQ(url, key);
        return value;
    }), value);
    EXPECT_EQ(calls, 1) << "expected load: " << key;
}

void expect_load(StringCache& cache, const std::string& key) {
    expect_load(cache, key, value_for(key));
}

void expect_hit(StringCache& cache, const std::string& key,
                const std::string& value) {
    int calls = 0;
    EXPECT_EQ(cache.fetch(key, [&](const std::string&) {
        ++calls;
        return std::string("unexpected load");
    }), value);
    EXPECT_EQ(calls, 0) << "expected hit: " << key;
}

void expect_hit(StringCache& cache, const std::string& key) {
    expect_hit(cache, key, value_for(key));
}

struct LoadStopped {};

// Observe a miss without inserting a value or evicting another page.
void expect_miss(StringCache& cache, const std::string& key) {
    int calls = 0;
    EXPECT_THROW((void)cache.fetch(key, [&](const std::string& url) -> std::string {
        ++calls;
        EXPECT_EQ(url, key);
        throw LoadStopped{};
    }), LoadStopped);
    EXPECT_EQ(calls, 1) << "expected miss: " << key;
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

// Transition scenarios use the current configuration: one resident HIR.
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
    expect_load(cache, "page", "loaded");
    expect_hit(cache, "page", "loaded");
}

TEST(LIRS, hit_does_not_replace_value) {
    StringCache cache;
    expect_load(cache, "page", "original");
    EXPECT_EQ(cache.fetch("page", [](const std::string&) {
        ADD_FAILURE() << "loader called on a hit";
        return std::string("replacement");
    }), "original");
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
    int calls = 0;
    const auto loader = [&](const std::string&) { ++calls; return 0; };
    EXPECT_EQ(cache.fetch("zero", loader), 0);
    EXPECT_EQ(cache.fetch("zero", loader), 0);
    EXPECT_EQ(calls, 1);
}

struct Payload {
    explicit Payload(int value) : value(value) {}
    int value;
};

TEST(LIRS, data_need_not_be_default_constructible) {
    LIRS::Cache<Payload> cache;
    int calls = 0;
    const auto loader = [&](const std::string&) { ++calls; return Payload(42); };
    EXPECT_EQ(cache.fetch("page", loader).value, 42);
    EXPECT_EQ(cache.fetch("page", loader).value, 42);
    EXPECT_EQ(calls, 1);
}

TEST(LIRS, null_shared_pointer_is_a_resident_value) {
    LIRS::Cache<std::shared_ptr<int>> cache;
    int calls = 0;
    const auto loader = [&](const std::string&) {
        ++calls;
        return std::shared_ptr<int>{};
    };
    EXPECT_EQ(cache.fetch("null", loader), nullptr);
    EXPECT_EQ(cache.fetch("null", loader), nullptr);
    EXPECT_EQ(calls, 1);
}

TEST(LIRS, returned_string_is_a_copy) {
    StringCache cache;
    auto value = cache.fetch("page", [](const std::string&) {
        return std::string("original");
    });
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
    int calls = 0;
    EXPECT_THROW((void)cache.fetch("page", [&](const std::string&) -> std::string {
        ++calls;
        throw std::runtime_error("load failed");
    }), std::runtime_error);
    EXPECT_EQ(calls, 1);
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
    // HIR was pruned from S but remains resident in Q.
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
    // Check protection before hitting the reloaded page again.
    expect_load(cache, "last");
    expect_miss(cache, lir_key(0));
    expect_hit(cache, "hir", "reloaded");
    touch_lir(cache, 1);
}

TEST_F(LirsTransitions, failed_ghost_load_preserves_history_and_residents) {
    expect_load(cache, "next");
    expect_miss(cache, "hir");
    expect_miss(cache, "hir");
    // This hit proves a failed reload did not evict the resident HIR.
    // It also promotes 'next', so lir-0 becomes the new resident HIR.
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
    touch_lir(cache); // Prunes both the ghost and the resident HIR from S.
    expect_load(cache, "hir", "reloaded");
    expect_load(cache, "last");
    expect_miss(cache, "hir");
    touch_lir(cache);
}

TEST_F(LirsTransitions, eviction_outside_stack_does_not_leave_promoting_history) {
    touch_lir(cache); // Resident 'hir' is now only in Q.
    expect_load(cache, "next");
    expect_load(cache, "hir");
    expect_load(cache, "last");
    expect_miss(cache, "hir");
    touch_lir(cache);
}

TEST(LIRS, eviction_releases_hir_payload) {
    using Cache = LIRS::Cache<std::shared_ptr<int>>;
    Cache cache;
    const auto loader = [](const std::string&) { return std::make_shared<int>(42); };
    for (std::size_t i = 0; i < Cache::lir_capacity; ++i) {
        (void)cache.fetch(lir_key(i), loader);
    }
    std::weak_ptr<int> observer = cache.fetch("hir", loader);
    ASSERT_FALSE(observer.expired());
    for (std::size_t i = 0; i < Cache::hir_capacity; ++i) {
        (void)cache.fetch("next-" + std::to_string(i), loader);
    }
    EXPECT_TRUE(observer.expired());
}

TEST(LIRS, destruction_releases_all_cached_payloads) {
    using Cache = LIRS::Cache<std::shared_ptr<int>>;
    std::vector<std::weak_ptr<int>> observers;
    {
        Cache cache;
        for (std::size_t i = 0; i < Cache::capacity; ++i) {
            observers.push_back(cache.fetch(std::to_string(i), [](const std::string&) {
                return std::make_shared<int>(42);
            }));
        }
        for (const auto& observer : observers) {
            EXPECT_FALSE(observer.expired());
        }
    }
    for (const auto& observer : observers) {
        EXPECT_TRUE(observer.expired());
    }
}

TEST(LIRS, mixed_workload_returns_correct_data_and_respects_capacity) {
    using Cache = LIRS::Cache<std::shared_ptr<int>>;
    Cache cache;
    std::vector<std::weak_ptr<int>> observers;
    unsigned state = 12345;
    for (int step = 0; step < 2000; ++step) {
        SCOPED_TRACE(step);
        state = state * 1664525U + 1013904223U;
        const auto number = static_cast<int>((state >> 16) % (Cache::capacity * 3));
        const auto key = std::to_string(number);
        {
            int calls = 0;
            const auto value = cache.fetch(key, [&](const std::string& url) {
                ++calls;
                EXPECT_EQ(url, key);
                auto loaded = std::make_shared<int>(number);
                observers.push_back(loaded);
                return loaded;
            });
            ASSERT_NE(value, nullptr);
            EXPECT_EQ(*value, number);
            EXPECT_LE(calls, 1);
        }
        std::size_t alive = 0;
        for (const auto& observer : observers) {
            alive += !observer.expired();
        }
        ASSERT_LE(alive, Cache::capacity);
    }
}

} // namespace
} // namespace Tests
