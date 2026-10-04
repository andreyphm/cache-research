#include "cache_runner.hpp"
#include "config.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

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
        EXPECT_EQ(count_hits(config, {1}, read_requests(small, 4)), 0u);
        EXPECT_EQ(count_hits(config, {2}, read_requests(large, 4)), 2u);
    }
}

TEST(Interface, counts_hits_in_lower_levels) {
    Config config{{CachePolicy::LFU, CachePolicy::LFU}};
    std::istringstream input("A B A C B");
    EXPECT_EQ(count_hits(config, {2, 2}, read_requests(input, 5)), 2u);
}

TEST(Interface, counts_hits_in_third_level) {
    Config config{{CachePolicy::LFU, CachePolicy::LFU, CachePolicy::LFU}};
    std::istringstream input("A B A C B D C");
    EXPECT_EQ(count_hits(config, {2, 2, 2}, read_requests(input, 7)), 3u);
}

TEST(Interface, reports_hits_for_each_level) {
    Config config{{CachePolicy::LFU, CachePolicy::LFU, CachePolicy::LFU}};
    const std::vector<std::size_t> capacities{1, 2, 3};
    const std::vector<std::string> requests{"A", "A", "B", "A", "C", "B"};

    const auto statistics = count_hits_by_level(config, capacities, requests);

    EXPECT_EQ(statistics.level_hits, (std::vector<std::size_t>{1, 1, 1}));
    EXPECT_EQ(statistics.storage_misses, 3u);
    EXPECT_EQ(statistics.total_hits(), 3u);
}

TEST(Interface, accepts_different_increasing_level_capacities) {
    Config config{{CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q}};
    const std::vector<std::size_t> capacities{4, 8, 16};
    const std::vector<std::string> requests{"A", "B", "A"};

    EXPECT_EQ(count_hits(config, capacities, requests), 1u);
}

TEST(Interface, rejects_capacity_count_different_from_level_count) {
    Config config{{CachePolicy::LFU, CachePolicy::ARC}};
    const std::vector<std::size_t> capacities{4};
    const std::vector<std::string> requests{"A"};

    EXPECT_THROW(count_hits(config, capacities, requests),
                 std::invalid_argument);
}

TEST(Interface, rejects_decreasing_capacities_for_inclusive_hierarchy) {
    Config config{{CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q}};
    const std::vector<std::size_t> capacities{8, 4, 16};
    const std::vector<std::string> requests{"A"};

    EXPECT_THROW(count_hits(config, capacities, requests),
                 std::invalid_argument);
}

TEST(Interface, lower_eviction_invalidates_upper_copy) {
    Config config{{CachePolicy::LFU, CachePolicy::LFU}};
    const std::vector<std::size_t> capacities{2, 3};
    const std::vector<std::string> requests{
        "A", "B", "C", "B", "B", "D", "E", "B"
    };

    EXPECT_EQ(count_hits(config, capacities, requests), 2u);
}

TEST(Interface, ghost_entries_are_misses) {
    Config config{{CachePolicy::ARC}};
    std::istringstream input("1 1 2 3 1");
    EXPECT_EQ(count_hits(config, {2}, read_requests(input, 5)), 1u);
}

TEST(Interface, empty_sequence_has_no_hits) {
    Config config{{CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}};
    std::istringstream input;
    EXPECT_EQ(count_hits(config, {2, 4, 8, 16}, read_requests(input, 0)), 0u);
}

TEST(Interface, repeated_key_hits_after_first_request) {
    for (auto policy : {CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}) {
        Config config{{policy}};
        std::istringstream input("key key key key key");
        EXPECT_EQ(count_hits(config, {1}, read_requests(input, 5)), 4u);
    }
}

TEST(Interface, processes_only_requested_number_of_keys) {
    Config config{{CachePolicy::LFU}};
    std::istringstream input("a a b b");
    EXPECT_EQ(count_hits(config, {1}, read_requests(input, 2)), 1u);
}

TEST(Interface, treats_numeric_looking_keys_as_strings) {
    Config config{{CachePolicy::LFU}};
    std::istringstream input("01 1 01 1");
    EXPECT_EQ(count_hits(config, {2}, read_requests(input, 4)), 2u);
}

TEST(Interface, supports_all_four_policies_in_any_order) {
    Config config{{CachePolicy::LFU, CachePolicy::ARC, CachePolicy::TWO_Q, CachePolicy::LIRS}};
    do {
        std::istringstream input("1 2 3 4 5 6 7 8 1 2 3 4 5 6 7 8");
        EXPECT_NO_THROW(count_hits(config, {2, 4, 8, 16},
                                   read_requests(input, 16)));
    } while (std::next_permutation(config.levels.begin(), config.levels.end()));
}
