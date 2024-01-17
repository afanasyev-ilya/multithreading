#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <chrono>
#include <execution>
#include <thread>

void fill_with_rands(std::vector<int> &data) {
    std::random_device dev;
    std::mt19937 generator(dev());
    std::uniform_int_distribution<int> distr(0, data.size() - 1);

    for(auto &val: data) {
        val = distr(generator);
    }
}

int partition(std::vector<int> &arr, int start, int end)
{
    int pivot = arr[start];

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

void quick_sort(std::vector<int> &arr, int start, int end, int depth, int max_threads, bool parallel)
{
    // base case
    if (start >= end)
        return;

    // partitioning the array
    int p = partition(arr, start, end);

    // Sorting the left part
    if(parallel && depth < 4) {
        std::thread t1(quick_sort, std::ref(arr), start, p-1, depth + 1, max_threads, parallel);
        std::thread t2(quick_sort, std::ref(arr), p+1, end, depth + 1, max_threads, parallel);
        t1.join();
        t2.join();
    } else {
        quick_sort(arr, start, p - 1, depth + 1, max_threads, parallel);
        quick_sort(arr, p + 1, end, depth + 1, max_threads, parallel);
    }

}

void time(const std::string &name, void(*sort_f)(std::vector<int> &, int, int, int, int, bool), std::vector<int> &data, int threads, bool parallel) {
    fill_with_rands(data);

    auto start = std::chrono::high_resolution_clock::now();
    sort_f(data, 0, (int)data.size(), 1, threads, parallel);
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << name << " time: " << ms << " ms" << std::endl;
}

template <typename ExecutionPolicy>
void time(const std::string &name, std::vector<int> &data, ExecutionPolicy&& policy) {
    fill_with_rands(data);

    auto start = std::chrono::high_resolution_clock::now();
    std::sort(policy, data.begin(), data.end());
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << name << " time: " << ms << " ms" << std::endl;
}

int main() {
    std::vector<int> data(1e6);
    int size = 1e7;
    int iters = 4;

    for(int i = 0; i < iters; i++)
        time("first sequential quick sort, 1 thread", quick_sort, data, 1, false);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("first parallel quick sort, 1 thread", quick_sort, data, 1, true);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("std sequential", data, std::execution::seq);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("std parallel", data, std::execution::par);
    std::cout << std::endl;

    for(int i = 0; i < iters; i++)
        time("std parallel unseq", data, std::execution::par_unseq);
    std::cout << std::endl;

    return 0;
}
