//
// Created by Илья Афанасьев on 07.02.2024.
//

#include "quick_sort.h"

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

template void quick_sort(std::vector<float> &arr, int max_threads);
template void quick_sort(std::vector<int> &arr, int max_threads);
