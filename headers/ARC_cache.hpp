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

namespace ARC {

enum class Status {
    success,
    not_found,
    found_in_ghost
};

template <typename Data, typename Lower> class Cache {
public:
    using Entry = std::pair<std::string, Data>;
    using EvictionCallback = std::function<void(const std::string&)>;

    Cache(Lower& lower_cache, const std::size_t capacity, const EvictionCallback& invalidate_upper = {})
        : capacity_(capacity), lower_cache_(lower_cache),
          invalidate_upper_(invalidate_upper) {}

    Data fetch(const std::string& url) {
        PageLocation* location = nullptr;
        if (find(url, location) == Status::success) {
            return *location->iterator_->data_;
        }

        const Data loaded = lower_cache_.fetch(url);
        insert(url, loaded);

        return loaded;
    }

    std::optional<Entry> insert(const std::string& url, const Data& data) {
        const auto found = map_.find(url);
        if (found != map_.end()) {
            auto& location = found->second;
            location.iterator_->data_ = data;
            if (is_ghost(*location.list_)) {
                return restore_from_ghost(location);
            }
            move_to_t2(location);
            return std::nullopt;
        }

        const auto entry = make_space_for_insert();
        const auto added = list_t1_.emplace(list_t1_.begin(), url, data);
        map_.emplace(added->url_, PageLocation{&list_t1_, added});
        return entry;
    }

    void remove(const std::string& url) {
        const auto found = map_.find(url);
        if (found == map_.end()) {
            return;
        }
        const auto location = found->second;
        evict(*location.list_, location.iterator_);
    }

    std::size_t get_size_parameter() const {
        return target_t1_size_;
    }

private:
    struct Page {
        Page(const std::string& url, const Data& data): url_(url), data_(data) {}

        const std::string url_;
        std::optional<Data> data_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    struct PageLocation {
        PageList* list_;
        const PageIterator iterator_;
    };

    const std::size_t capacity_;
    std::size_t target_t1_size_ = capacity_ / 2;

    PageList list_t1_;
    PageList list_t2_;
    PageList list_b1_;
    PageList list_b2_;

    std::unordered_map<std::string, PageLocation> map_;
    Lower& lower_cache_;
    const EvictionCallback invalidate_upper_;

    Status find(const std::string& url, PageLocation*& location) {
        location = nullptr;
        const auto found = map_.find(url);
        if (found == map_.end()) {
            return Status::not_found;
        }

        location = std::addressof(found->second);
        if (is_ghost(*location->list_)) {
            return Status::found_in_ghost;
        }

        move_to_t2(*location);

        return Status::success;
    }

    bool is_ghost(const PageList& list) const {
        return &list == &list_b1_ || &list == &list_b2_;
    }

    void move_to_t2(PageLocation& location) {
        list_t2_.splice(list_t2_.begin(), *location.list_, location.iterator_);
        location.list_ = &list_t2_;
    }

    std::optional<Entry> restore_from_ghost(PageLocation& location) {
        const bool is_b2_hit = location.list_ == &list_b2_;
        if (is_b2_hit) {
            if (target_t1_size_ > 0) {
                --target_t1_size_;
            }
        } else if (target_t1_size_ < capacity_) {
            ++target_t1_size_;
        }

        const auto entry = replace(*location.list_);
        move_to_t2(location);
        return entry;
    }

    std::optional<Entry> make_space_for_insert() {
        if (list_t1_.size() + list_b1_.size() == capacity_) {
            if (list_t1_.size() == capacity_) {
                return evict_oldest(list_t1_);
            }

            evict_oldest(list_b1_);
            return replace(list_b1_);
        }

        const auto total_size = map_.size();
        if (total_size < capacity_) {
            return std::nullopt;
        }

        if (total_size == 2 * capacity_) {
            evict_oldest(list_b2_);
        }

        return replace(list_b1_);
    }

    std::optional<Entry> replace(const PageList& origin) {
        if (list_t1_.size() + list_t2_.size() < capacity_) {
            return std::nullopt;
        }
        if (list_t1_.empty()) {
            return evict_to_ghost(list_t2_, list_b2_);
        }

        const auto size = list_t1_.size();
        if (list_t2_.empty() || size > target_t1_size_ || (&origin == &list_b2_ && size == target_t1_size_)) {
            return evict_to_ghost(list_t1_, list_b1_);
        } else {
            return evict_to_ghost(list_t2_, list_b2_);
        }
    }

    Entry evict_to_ghost(PageList& source, PageList& ghost) {
        const auto page = std::prev(source.end());
        const Entry entry{page->url_, *page->data_};

        if (invalidate_upper_) {
            invalidate_upper_(page->url_);
        }
        page->data_.reset();
        ghost.splice(ghost.begin(), source, page);
        map_.at(page->url_).list_ = &ghost;
        return entry;
    }

    std::optional<Entry> evict(PageList& list, const PageIterator page) {
        std::optional<Entry> entry;
        if (page->data_) {
            entry.emplace(page->url_, *page->data_);
            if (invalidate_upper_) {
                invalidate_upper_(page->url_);
            }
        }
        map_.erase(page->url_);
        list.erase(page);
        return entry;
    }

    std::optional<Entry> evict_oldest(PageList& list) {
        return evict(list, std::prev(list.end()));
    }
};

} // namespace ARC
