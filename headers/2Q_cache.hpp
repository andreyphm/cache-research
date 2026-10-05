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

namespace TWO_Q {

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
        : capacity_(capacity), kin_((capacity + 3) / 4),
          kout_((capacity + 1) / 2), lower_cache_(lower_cache),
          invalidate_upper_(invalidate_upper) {}

    Data fetch(const std::string& url) {
        PageLocation* location = nullptr;
        std::optional<Data> loaded;

        switch(find(url, location)) {
            case Status::success:
                return *location->iterator_->data_;

            case Status::found_in_ghost: {
                loaded = lower_cache_.fetch(url);
                location->iterator_->data_ = loaded;
                restore_from_ghost(*location);
                break;
            }

            case Status::not_found:
                loaded = lower_cache_.fetch(url);
                insert(url, *loaded);
                break;
        }

        return *loaded;
    }

    std::optional<Entry> insert(const std::string& url, const Data& data) {
        const auto found = map_.find(url);
        if (found != map_.end()) {
            auto& location = found->second;
            location.iterator_->data_ = data;
            if (location.list_ == &list_a1out_) {
                return restore_from_ghost(location);
            }
            if (location.list_ == &list_am_) {
                list_am_.splice(list_am_.begin(), list_am_, location.iterator_);
            }
            return std::nullopt;
        }
        const std::optional<Entry> entry = make_space_for_insert();

        const auto added = list_a1in_.emplace(list_a1in_.begin(), url, data);
        map_.emplace(added->url_, PageLocation{&list_a1in_, added});
        return entry;
    }

    void remove(const std::string& url) {
        PageLocation* location = nullptr;
        switch(find(url, location)) {
            case Status::success:
            case Status::found_in_ghost:
                evict(*location);
                return;

            case Status::not_found:
                return;
        }
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
    const std::size_t kin_;
    const std::size_t kout_;

    PageList list_a1in_;
    PageList list_a1out_;
    PageList list_am_;
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
        if (location->list_ == &list_a1out_) {
            return Status::found_in_ghost;
        }

        if (location->list_ == &list_am_) {
            list_am_.splice(list_am_.begin(), list_am_, location->iterator_);
        }

        return Status::success;
    }

    std::optional<Entry> make_space_for_insert() {
        if (list_a1in_.size() + list_am_.size() < capacity_) {
            return std::nullopt;
        }

        if (list_a1in_.size() <= kin_ && !list_am_.empty()) {
            return evict_last(list_am_);
        }

        if (list_a1out_.size() == kout_) {
            evict_last(list_a1out_);
        }
        return evict_to_ghost();
    }

    std::optional<Entry> restore_from_ghost(PageLocation& location) {
        std::optional<Entry> entry;
        if (list_a1in_.size() + list_am_.size() == capacity_) {
            if (list_a1in_.size() > kin_ || list_am_.empty()) {
                entry = evict_to_ghost();
            } else {
                entry = evict_last(list_am_);
            }
        }

        list_am_.splice(list_am_.begin(), list_a1out_, location.iterator_);
        location.list_ = &list_am_;
        return entry;
    }

    Entry evict_to_ghost() {
        const auto page = std::prev(list_a1in_.end());
        const Entry entry = {page->url_, *page->data_};

        if (invalidate_upper_) {
            invalidate_upper_(page->url_);
        }
        page->data_.reset();
        list_a1out_.splice(list_a1out_.begin(), list_a1in_, page);
        map_.at(page->url_).list_ = &list_a1out_;
        return entry;
    }
    
    std::optional<Entry> evict(const PageLocation& location) {
        const PageIterator page = location.iterator_;
        std::optional<Entry> entry;
        if (page->data_) {
            entry.emplace(page->url_, *page->data_);
            if (invalidate_upper_) {
                invalidate_upper_(page->url_);
            }
        }
        map_.erase(page->url_);
        location.list_->erase(page);
        return entry;
    }

    std::optional<Entry> evict_last(PageList& list) {
        return evict(PageLocation{&list, std::prev(list.end())});
    }
};

} // namespace TWO_Q
