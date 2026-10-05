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

template <typename Data, typename Lower> class Cache {
public:
    using Entry = std::pair<std::string, Data>;
    using EvictionCallback = std::function<void(const std::string&)>;

    Cache(Lower& lower_cache, const std::size_t capacity, const EvictionCallback& invalidate_upper = {})
        : capacity_(capacity), lir_capacity_(capacity * 99 / 100),
          lower_cache_(lower_cache), invalidate_upper_(invalidate_upper) {}

    Data fetch(const std::string& url) {
        PageInfo* info = nullptr;
        const Status found = find(url, info);

        if (found == Status::success) {
            return *info->data_;
        }

        const Data loaded = lower_cache_.fetch(url);
        insert(url, loaded);

        return loaded;
    }

    std::optional<Entry> insert(const std::string& url, const Data& data) {
        const auto found = map_.find(url);
        if (found != map_.end()) {
            auto& info = found->second;
            if (info.data_) {
                info.data_ = data;
                PageInfo* location = nullptr;
                find(url, location);
                return std::nullopt;
            }

            info.data_ = data;
            list_s_.splice(list_s_.begin(), list_s_, *info.s_iterator_);
            std::optional<Entry> entry;
            if (lir_count < lir_capacity_) {
                ++lir_count;
            } else {
                entry = last_lir_to_hir();
            }
            prune_stack();
            return entry;
        }

        if (lir_count < lir_capacity_) {
            const auto added = list_s_.emplace(list_s_.begin(), url);
            map_.emplace(added->url_, PageInfo{data, added});
            ++lir_count;
            return std::nullopt;
        }

        const auto entry = make_space_in_queue();
        const auto added_q = list_q_.emplace(list_q_.begin(), url);
        const auto added_s = list_s_.emplace(list_s_.begin(), url);
        map_.emplace(added_q->url_, PageInfo{data, added_s, added_q});
        if (lir_capacity_ == 0) {
            prune_stack();
        }
        return entry;
    }

    void remove(const std::string& url) {
        const auto found = map_.find(url);
        if (found == map_.end()) {
            return;
        }

        const auto& info = found->second;
        if (info.data_ && invalidate_upper_) {
            invalidate_upper_(url);
        }
        if (info.data_ && !info.q_iterator_) {
            --lir_count;
        }
        if (info.s_iterator_) {
            list_s_.erase(*info.s_iterator_);
        }
        if (info.q_iterator_) {
            list_q_.erase(*info.q_iterator_);
        }
        map_.erase(found);
        prune_stack();
    }

private:
    struct Page {
        Page(const std::string& url): url_(url) {}

        const std::string url_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    struct PageInfo {
        std::optional<Data> data_;
        std::optional<PageIterator> s_iterator_;
        std::optional<PageIterator> q_iterator_ = std::nullopt;
    };

    const std::size_t capacity_;
    const std::size_t lir_capacity_;
    std::size_t lir_count = 0;

    PageList list_s_;
    PageList list_q_;

    std::unordered_map<std::string, PageInfo> map_;
    Lower& lower_cache_;
    const EvictionCallback invalidate_upper_;

    Status find(const std::string& url, PageInfo*& info) {
        info = nullptr;
        const auto found = map_.find(url);
        if (found == map_.end() || !found->second.data_) {
            return Status::not_found;
        }

        info = std::addressof(found->second);
        if (lir_capacity_ == 0) {
            return Status::success;
        }
        if (!info->q_iterator_ && info->data_) {
            list_s_.splice(list_s_.begin(), list_s_, *info->s_iterator_);
            prune_stack();
        }

        if (info->q_iterator_) {
            if (info->s_iterator_) {
                list_s_.splice(list_s_.begin(), list_s_, *info->s_iterator_);
                list_q_.erase(*info->q_iterator_);
                info->q_iterator_.reset();

                if (lir_count < lir_capacity_) {
                    ++lir_count;
                } else {
                    last_lir_to_hir();
                }

                prune_stack();
                return Status::success;
            }

            list_s_.push_front(url);
            info->s_iterator_ = list_s_.begin();
            list_q_.splice(list_q_.begin(), list_q_, *info->q_iterator_);
        }  

        return Status::success;
    }

    std::optional<Entry> make_space_in_queue() {
        if (lir_count + list_q_.size() == capacity_) {
            return evict_hir(std::prev(list_q_.end()));
        }
        return std::nullopt;
    }

    void prune_stack() {
        while (!list_s_.empty()) {
            const auto found = map_.find(list_s_.back().url_);
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

    std::optional<Entry> last_lir_to_hir() {
        auto& new_hir_info = map_.find(list_s_.back().url_)->second;

        const auto entry = make_space_in_queue();
        list_q_.splice(list_q_.begin(), list_s_, std::prev(list_s_.end()));

        new_hir_info.s_iterator_.reset();
        new_hir_info.q_iterator_ = list_q_.begin();
        return entry;
    }

    Entry evict_hir(const PageIterator page) {
        const auto found = map_.find(page->url_);
        auto& info = found->second;
        const Entry entry{page->url_, *info.data_};

        if (invalidate_upper_) {
            invalidate_upper_(page->url_);
        }
        info.data_.reset();
        info.q_iterator_.reset();

        if (!info.s_iterator_) {
            map_.erase(found);
        }

        list_q_.erase(page);
        return entry;
    }
};

} // namespace LIRS
