#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <optional>

namespace LIRS {

enum class Status {
    success,
    not_found
};

template <typename Data> class Cache {
public:
    static constexpr std::size_t capacity = 8;
    static constexpr std::size_t hir_capacity = 1;
    static constexpr std::size_t lir_capacity = capacity - hir_capacity;

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    [[nodiscard]] Data fetch(const std::string& url, const std::function<Data(const std::string&)>& slow_get_page) {
        PageInfo* info = nullptr;
        Status found = find(url, info);

        if (found == Status::success) {
            return *info->data_;
        }

        Data loaded = slow_get_page(url);
        insert(url, loaded);

        return loaded;
    }

private:
    struct Page {
        Page(const std::string& url): url_(url) {}

        std::string url_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    struct PageInfo {
        std::optional<Data> data_;
        std::optional<PageIterator> s_iterator_;
        std::optional<PageIterator> q_iterator_ = std::nullopt;
    };

    std::size_t lir_count = 0;

    PageList list_s_;
    PageList list_q_;
    std::unordered_map<std::string, PageInfo> map_;

    void insert(const std::string& url, Data data) {
        if (lir_count < lir_capacity) {
            const auto added = list_s_.emplace(list_s_.begin(), url);
            map_.emplace(added->url_, PageInfo{data, added});
            lir_count++;
        } else {
            make_space_in_queue();
            const auto added_q = list_q_.emplace(list_q_.begin(), url);
            const auto added_s = list_s_.emplace(list_s_.begin(), url);
            map_.emplace(added_q->url_, PageInfo{data, added_s, added_q});
        }
    }

    [[nodiscard]] Status find(const std::string& url, PageInfo*& info) {
        info = nullptr;
        const auto found = map_.find(url);
        if (found == map_.end() || !found->second.data_) {
            return Status::not_found;
        }

        info = std::addressof(found->second);
        if (!info->q_iterator_ && info->data_) {
            list_s_.splice(list_s_.begin(), list_s_, *info->s_iterator_);
            prune_stack();
        }

        if (info->q_iterator_) {
            if (info->s_iterator_) {
                list_s_.splice(list_s_.begin(), list_s_, *info->s_iterator_);
                info->q_iterator_.reset();

                last_lir_to_hir();
                prune_stack();
            } else {
                list_s_.push_front(url);
                info->s_iterator_ = list_s_.begin();
                list_q_.splice(list_q_.begin(), list_q_, *info->q_iterator_);
            }
        }  

        return Status::success;
    }

    void make_space_in_queue() {
        if (lir_count + list_q_.size() == capacity) {
            evict_hir(std::prev(list_q_.end()));
        }
    }

    void prune_stack() {
        while (!list_s_.empty()) {
            auto found = map_.find(list_s_.back().url_);
            auto& info = found->second;

            if (!info.q_iterator_ && info.data_) {
                return;
            }

            info.s_iterator_.reset();
            list_s_.pop_back();

            if (!info.q_iterator_) {
                map_.erase(found);
            }
        }
    }

    void last_lir_to_hir() {
        auto& new_hir_info = map_.find(list_s_.back().url_)->second;

        make_space_in_queue();
        list_q_.splice(list_q_.begin(), list_s_, std::prev(list_s_.end()));

        new_hir_info.s_iterator_.reset();
        new_hir_info.q_iterator_ = list_q_.begin();
    }

    void evict_hir(PageIterator page) {
        auto found = map_.find(page->url_);
        auto& info = found->second;

        info.data_.reset();
        info.q_iterator_.reset();

        if (!info.s_iterator_) {
            map_.erase(found);
        }

        list_q_.erase(page);
    }
};

} // namespace LIRS
