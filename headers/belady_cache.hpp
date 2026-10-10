#pragma once

#include <cstddef>
#include <limits>
#include <list>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Belady {

enum class Status {
    success,
    not_found
};

template <typename Data> class Cache {
public:
    Cache(const std::size_t capacity, const std::vector<std::string>& requests)
        : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("Cache capacity must be positive");
        }
        index_requests(requests);
    }

    template <typename Loader> Data fetch(const std::string& url, const Loader& loader) {
        const std::size_t next_use = process_request(url);
        const Data* data = nullptr;
        if (get(url, next_use, data) == Status::success) {
            return *data;
        }

        const Data loaded = loader(url);
        insert(url, loaded);

        return loaded;
    }

private:
    struct Page {
        Page(const std::string& url, const Data& data, const std::size_t next_use)
            : url_(url), data_(data), next_use_(next_use) {}

        const std::string url_;
        Data data_;
        std::size_t next_use_;
    };

    using PageList = std::list<Page>;
    using PageIterator = PageList::iterator;

    static constexpr std::size_t never_ = std::numeric_limits<std::size_t>::max();

    const std::size_t capacity_;
    std::size_t current_position_ = 0;
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
        if (found == request_positions_.end() || found->second.empty()
            || found->second.front() != current_position_) {
            throw std::invalid_argument("Request does not match indexed sequence");
        }

        auto& positions = found->second;
        positions.pop();
        ++current_position_;

        return positions.empty() ? never_ : positions.front();
    }

    std::size_t next_request(const std::string& url) const {
        const auto found = request_positions_.find(url);
        return (found == request_positions_.end() || found->second.empty())
                ? never_
                : found->second.front();
    }

    Status get(const std::string& url, const std::size_t next_use, const Data*& data) {
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

    void insert(const std::string& url, const Data& data) {
        if (pages_.size() == capacity_) {
            evict();
        }

        const auto added = pages_.emplace(pages_.end(), url, data, next_request(url));
        cache_.emplace(added->url_, added);
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

} // namespace Belady
