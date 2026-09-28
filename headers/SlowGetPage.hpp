#pragma once

#include <functional>
#include <string>
#include <utility>

template <typename Data> struct SlowGetPage {
    std::function<Data(const std::string&)> load;

    Data fetch(const std::string& url) { return load(url); }
    void remove(const std::string&) {}
    void insert(const std::string&, Data) {}
};

template <typename Cache, typename Data, typename Loader>
Data fetch_with_loader(Cache& cache, SlowGetPage<Data>& lower, const std::string& url, Loader& loader) {
    lower.load = loader;
    return cache.fetch(url);
}
