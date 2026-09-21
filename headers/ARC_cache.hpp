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

template <typename Data> class Cache {
public:
    static constexpr std::size_t capacity = 4;

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    [[nodiscard]] Data fetch(const std::string& url, const std::function<Data(const std::string&)>& slow_get_page) {
        PageLocation* location = nullptr;
        switch (find(url, location)) {
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

    [[nodiscard]] std::size_t get_size_parameter() const {
        return target_t1_size_;
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

    std::size_t target_t1_size_ = capacity / 2;

    PageList list_t1_;
    PageList list_t2_;
    PageList list_b1_;
    PageList list_b2_;
    std::unordered_map<std::string, PageLocation> map_;

    void insert(const std::string& url, Data data) {
        make_space_for_insert();

        const auto added = list_t1_.emplace(list_t1_.begin(), url, data);
        map_.emplace(added->url_, PageLocation{&list_t1_, added});
    }

    [[nodiscard]] Status find(const std::string& url, PageLocation*& location) {
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

    void restore_from_ghost(PageLocation& location) {
        bool is_b2_hit = location.list_ == &list_b2_;
        if (is_b2_hit) {
            if (target_t1_size_ > 0) {
                --target_t1_size_;
            }
        } else if (target_t1_size_ < capacity) {
            ++target_t1_size_;
        }

        replace(*location.list_);
        move_to_t2(location);
    }

    void make_space_for_insert() {
        if (list_t1_.size() + list_b1_.size() == capacity) {
            if (list_t1_.size() == capacity) {
                evict_oldest(list_t1_);
                return;
            }

            evict_oldest(list_b1_);
            replace(list_b1_);
            return;
        }

        const auto total_size = map_.size();
        if (total_size < capacity) {
            return;
        }

        if (total_size == 2 * capacity) {
            evict_oldest(list_b2_);
        }

        replace(list_b1_);
    }

    void replace(const PageList& origin) {
        if (list_t1_.empty()) {
            evict_to_ghost(list_t2_, list_b2_);
            return;
        }

        const auto size = list_t1_.size();
        if (size > target_t1_size_ || (&origin == &list_b2_ && size == target_t1_size_)) {
            evict_to_ghost(list_t1_, list_b1_);
        } else {
            evict_to_ghost(list_t2_, list_b2_);
        }
    }

    void evict_to_ghost(PageList& source, PageList& ghost) {
        const auto page = std::prev(source.end());

        page->data_.reset();
        ghost.splice(ghost.begin(), source, page);
        map_.at(page->url_).list_ = &ghost;
    }

    void evict(PageList& list, PageIterator page) {
        map_.erase(page->url_);
        list.erase(page);
    }

    void evict_oldest(PageList& list) {
        evict(list, std::prev(list.end()));
    }
};

} // namespace ARC
