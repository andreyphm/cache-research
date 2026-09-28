#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <optional>
#include <array>

namespace TWO_Q {

enum class Status {
    success,
    not_found,
    found_in_ghost
};

template <typename Data, typename Lower> class Cache {
public:
    static constexpr std::size_t capacity = 8;
    static constexpr std::size_t kin = (capacity + 3) / 4;
    static constexpr std::size_t kout = (capacity + 1) / 2;

    using Entry = std::pair<std::string, Data>;

    Cache(Lower& lower_cache) : lower_cache_(lower_cache) {};

    Data fetch(const std::string& url) {
        PageLocation* location = nullptr;
        std::optional<Data> loaded;
        std::optional<Entry> entry;

        switch(find(url, location)) {
            case Status::success:
                return *location->iterator_->data_;

            case Status::found_in_ghost: {
                loaded = lower_cache_.fetch(url);
                location->iterator_->data_ = loaded;
                entry = restore_from_ghost(*location);
                break;
            }

            case Status::not_found:
                loaded = lower_cache_.fetch(url);
                entry = insert(url, *loaded);
                break;
        }

        lower_cache_.remove(url);
        if (entry != std::nullopt) {
            lower_cache_.insert(entry->first, entry->second);
        }

        return *loaded;
    }

    std::optional<Entry> insert(const std::string& url, Data data) {
        std::optional<Entry> entry = make_space_for_insert();

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
                std::cout << url << " not found\n";
                return;
        }
    }

private:
    struct Page {
        Page(const std::string& url, Data data): url_(url), data_(data) {}

        std::string url_;
        std::optional<Data> data_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    struct PageLocation {
        PageList* list_;
        PageIterator iterator_;
    };

    PageList list_a1in_;
    PageList list_a1out_;
    PageList list_am_;
    std::unordered_map<std::string, PageLocation> map_;
    Lower& lower_cache_;

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
        if (list_a1in_.size() + list_am_.size() < capacity) {
            return std::nullopt;
        }

        if (list_a1in_.size() <= kin) {
            return evict_last(list_am_);
        }

        if (list_a1out_.size() == kout) {
            evict_last(list_a1out_);
        }
        return evict_to_ghost();
    }

    std::optional<Entry> restore_from_ghost(PageLocation& location) {
        std::optional<Entry> entry;
        if (list_a1in_.size() + list_am_.size() == capacity) {
            if (list_a1in_.size() > kin) {
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
        Entry entry = {page->url_, *page->data_};

        page->data_.reset();
        list_a1out_.splice(list_a1out_.begin(), list_a1in_, page);
        map_.at(page->url_).list_ = &list_a1out_;
        return entry;
    }
    
    std::optional<Entry> evict(PageLocation location) {
        PageIterator page = location.iterator_;
        std::optional<Entry> entry;
        if (page->data_) {
            entry.emplace(page->url_, *page->data_);
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
