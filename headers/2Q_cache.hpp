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
#include <array>

namespace TWO_Q {

enum class Status {
    success,
    not_found,
    found_in_ghost
};

template <typename Data> class Cache {
public:
    static constexpr std::size_t capacity = 8;
    static constexpr std::size_t kin = (capacity + 3) / 4;
    static constexpr std::size_t kout = (capacity + 1) / 2;

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    [[nodiscard]] Data fetch(const std::string& url, const std::function<Data(const std::string&)>& slow_get_page) {
        PageLocation* location = nullptr;
        switch(find(url, location)) {
            case Status::success:
                return *location->iterator_->data_;

            case Status::found_in_ghost: {
                Data loaded = slow_get_page(url);
                location->iterator_->data_ = loaded;
                restore_from_ghost(*location);
                return loaded;
            }

            case Status::not_found:
                break;
        }

        Data loaded = slow_get_page(url);
        insert(url, loaded);

        return loaded;
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

    void insert(const std::string& url, Data data) {
        make_space_for_insert();

        const auto added = list_a1in_.emplace(list_a1in_.begin(), url, data);
        map_.emplace(added->url_, PageLocation{&list_a1in_, added});
    }

    [[nodiscard]] Status find(const std::string& url, PageLocation*& location) {
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

    void make_space_for_insert() {
        if (list_a1in_.size() + list_am_.size() < capacity) {
            return;
        }

        if (list_a1in_.size() <= kin) {
            evict_last(list_am_);
            return;
        }

        if (list_a1out_.size() == kout) {
            evict_last(list_a1out_);
        }
        evict_to_ghost();
    }

    void restore_from_ghost(PageLocation& location) {
        if (list_a1in_.size() + list_am_.size() == capacity && list_a1in_.size() > kin) {
            evict_to_ghost();
        } else {
            evict_last(list_am_);
        }

        list_am_.splice(list_am_.begin(), list_a1out_, location.iterator_);
        location.list_ = &list_am_;
    }

    void evict_to_ghost() {
        const auto page = std::prev(list_a1in_.end());

        page->data_.reset();
        list_a1out_.splice(list_a1out_.begin(), list_a1in_, page);
        map_.at(page->url_).list_ = &list_a1out_;
    }
    
    void evict(PageList& list, PageIterator page) {
        map_.erase(page->url_);
        list.erase(page);
    }

    void evict_last(PageList& list) {
        evict(list, std::prev(list.end()));
    }
};

} // namespace TWO_Q
