//
// Created by Илья Афанасьев on 07.02.2024.
//

#pragma once

#include <vector>
#include <tbb/tbb.h>
#include <atomic>
#include <algorithm>
#include <omp.h>

template <typename T>
void quick_sort(std::vector<T> &arr, int max_threads);
