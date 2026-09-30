#include "cache_runner.hpp"
#include "config.hpp"
#include "2Q_cache.hpp"
#include "SlowGetPage.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <sstream>

TEST(Interface, reads_levels_in_order) {
    std::istringstream input("4 ARC LFU 2Q LIRS");
    Config config;
    ASSERT_TRUE(read_config(input, config));
    EXPECT_EQ(config.levels, (std::vector<CachePolicy>{CachePolicy::ARC, CachePolicy::LFU, CachePolicy::TWO_Q, CachePolicy::LIRS}));
}

TEST(Interface, reads_config_with_arbitrary_whitespace) {
    std::istringstream input("\n  4\tLFU\nARC  2Q\tLIRS\n");
    Config config;
    ASSERT_TRUE(read_config(input, config));
    EXPECT_EQ(config.levels, (std::vector<CachePolicy>{CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}));
}

TEST(Interface, successful_config_read_replaces_old_levels) {
    Config config{{CachePolicy::LIRS, CachePolicy::ARC}};
    std::istringstream input("1 LFU");
    ASSERT_TRUE(read_config(input, config));
    EXPECT_EQ(config.levels, (std::vector<CachePolicy>{CachePolicy::LFU}));
}

TEST(Interface, rejects_empty_config) {
    std::istringstream input;
    Config config;
    EXPECT_FALSE(read_config(input, config));
}

TEST(Interface, rejects_zero_levels) {
    std::istringstream input("0");
    Config config;
    EXPECT_FALSE(read_config(input, config));
}

TEST(Interface, rejects_missing_policy) {
    std::istringstream input("2 LFU");
    Config config;
    EXPECT_FALSE(read_config(input, config));
}

TEST(Interface, uses_requested_capacity) {
    for (auto policy : {CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}) {
        Config config{{policy}};
        std::istringstream small("1 2 1 2");
        std::istringstream large("1 2 1 2");
        EXPECT_EQ(count_hits(config, 1, 4, small), 0u);
        EXPECT_EQ(count_hits(config, 2, 4, large), 2u);
    }
}

TEST(Interface, counts_hits_in_lower_levels) {
    for (auto top : {CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}) {
        for (auto bottom : {CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}) {
            Config config{{top, bottom}};
            std::istringstream input("1 2 1 2 1 2");
            EXPECT_EQ(count_hits(config, 1, 6, input), 4u);
        }
    }
}

TEST(Interface, forwards_evictions_to_third_level) {
    for (auto policy : {CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}) {
        Config config{{policy, policy, policy}};
        std::istringstream input("1 2 3 1 2 3");
        EXPECT_EQ(count_hits(config, 1, 6, input), 3u);
    }
}

TEST(Interface, ghost_entries_are_misses) {
    Config config{{CachePolicy::ARC}};
    std::istringstream input("1 1 2 3 1");
    EXPECT_EQ(count_hits(config, 2, 5, input), 1u);
}

TEST(Interface, empty_sequence_has_no_hits) {
    Config config{{CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}};
    std::istringstream input;
    EXPECT_EQ(count_hits(config, 2, 0, input), 0u);
}

TEST(Interface, repeated_key_hits_after_first_request) {
    for (auto policy : {CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}) {
        Config config{{policy}};
        std::istringstream input("key key key key key");
        EXPECT_EQ(count_hits(config, 1, 5, input), 4u);
    }
}

TEST(Interface, processes_only_requested_number_of_keys) {
    Config config{{CachePolicy::LFU}};
    std::istringstream input("a a b b");
    EXPECT_EQ(count_hits(config, 1, 2, input), 1u);
}

TEST(Interface, treats_numeric_looking_keys_as_strings) {
    Config config{{CachePolicy::LFU}};
    std::istringstream input("01 1 01 1");
    EXPECT_EQ(count_hits(config, 2, 4, input), 2u);
}

TEST(Interface, supports_all_four_policies_in_any_order) {
    Config config{{CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}};
    do {
        std::istringstream input("1 2 3 4 5 6 7 8 1 2 3 4 5 6 7 8");
        EXPECT_EQ(count_hits(config, 2, 16, input), 8u);
    } while (std::next_permutation(config.levels.begin(), config.levels.end()));
}

TEST(Interface, two_q_accepts_demoted_data_for_a_ghost_key) {
    std::size_t misses = 0;
    SlowGetPage<std::string> source;
    source.load = [&](const std::string& key) { ++misses; return key; };
    TWO_Q::Cache<std::string, SlowGetPage<std::string>> cache(source, 1);
    cache.fetch("a");
    cache.fetch("b");
    auto evicted = cache.insert("a", "updated");
    ASSERT_TRUE(evicted);
    EXPECT_EQ(evicted->first, "b");
    EXPECT_EQ(cache.fetch("a"), "updated");
    EXPECT_EQ(misses, 2u);
    cache.insert("a", "again");
    EXPECT_EQ(cache.fetch("a"), "again");
    EXPECT_EQ(misses, 2u);
}
