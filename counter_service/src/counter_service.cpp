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


class DistributedCounter {
    std::unordered_map<int, int> counters_;
    std::shared_mutex mtx_;
    std::atomic<int> read_ops_{0};
    std::atomic<int> write_ops_{0};

public:
    void add_view(int post_id) {
        std::unique_lock<std::shared_mutex> lock(mtx_);

        counters_[post_id]++;

        write_ops_++;
    }

    int get_views(int post_id) {
        std::unique_lock<std::shared_mutex> lock(mtx_);
        auto it = counters_.find(post_id);
        if(it == counters_.end()) {
            return 0;
        } else {
            return it->second;
        }
        read_ops_++;
    }

    int get_read_ops() const { return read_ops_;};
    int get_write_ops() const { return write_ops_;};
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
    DistributedCounter &data_;

    void print_stats(double duration_s) {
        int ops = data_.get_read_ops() + data_.get_write_ops();
        double QPS = ops / duration_s;
        std::cout << "inner duration: " << duration_s << " sec" << std::endl;
        std::cout << "inner ops: " << ops << std::endl;
        std::cout << "inner QPS: " << QPS << std::endl;
    }
public:
    WorkloadManager(DistributedCounter &data) : data_(data) {

    }
    void run(const std::vector<Request> &cmds, int start_cmd, int end_cmd) {
        auto start_time = std::chrono::high_resolution_clock::now();
        std::cout << "thread borders " << start_cmd << " - " << end_cmd << std::endl;
        for(int cmd_id = start_cmd; cmd_id < end_cmd; cmd_id++) {
            const auto &cmd = cmds[cmd_id];
            if(cmd.op_type == GET_VIEWS)
                data_.get_views(cmd.post_id);
            if(cmd.op_type == ADD_VIEW)
                data_.add_view(cmd.post_id);
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_s = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
        print_stats(duration_s.count());
    }
};

int main(int argc, char* argv[]) {
    const int num_requests = 10000000;
    const int max_posts = 10000;
    const int reads_per_write = 5;
    const int num_threads = 1;

    DistributedCounter post_data;

    RequestGenerator gen;


    // gen input data
    auto cmds = gen.gen_batch(num_requests, max_posts, reads_per_write);

    // start parallel workflow
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;

    const int total_work = cmds.size();
    const int work_per_thread = (total_work - 1) / num_threads + 1;
    for(int thread_idx = 0; thread_idx < num_threads; thread_idx++) {
        std::cout << "total_work : " << total_work << std::endl;
        std::cout << "work_per_thread : " << work_per_thread << std::endl;

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
    auto duration_s = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    std::cout << "WALL time: " << std::chrono::duration<double>(duration_s).count() << " seconds" << std::endl;

    return 0;
}