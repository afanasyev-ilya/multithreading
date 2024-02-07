//
// Created by Илья Афанасьев on 07.02.2024.
//
#include <vector>
#include <tbb/tbb.h>
#include <atomic>
#include <algorithm>
#include <omp.h>

void bucket_sort(std::vector<float> &arr, int num_buckets);
void omp_bucket_sort(std::vector<float> &arr, int num_buckets, int num_threads);
void tbb_bucket_sort(std::vector<float> &arr, int num_buckets);
