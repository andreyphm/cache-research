#pragma once

#include <cstddef>
#include <iostream>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Belady {

enum class Status {
    success,
    not_found
};

template <typename Data, typename Lower> class Cache {
public:
    static constexpr std::size_t capacity = 8;

    using Entry = std::pair<std::string, Data>;

    Cache(Lower& lower_cache, const std::vector<std::string>& requests)
        : lower_cache_(lower_cache) {
        index_requests(requests);
    }

    Data fetch(const std::string& url) {
        const std::size_t next_use = process_request(url);
        const Data* data = nullptr;
        if (get(url, next_use, data) == Status::success) {
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
        const auto next_use = next_request(url);
        const auto found = cache_.find(url);
        if (found != cache_.end()) {
            found->second->data_ = data;
            found->second->next_use_ = next_use;
            return std::nullopt;
        }

        std::optional<Entry> entry;
        if (pages_.size() >= capacity) {
            entry = evict();
        }

        const auto added = pages_.emplace(pages_.end(), url, data, next_use);
        cache_.emplace(added->url_, added);
        return entry;
    }

    void remove(const std::string& url) {
        const auto found = cache_.find(url);
        if (found == cache_.end()) {
            std::cout << url << " not found\n";
            return;
        }
        evict(found->second);
    }

private:
    struct Page {
        Page(const std::string& url, Data data, std::size_t next_use)
            : url_(url), data_(data), next_use_(next_use) {}

        std::string url_;
        Data data_;
        std::size_t next_use_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    const std::size_t never = std::numeric_limits<std::size_t>::max();

    PageList pages_;
    std::unordered_map<std::string, PageIterator> cache_;
    std::unordered_map<std::string, std::queue<std::size_t>> request_positions_;
    Lower& lower_cache_;

    void index_requests(const std::vector<std::string>& requests) {
        for (std::size_t pos = 0; pos < requests.size(); pos++) {
            request_positions_[requests[pos]].push(pos);
        }
    }

    std::size_t process_request(const std::string& url) {
        const auto found = request_positions_.find(url);
        auto& positions = found->second;
        positions.pop();

        return positions.empty() ? never : positions.front();
    }

    std::size_t next_request(const std::string& url) const {
        const auto found = request_positions_.find(url);
        return (found == request_positions_.end() || found->second.empty()) ? never : found->second.front();
    }

    Status get(const std::string& url, std::size_t next_use, const Data*& data) {
        data = nullptr;
        const auto found = cache_.find(url);
        if (found == cache_.end()) {
            return Status::not_found;
        }

        const auto page = found->second;
        page->next_use_ = next_use;
        data = std::addressof(page->data_);

        return Status::success;
    }

    Entry evict() {
        auto victim = pages_.begin();
        for (auto page = pages_.begin(); page != pages_.end(); page++) {
            if (page->next_use_ > victim->next_use_) {
                victim = page;
            }
        }

        return evict(victim);
    }

    Entry evict(PageIterator page) {
        Entry entry{page->url_, page->data_};
        cache_.erase(page->url_);
        pages_.erase(page);
        return entry;
    }
};

} // namespace Belady
