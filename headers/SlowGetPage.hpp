#pragma once

#include <functional>
#include <string>

template <typename Data> struct SlowGetPage {
    std::function<Data(const std::string&)> load;

    Data fetch(const std::string& url) const { return load(url); }
    void remove(const std::string&) const {}
    void insert(const std::string&, const Data&) const {}
};

template <typename Cache, typename Data, typename Loader>
Data fetch_with_loader(Cache& cache, SlowGetPage<Data>& lower, const std::string& url, const Loader& loader) {
    lower.load = loader;
    return cache.fetch(url);
}
