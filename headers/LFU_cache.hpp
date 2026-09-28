#pragma once

#include <cstddef>
#include <iostream>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace LFU {

enum class Status {
    success,
    not_found
};

template <typename Data, typename Lower> class Cache {
public:
    static constexpr std::size_t capacity = 4;

    using Entry = std::pair<std::string, Data>;

    Cache(Lower& lower_cache) : lower_cache_(lower_cache) {}

    Data fetch(const std::string& url) {
        const Data* data = nullptr;
        if (get(url, data) == Status::success) {
            return *data;
        }

        Data loaded = lower_cache_.fetch(url);
        auto entry = insert(url, loaded);
        lower_cache_.remove(url);
        if (entry) {
            lower_cache_.insert(entry->first, entry->second);
        }

        return loaded;
    }

    std::optional<Entry> insert(const std::string& url, Data data) {
        const auto found = map_.find(url);
        if (found != map_.end()) {
            found->second->data_ = data;
            promote(found->second);
            return std::nullopt;
        }

        std::optional<Entry> entry;
        if (pages_.size() >= capacity) {
            entry = evict(std::prev(pages_.end()));
        }

        const auto added = pages_.emplace(pages_.end(), url, data);
        map_.emplace(added->url_, added);
        promote(added);
        return entry;
    }

    void remove(const std::string& url) {
        const auto found = map_.find(url);
        if (found == map_.end()) {
            std::cout << url << " not found\n";
            return;
        }
        evict(found->second);
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
    Lower& lower_cache_;

    Status get(const std::string& url, const Data*& data) {
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

    Entry evict(PageIterator page) {
        Entry entry{page->url_, page->data_};
        map_.erase(page->url_);
        pages_.erase(page);
        return entry;
    }
};

} // namespace LFU
