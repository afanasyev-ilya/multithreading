#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <execution>
#include <thread>

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

template <typename T>
int partition(std::vector<T> &arr, int start, int end)
{
    T pivot = arr[start];

    int count = 0;
    for (int i = start + 1; i <= end; i++) {
        if (arr[i] <= pivot)
            count++;
    }

    // Giving pivot element its correct position
    int pivotIndex = start + count;
    std::swap(arr[pivotIndex], arr[start]);

    // Sorting left and right parts of the pivot element
    int i = start, j = end;

    while (i < pivotIndex && j > pivotIndex) {

        while (arr[i] <= pivot) {
            i++;
        }

        while (arr[j] > pivot) {
            j--;
        }

        if (i < pivotIndex && j > pivotIndex) {
            std::swap(arr[i++], arr[j--]);
        }
    }

    return pivotIndex;
}

template <typename T>
void quick_sort_process(std::vector<T> &arr, int start, int end, int depth, int max_threads)
{
    bool parallel = (max_threads != 1);
    // base case
    if (start >= end)
        return;

    // partitioning the array
    int p = partition(arr, start, end);

    // Sorting the left part
    if(parallel && depth < 4) {
        std::thread t1(quick_sort_process<T>, std::ref(arr), start, p-1, depth + 1, max_threads);
        std::thread t2(quick_sort_process<T>, std::ref(arr), p+1, end, depth + 1, max_threads);
        t1.join();
        t2.join();
    } else {
        quick_sort_process<T>(arr, start, p - 1, depth + 1, max_threads);
        quick_sort_process<T>(arr, p + 1, end, depth + 1, max_threads);
    }
}

template <typename T>
void quick_sort(std::vector<T> &arr, int max_threads)
{
    quick_sort_process(arr, 0, arr.size(), 1, max_threads);
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

void bucket_sort(std::vector<float> &arr, int num_buckets) {
    std::vector<std::vector<float>> buckets(num_buckets);
    for(auto val: arr) {
        int bucket_idx = val * num_buckets;
        buckets[bucket_idx].push_back(val);
    }

    for(auto &bucket: buckets) {
        std::sort(bucket.begin(), bucket.end());
    }

    size_t write_idx = 0;
    for(const auto &bucket: buckets) {
        for(auto val: bucket) {
            arr[write_idx] = val;
            write_idx++;
        }
    }
}

int main() {
    std::vector<float> data(1e6);
    int size = 1e7;
    int iters = 4;

    for(int i = 0; i < iters; i++)
        time("first sequential quick sort, 1 thread", data, quick_sort<float>, 1);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("first parallel quick sort, 4 threads", data, quick_sort<float>, 4);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("bucket sort, sqrt(N)*2 buckets", data, bucket_sort, sqrt(data.size())*2);
    std::cout << std::endl;

    return 0;
}
