#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <shared_mutex>
#include <atomic>
#include <random>
#include <mutex>
#include <chrono>
#include <cassert>
#include <memory>
#include "arg_parser.h"

class BaseCounter {
protected:

public:
    virtual void add_view(int post_id) = 0;
    virtual int get_views(int post_id) = 0;
};

class LockMap: public BaseCounter {
    std::unordered_map<int, int> counters_;
    std::shared_mutex mtx_;

public:
    explicit LockMap(int expected_posts = 0) {
        if(expected_posts > 0) {
            counters_.reserve(expected_posts);
        }
    }

    void add_view(int post_id) {
        std::unique_lock<std::shared_mutex> lock(mtx_);

        counters_[post_id]++;
    }

    int get_views(int post_id) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = counters_.find(post_id);
        if(it == counters_.end()) {
            return 0;
        } else {
            return it->second;
        }
    }
};


class AtomicArray : public BaseCounter {
    int max_id_;
    std::vector<std::atomic<int>> counters_; 
public:
    explicit AtomicArray(size_t max_posts): max_id_(max_posts + 1), counters_(max_id_) {
        for(auto &val: counters_) 
            val = 0;
        max_id_ = max_posts + 1;
    }

    void add_view(int post_id) {
        if(post_id < max_id_) {
            counters_[post_id].fetch_add(1, std::memory_order_relaxed);
        }
    }

    int get_views(int post_id) {
        if(post_id < max_id_) {
            return counters_[post_id].load(std::memory_order_relaxed);
        }
        return 0;
    }
};


bool isPowerOfTwo(unsigned int n) { // Use unsigned for popcount
    return (n > 0) && (std::popcount(n) == 1);
}

class ShardedMap : public BaseCounter {
    struct alignas(64) Shard {
        std::unordered_map<int, int> counters_;
        mutable std::shared_mutex mtx_;

        void reserve(int size) {counters_.reserve(size); };
    };
    
    int num_shards_;
    std::vector<Shard> shards_;

    size_t mask_{0};
    std::hash<int> hasher_;

    // Hash+mask avoids expensive division and spreads clustered IDs
    inline int get_shard_idx(int post_id) const noexcept {
        return hasher_(post_id) & mask_;
    }

    /*inline int get_shard_idx(int post_id) const noexcept {
        returnpost_id % num_shards_;
    }*/
public:
    explicit ShardedMap(int num_shards, int expected_posts) : num_shards_(num_shards), shards_(num_shards_) {
        int posts_per_shard = (expected_posts - 1) / num_shards + 1;

        assert(isPowerOfTwo(num_shards_));
        mask_ = num_shards_ - 1;

        for(auto &shard: shards_) {
            shard.reserve(posts_per_shard);
        }
    }

    void add_view(int post_id) {
        int shard_idx = get_shard_idx(post_id);
        auto &shard = shards_[shard_idx];
        {
            std::unique_lock<std::shared_mutex> lock(shard.mtx_);
            shard.counters_[post_id]++;
        }   
    }

    int get_views(int post_id) {
        int shard_idx = get_shard_idx(post_id);
        auto &shard = shards_[shard_idx];
        {
            std::shared_lock<std::shared_mutex> lock(shard.mtx_);
            auto it = shard.counters_.find(post_id);
            if(it == shard.counters_.end()) {
                return 0;
            } else {
                return it->second;
            }
        }
    }
};


enum OperationType {
    GET_VIEWS = 0,
    ADD_VIEW = 1
};

struct Request {
    OperationType op_type {ADD_VIEW};
    int post_id {0};
};

class RequestGenerator {
    std::random_device rd_;
    std::mt19937 engine_;
public:
    RequestGenerator(): engine_(rd_()) {}
    std::vector<Request> gen_batch(int batch_size, int max_post_id, int reads_per_writes = 1) {
        assert(reads_per_writes >= 1);

        std::uniform_int_distribution<int> uniform_dist(0, max_post_id);

        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<Request> batch;
        batch.reserve(batch_size);
        for(int i = 0; i < batch_size; i++) {
            int post_id = uniform_dist(engine_);
            int op_prob = static_cast<int>(uniform_dist(engine_) % (reads_per_writes + 1));
            OperationType op_type;
            if(op_prob == 0) {
                op_type = GET_VIEWS;
            } else {
                op_type = ADD_VIEW;
            }
            batch.push_back({op_type, post_id});
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_s = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
        std::cout << "generation done in : " << std::chrono::duration<double>(duration_s).count() << " sec" << std::endl;

        return batch;
    }
};

class WorkloadManager {
    BaseCounter &data_;

public:
    WorkloadManager(BaseCounter &data) : data_(data) {

    }
    void run(const std::vector<Request> &cmds, int start_cmd, int end_cmd) {
        for(int cmd_id = start_cmd; cmd_id < end_cmd; cmd_id++) {
            const auto &cmd = cmds[cmd_id];
            if(cmd.op_type == GET_VIEWS)
                data_.get_views(cmd.post_id);
            if(cmd.op_type == ADD_VIEW)
                data_.add_view(cmd.post_id);
        }
    }
};

int main(int argc, char* argv[]) {
    Settings settings;
    load_cli_settings(settings, argc, argv);

    // DistributedCounter post_data;
    // ShardedDistributedCounter post_data(settings.num_shards, settings.max_posts);
    AtomicArray post_data(settings.max_posts);

    RequestGenerator gen;

    // gen input data
    auto cmds = gen.gen_batch(settings.num_requests, settings.max_posts, settings.reads_per_write);

    const int total_work = cmds.size();
    std::cout << "num shards: " << settings.num_shards << std::endl;
    std::cout << "estimated num posts : " << settings.max_posts << std::endl;
    std::cout << "   posts per shard : " << (settings.max_posts - 1)/settings.num_shards + 1 << std::endl;
    std::cout << "   estimated data size : " << (settings.max_posts * sizeof(int)) / 1e6 << " MB" << std::endl;
    std::cout << "reads to writes ratio: " << settings.reads_per_write << std::endl;
    std::cout << "total requests (commands) : " << total_work << std::endl;
    std::cout << "avg cmds per post : " << total_work / settings.max_posts << std::endl;

    std::vector<double> thread_times;
    for(int num_threads = 1; num_threads <= settings.max_threads; num_threads *= 2) {
        std::vector<std::thread> threads;

        const int work_per_thread = (total_work - 1) / num_threads + 1;
        auto start_time = std::chrono::high_resolution_clock::now();

        for(int thread_idx = 0; thread_idx < num_threads; thread_idx++) {
            threads.emplace_back([&, thread_idx]() {
                int start_cmd = thread_idx * work_per_thread;
                int end_cmd = std::min((thread_idx + 1) * work_per_thread, total_work);
                WorkloadManager mgr(post_data);
                mgr.run(cmds, start_cmd, end_cmd);
            });
        }

        for(auto &thread: threads) {
            thread.join();
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        thread_times.push_back(duration_ms.count());
    }

    int num_threads = 1;
    for(auto thread_time: thread_times) {
        std::cout << num_threads << " threads) " << thread_time << " ms (" << thread_times[0]/thread_time << "x)" << std::endl;
        num_threads *= 2;
    }

    return 0;
}