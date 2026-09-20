#pragma once

#include <cstddef>
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

    [[nodiscard]] Status insert(const std::string& url, Data data) {
        const auto found = map_.find(url);
        if (found != map_.end()) {
            const auto page = found->second;
            page->data_ = data;
            ++page->frequency_;
            promote(page);
            return Status::success;
        }

        const auto added = pages_.emplace(pages_.end(), url, data);
        map_.emplace(added->url_, added);

        promote(added);
        if (pages_.size() > capacity) {
            evict(std::prev(pages_.end()));
        }

        return Status::success;
    }

    [[nodiscard]] Status get(const std::string& url, const Data*& data) {
        data = nullptr;
        const auto found = map_.find(url);
        if (found == map_.end()) {
            return Status::not_found;
        }

        const auto page = found->second;
        ++page->frequency_;
        promote(page);
        data = std::addressof(page->data_);

        return Status::success;
    }

private:
    struct Page {
        Page(const std::string& url, Data data)
            : url_(url), data_(data), frequency_(1) {}

        std::string url_;
        Data data_;
        std::size_t frequency_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    PageList pages_;
    std::unordered_map<std::string, PageIterator> map_;

    void promote(PageIterator page) {
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
