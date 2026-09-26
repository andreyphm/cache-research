#pragma once

#include <cstddef>
#include <functional>
#include <limits>
#include <list>
#include <memory>
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

template <typename Data> class Cache {
public:
    static constexpr std::size_t capacity = 8;

    Cache(const std::vector<std::string>& requests) {
        index_requests(requests);
    }
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    [[nodiscard]] Data fetch(const std::string& url, const std::function<Data(const std::string&)>& slow_get_page) {
        const std::size_t next_use = process_request(url);
        const Data* data = nullptr;
        if (get(url, next_use, data) == Status::success) {
            return *data;
        }

        Data loaded = slow_get_page(url);
        insert(url, loaded, next_use);

        return loaded;
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

    PageIterator insert(const std::string& url, Data data, std::size_t next_use) {
        if (pages_.size() >= capacity) {
            evict();
        }

        const auto added = pages_.emplace(pages_.end(), url, data, next_use);
        cache_.emplace(added->url_, added);

        return added;
    }

    [[nodiscard]] Status get(const std::string& url, std::size_t next_use, const Data*& data) {
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

    void evict() {
        auto victim = pages_.begin();
        for (auto page = pages_.begin(); page != pages_.end(); page++) {
            if (page->next_use_ > victim->next_use_) {
                victim = page;
            }
        }

        cache_.erase(victim->url_);
        pages_.erase(victim);
    }
};

}
