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
    not_found
};

template <typename Data> class Cache {
public:
    static constexpr std::size_t capacity = 8;
    static constexpr std::size_t kin = (capacity + 3) / 4;
    static constexpr std::size_t kout = (capacity + 1) / 2;

    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    [[nodiscard]] Status insert(const std::string& url, Data data) {
        const auto found = map_.find(url);
        if (found == map_.end()) {
            make_space_for_insert();

            auto& list_a1in = get_list(ListId::A1in_);
            const auto added = list_a1in.emplace(list_a1in.begin(), url, data);
            map_.emplace(added->url_, PageLocation{ListId::A1in_, added});

            return Status::success;
        }

        auto& location = found->second;
        const auto page = location.iterator_;
        auto& list_am = get_list(ListId::Am_);
        switch (location.list_id_) {
            case ListId::A1in_:
                break;

            case ListId::Am_:
                list_am.splice(list_am.begin(), list_am, page);
                break;

            case ListId::A1out_:
                promote_from_ghost(location);
                break;

            default:
                break;
        }

        page->data_ = data;

        return Status::success;
    }

    [[nodiscard]] Status get(const std::string& url, const Data*& data) {
        data = nullptr;

        const auto found = map_.find(url);
        if (found == map_.end()) {
            return Status::not_found;
        }

        const auto page = found->second.iterator_;

        switch (found->second.list_id_) {
            case ListId::A1out_:
                return Status::not_found;

            case ListId::Am_: {
                auto& list_am = get_list(ListId::Am_);
                list_am.splice(list_am.begin(), list_am, page);
                break;
            }

            case ListId::A1in_:
            default:
                break;
        }

        data = std::addressof(*page->data_);
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

private:
    struct Page {
        Page(const std::string& url, Data data): url_(url), data_(data) {}

        std::string url_;
        std::optional<Data> data_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    enum class ListId {
        A1in_,
        A1out_,
        Am_
    };

    struct PageLocation {
        ListId list_id_;
        PageIterator iterator_;
    };

    static constexpr std::size_t number_of_lists_ = 3;

    std::array<PageList, number_of_lists_> lists_;
    std::unordered_map<std::string, PageLocation> map_;

    PageList& get_list(ListId id) {
        return lists_[static_cast<std::size_t>(id)];
    }

    void make_space_for_insert() {
        auto& a1in = get_list(ListId::A1in_);
        auto& am = get_list(ListId::Am_);

        if (a1in.size() + am.size() < capacity) {
            return;
        }

        if (a1in.size() <= kin) {
            evict(am, std::prev(am.end()));
            return;
        }

        auto& a1out = get_list(ListId::A1out_);
        if (a1out.size() == kout) {
            evict(a1out, std::prev(a1out.end()));
        }
        evict_to_ghost();
    }

    void promote_from_ghost(PageLocation& location) {
        auto& list_a1in = get_list(ListId::A1in_);
        auto& list_am = get_list(ListId::Am_);
        auto& list_a1out = get_list(ListId::A1out_);

        if (list_a1in.size() + list_am.size() == capacity && list_a1in.size() > kin) {
            evict_to_ghost();
        } else {
            evict(list_am, std::prev(list_am.end()));
        }

        list_am.splice(list_am.begin(), list_a1out, location.iterator_);
        location.list_id_ = ListId::Am_;
    }

    void evict_to_ghost() {
        auto& list_a1in = get_list(ListId::A1in_);
        auto& list_a1out = get_list(ListId::A1out_);
        const auto page = std::prev(list_a1in.end());

        page->data_.reset();
        list_a1out.splice(list_a1out.begin(), list_a1in, page);
        map_.at(page->url_).list_id_ = ListId::A1out_;
    }
    
    void evict(PageList& list, PageIterator page) {
        map_.erase(page->url_);
        list.erase(page);
    }
};

} // namespace TWO_Q
