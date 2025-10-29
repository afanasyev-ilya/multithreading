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
    std::atomic<int> read_ops_{0};
    std::atomic<int> write_ops_{0};
public:
    virtual void add_view(int post_id) = 0;
    virtual int get_views(int post_id) = 0;

    int get_read_ops() const  { return read_ops_.load(std::memory_order_relaxed); }
    int get_write_ops() const { return write_ops_.load(std::memory_order_relaxed); }
};

class DistributedCounter: public BaseCounter {
    std::unordered_map<int, int> counters_;
    std::shared_mutex mtx_;

public:
    explicit DistributedCounter(int expected_posts = 0) {
        if(expected_posts > 0) {
            counters_.reserve(expected_posts);
        }
    }

    void add_view(int post_id) {
        std::unique_lock<std::shared_mutex> lock(mtx_);

        counters_[post_id]++;

        write_ops_++;
    }

    int get_views(int post_id) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = counters_.find(post_id);
        read_ops_++;
        if(it == counters_.end()) {
            return 0;
        } else {
            return it->second;
        }
    }
};


class AtomicDistributedCounter : public BaseCounter {
    std::unordered_map<int, std::shared_ptr<std::atomic<int>>> counters_;
    mutable std::shared_mutex mtx_;
public:
    explicit AtomicDistributedCounter(size_t expected_posts = 0) {
        if (expected_posts) counters_.reserve(expected_posts);
    }

    void add_view(int post_id) {
        {
            std::shared_lock<std::shared_mutex> rlock(mtx_);
            auto it = counters_.find(post_id);
            if (it != counters_.end()) {
                it->second->fetch_add(1, std::memory_order_relaxed);
                write_ops_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }

        {
            std::unique_lock<std::shared_mutex> wlock(mtx_);
            auto [it, inserted] =
                counters_.try_emplace(post_id, std::make_shared<std::atomic<int>>(0));
            it->second->fetch_add(1, std::memory_order_relaxed);
            write_ops_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    int get_views(int post_id) {
        std::shared_ptr<std::atomic<int>> ptr;
        {
            std::shared_lock<std::shared_mutex> rlock(mtx_);
            auto it = counters_.find(post_id);
            if (it != counters_.end()) ptr = it->second; // copy shared_ptr while locked
        }
        read_ops_.fetch_add(1, std::memory_order_relaxed);
        if (!ptr) return 0;
        return ptr->load(std::memory_order_relaxed);
    }
};


bool isPowerOfTwo(unsigned int n) { // Use unsigned for popcount
    return (n > 0) && (std::popcount(n) == 1);
}

class ShardedDistributedCounter : public BaseCounter {
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
    explicit ShardedDistributedCounter(int num_shards, int expected_posts) : num_shards_(num_shards), shards_(num_shards_) {
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

    void print_stats(double duration_ms) {
        int ops = data_.get_read_ops() + data_.get_write_ops();
        double QPS = ops / (duration_ms * 1e3);
        std::cout << "inner duration: " << duration_ms << " ms" << std::endl;
        std::cout << "inner ops: " << ops << std::endl;
        std::cout << "inner mQPS: " << QPS/1e6 << std::endl;
    }
public:
    WorkloadManager(BaseCounter &data) : data_(data) {

    }
    void run(const std::vector<Request> &cmds, int start_cmd, int end_cmd) {
        //auto start_time = std::chrono::high_resolution_clock::now();
        //std::cout << "thread borders " << start_cmd << " - " << end_cmd << std::endl;
        for(int cmd_id = start_cmd; cmd_id < end_cmd; cmd_id++) {
            const auto &cmd = cmds[cmd_id];
            if(cmd.op_type == GET_VIEWS)
                data_.get_views(cmd.post_id);
            if(cmd.op_type == ADD_VIEW)
                data_.add_view(cmd.post_id);
        }
        //auto end_time = std::chrono::high_resolution_clock::now();
        //auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        //print_stats(duration_ms.count());
    }
};

int main(int argc, char* argv[]) {
    Settings settings;
    load_cli_settings(settings, argc, argv);

    // DistributedCounter post_data;
    ShardedDistributedCounter post_data(settings.num_shards, settings.max_posts);

    RequestGenerator gen;

    // gen input data
    auto cmds = gen.gen_batch(settings.num_requests, settings.max_posts, settings.reads_per_write);

    // start parallel workflow
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;

    const int total_work = cmds.size();
    const int work_per_thread = (total_work - 1) / settings.num_threads + 1;
    std::cout << "num shards: " << settings.num_shards << std::endl;
    std::cout << "estimated num posts : " << settings.max_posts << std::endl;
    std::cout << "   posts per shard : " << (settings.max_posts - 1)/settings.num_shards + 1 << std::endl;
    std::cout << "reads to writes ratio: " << settings.reads_per_write << std::endl;
    std::cout << "total requests (commands) : " << total_work << std::endl;
    std::cout << "work_per_thread (commands) : " << work_per_thread << std::endl;


    for(int thread_idx = 0; thread_idx < settings.num_threads; thread_idx++) {
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
    std::cout << "WALL time: " << duration_ms.count() << " ms" << std::endl;

    return 0;
}