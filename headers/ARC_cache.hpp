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

namespace ARC {

enum class Status {
    success,
    not_found
};

template <typename Data> class Cache {
public:
    static constexpr std::size_t capacity = 4;

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    [[nodiscard]] Status insert(const std::string& url, Data data) {
        const auto found = map_.find(url);
        if (found == map_.end()) {
            make_space_for_insert();

            auto& list_t1 = get_list(ListId::T1_);
            const auto added = list_t1.emplace(list_t1.begin(), url, data);
            map_.emplace(added->url_, PageLocation{ListId::T1_, added});

            return Status::success;
        }

        auto& location = found->second;
        if (is_ghost(location.list_id_)) {
            restore_from_ghost(location);
        }

        location.iterator_->data_ = data;
        move_to_t2(location);

        return Status::success;
    }

    [[nodiscard]] Status get(const std::string& url, const Data*& data) {
        data = nullptr;

        const auto found = map_.find(url);
        if (found == map_.end()) {
            return Status::not_found;
        }

        auto& location = found->second;
        if (is_ghost(location.list_id_)) {
            return Status::not_found;
        }

        move_to_t2(location);

        data = std::addressof(*location.iterator_->data_);

        return Status::success;
    }

    [[nodiscard]] Data get(const std::string& url, const std::function<Data(const std::string&)>& slow_get_page) {
        const Data* data = nullptr;
        if (get(url, data) == Status::success) {
            return *data;
        }

        Data loaded = slow_get_page(url);
        (void)insert(url, loaded);

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

    enum class ListId {
        T1_,
        T2_,
        B1_,
        B2_
    };

    struct PageLocation {
        ListId list_id_;
        PageIterator iterator_;
    };

    std::size_t target_t1_size_ = capacity / 2;
    static constexpr std::size_t number_of_lists_ = 4;

    std::array<PageList, number_of_lists_> lists_;
    std::unordered_map<std::string, PageLocation> map_;

    PageList& get_list(ListId id) {
        return lists_[static_cast<std::size_t>(id)];
    }

    bool is_ghost(ListId id) {
        return id == ListId::B1_ || id == ListId::B2_;
    }

    void move_to_t2(PageLocation& location) {
        auto& t2 = get_list(ListId::T2_);
        auto& source = get_list(location.list_id_);

        t2.splice(t2.begin(), source, location.iterator_);
        location.list_id_ = ListId::T2_;
    }

    void restore_from_ghost(PageLocation& location) {
        const bool is_b2_hit = location.list_id_ == ListId::B2_;
        if (is_b2_hit) {
            if (target_t1_size_ > 0) {
                --target_t1_size_;
            }
        } else if (target_t1_size_ < capacity) {
            ++target_t1_size_;
        }

        evict_to_ghost(is_b2_hit);
    }

    void make_space_for_insert() {
        auto& t1 = get_list(ListId::T1_);
        auto& b1 = get_list(ListId::B1_);

        if (t1.size() + b1.size() == capacity) {
            if (t1.size() == capacity) {
                evict_oldest(t1);
                return;
            }

            evict_oldest(b1);
            evict_to_ghost(false);
            return;
        }

        const auto total_size = map_.size();
        if (total_size < capacity) {
            return;
        }

        if (total_size == 2 * capacity) {
            evict_oldest(get_list(ListId::B2_));
        }

        evict_to_ghost(false);
    }

    void evict_to_ghost(bool is_b2_hit) {
        auto& list_t1 = get_list(ListId::T1_);

        const bool evict_from_t1 = !list_t1.empty() && (list_t1.size() > target_t1_size_
                                   || (is_b2_hit && list_t1.size() == target_t1_size_));

        const auto source_id = evict_from_t1 ? ListId::T1_ : ListId::T2_;
        const auto ghost_id = evict_from_t1 ? ListId::B1_ : ListId::B2_;

        auto& source = get_list(source_id);
        auto& ghost = get_list(ghost_id);
        const auto page = std::prev(source.end());

        page->data_.reset();
        ghost.splice(ghost.begin(), source, page);
        map_.at(page->url_).list_id_ = ghost_id;
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
