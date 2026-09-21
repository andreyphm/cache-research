#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace LFU {

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

    [[nodiscard]] Data fetch(const std::string& url, const std::function<Data(const std::string&)>& slow_get_page) {
        const Data* data = nullptr;
        if (get(url, data) == Status::success) {
            return *data;
        }

        Data loaded = slow_get_page(url);
        promote(insert(url, loaded));

        return loaded;
    }

private:
    struct Page {
        Page(const std::string& url, Data data)
            : url_(url), data_(data), frequency_(0) {}

        std::string url_;
        Data data_;
        std::size_t frequency_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    PageList pages_;
    std::unordered_map<std::string, PageIterator> map_;

    PageIterator insert(const std::string& url, Data data) {
        if (pages_.size() >= capacity) {
            evict(std::prev(pages_.end()));
        }

        const auto added = pages_.emplace(pages_.end(), url, data);
        map_.emplace(added->url_, added);

        return added;
    }

    [[nodiscard]] Status get(const std::string& url, const Data*& data) {
        const auto found = map_.find(url);
        if (found == map_.end()) {
            return Status::not_found;
        }

        const auto page = found->second;
        promote(page);
        data = std::addressof(page->data_);

        return Status::success;
    }

    void promote(PageIterator page) {
        ++page->frequency_;
        auto destination = page;
        while (destination != pages_.begin()
               && std::prev(destination)->frequency_ <= page->frequency_) {
            --destination;
        }
        if (destination != page) {
            pages_.splice(destination, pages_, page);
        }
    }

    void evict(PageIterator page) {
        map_.erase(page->url_);
        pages_.erase(page);
    }
};

} // namespace LFU
