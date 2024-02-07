//
// Created by Илья Афанасьев on 07.02.2024.
//
#include "bucket_sort.h"

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