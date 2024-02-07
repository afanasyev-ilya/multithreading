#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <execution>
#include <thread>
#include <omp.h>
#include <tbb/tbb.h>
#include <tbb/parallel_for.h>
#include "bucket_sort.h"
#include "quick_sort.h"

constexpr bool print_arrays = false;

template <typename T>
std::ostream & operator << (std::ostream &os, const std::vector<T> &vec) {
    os << "[ ";
    for(int i = 0; i < std::min((size_t)10, vec.size()); i++) {
        os << vec[i] << " ";
    }
    os << "]";
    return os;
}

void fill_with_rands(std::vector<int> &data) {
    std::random_device dev;
    std::mt19937 generator(dev());
    std::uniform_int_distribution<int> distr(0, data.size() - 1);

    for(auto &val: data) {
        val = distr(generator);
    }
}

void fill_with_rands(std::vector<float> &data) {
    std::random_device dev;
    std::mt19937 generator(dev());
    std::uniform_real_distribution<float> distr(0, 1);

    for(auto &val: data) {
        val = distr(generator);
    }
}

template <typename T, typename SortFunc, typename ...Args>
void time(const std::string &name, std::vector<T> &data, SortFunc sort_func, Args ... args) {
    fill_with_rands(data);

    if constexpr (print_arrays)
        std::cout << "before : " << data << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    sort_func(data, args...);
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << name << " time: " << ms << " ms" << std::endl;

    if constexpr (print_arrays)
        std::cout << "after : " << data << std::endl;
}

int main() {
    std::vector<float> data(1e6);
    int size = 1e7;
    int iters = 3;

    for(int i = 0; i < iters; i++)
        time("first sequential quick sort, 1 thread", data, quick_sort<float>, 1);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("first parallel quick sort, 4 threads", data, quick_sort<float>, 4);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("bucket sort, sqrt(N)*2 buckets", data, bucket_sort, sqrt(data.size())*2);
    std::cout << std::endl;

    std::array<int, 3> omp_threads = {2, 4, 8};
    for(auto threads: omp_threads) {
        std::string name = "omp bucket sort, sqrt(N)*2 buckets," + std::to_string(threads) + " threads";
        for(int i = 0; i < iters; i++)
            time(name, data, omp_bucket_sort, sqrt(data.size())*2, threads);
        std::cout << std::endl;
    }

    std::string name = "tbb bucket sort, sqrt(N)*2 buckets ";
    for(int i = 0; i < iters; i++)
        time(name, data, tbb_bucket_sort, sqrt(data.size())*2);
    std::cout << std::endl;

    return 0;
}
