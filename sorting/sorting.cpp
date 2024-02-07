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

void omp_bucket_sort(std::vector<float> &arr, int num_buckets, int num_threads) {
    std::vector<std::atomic<int>> bucket_sizes(num_buckets);
    std::vector<std::vector<float>> buckets(num_buckets);

    std::vector<size_t> shifts(num_threads, 0);

    #pragma omp parallel num_threads(num_threads) shared(shifts)
    {
        #pragma omp for schedule(static)
        for(int bucket_idx = 0; bucket_idx < num_buckets; bucket_idx++) {
            bucket_sizes[bucket_idx].store(0);
        }

        #pragma omp for schedule(static)
        for(auto val: arr) {
            int bucket_idx = val * num_buckets;
            bucket_sizes[bucket_idx].fetch_add(1);
        }

        #pragma omp for schedule(static)
        for(int bucket_idx = 0; bucket_idx < num_buckets; bucket_idx++) {
            buckets[bucket_idx].resize(bucket_sizes[bucket_idx]);
            bucket_sizes[bucket_idx].store(0);
        }

        #pragma omp for schedule(static)
        for(auto val: arr) {
            int bucket_idx = val * num_buckets;
            int write_idx = bucket_sizes[bucket_idx].fetch_add(1);
            buckets[bucket_idx][write_idx] = val;
        }

        #pragma omp for schedule(static)
        for(auto &bucket: buckets) {
            std::sort(bucket.begin(), bucket.end());
        }

        size_t write_count = 0;
        #pragma omp for schedule(static)
        for(const auto &bucket: buckets) {
            write_count += bucket.size();
        }
        int tid = omp_get_thread_num();
        shifts[tid] = write_count;

        #pragma omp barrier
        #pragma omp single
        {
            int prev = 0;
            for(int i = 0; i < num_threads; i++) {
                int num = shifts[i];
                shifts[i] = prev;
                prev += num;
            }
        }
        #pragma omp barrier

        size_t write_idx = 0;
        size_t shift = shifts[tid];
        #pragma omp for schedule(static)
        for(const auto &bucket: buckets) {
            for(auto val: bucket) {
                arr[shift + write_idx] = val;
                write_idx++;
            }
        }
    }
}

void tbb_bucket_sort(std::vector<float> &arr, int num_buckets) {
    std::vector<std::atomic<int>> bucket_sizes(num_buckets);
    std::vector<std::vector<float>> buckets(num_buckets);

    tbb::parallel_for(0, num_buckets, [&](int bucket_idx)
    {
        bucket_sizes[bucket_idx].store(0);
    });

    tbb::parallel_for(0, static_cast<int>(arr.size()), [&](int i)
    {
        float val = arr[i];
        int bucket_idx = val * num_buckets;
        bucket_sizes[bucket_idx].fetch_add(1);
    });

    tbb::parallel_for(0, num_buckets, [&](int bucket_idx)
    {
        buckets[bucket_idx].resize(bucket_sizes[bucket_idx]);
        bucket_sizes[bucket_idx].store(0);
    });

    tbb::parallel_for(0, static_cast<int>(arr.size()), [&](int i)
    {
        float val = arr[i];
        int bucket_idx = val * num_buckets;
        int write_idx = bucket_sizes[bucket_idx].fetch_add(1);
        buckets[bucket_idx][write_idx] = val;
    });

    tbb::parallel_for(0, num_buckets, [&](int bucket_idx)
    {
        std::sort(buckets[bucket_idx].begin(), buckets[bucket_idx].end());
    });

    /*size_t write_idx = 0;
    for(const auto &bucket: buckets) {
        for(auto val: bucket) {
            arr[write_idx] = val;
            write_idx++;
        }
    }*/

    std::vector<int> sizes(num_buckets, 0);
    tbb::parallel_for(0, num_buckets, [&](int bucket_idx)
    {
        sizes[bucket_idx] = buckets[bucket_idx].size();
    });
    std::vector<int> shifts(num_buckets, 0);

    tbb::parallel_scan(
        tbb::blocked_range<size_t>(0, num_buckets), 0,
        [&sizes, &shifts](const tbb::blocked_range<size_t>& range, int sum, bool is_final) -> int {
            int local_sum = sum;
            for (size_t i = range.begin(); i != range.end(); ++i) {
                shifts[i] = local_sum;
                local_sum += sizes[i];
            }
            return local_sum;
        },
        [](int left_sum, int right_sum) -> int {
            return left_sum + right_sum;
        }
    );

    tbb::parallel_for(0, num_buckets, [&](int bucket_idx)
    {
        int i = 0;
        for(auto val: buckets[bucket_idx]) {
            arr[shifts[bucket_idx] + i] = val;
            i++;
        }
    });
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

    std::array<int, 4> omp_threads = {2, 4, 8};
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
