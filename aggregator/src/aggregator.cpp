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
#include <deque>
#include <condition_variable>

enum EventType {
    LIKE = 0,
    VIEW = 1
};

struct Event {
    uint64_t timestamp {0};
    EventType type {LIKE};
    int post_id {0};
};

template <typename T>
class BlockingQueue {
    std::deque<T> data_;
    std::mutex mtx_;
    std::condition_variable cv_not_empty_;
    bool is_closed_{false};

public:
    void push(const T &val) {
        std::unique_lock<std::mutex> lk(mtx_);

        data_.push_back(val);

        lk.unlock();

        cv_not_empty_.notify_one();
    }
    void pop(T &out) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_not_empty_.wait(lk, [&] { return !data_.empty(); });
        if(!data_.empty()) {
            out = data_.front();
            data_.pop_front();
        }
    }

    bool is_closed() {
        return is_closed_;
    }

    void close() {
        is_closed_ = true;
    }
};

class EventGenerator {
    std::random_device rd_;
    std::mt19937 engine_;

    int max_events_per_sec_;
    int max_post_id_;

    BlockingQueue<Event> &events_;
    
    uint64_t get_now_ms_utc() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
public:
    EventGenerator(BlockingQueue<Event> &events, int max_events_per_sec, int max_post_id): 
        engine_(rd_()), 
        max_events_per_sec_(max_events_per_sec), 
        max_post_id_(max_post_id),
        events_(events) {}

    void run() {
        std::uniform_int_distribution<int> num_events_generator(0, max_events_per_sec_);
        std::uniform_int_distribution<int> event_type_generator(0, 1);
        std::uniform_int_distribution<int> post_id_generator(0, max_post_id_);

        int num_events_this_seconds = num_events_generator(engine_);
        uint64_t event_time = get_now_ms_utc();
        for(int i = 0; i < num_events_this_seconds; i++) {
            int post_id = post_id_generator(engine_);
            EventType event_type = static_cast<EventType>(event_type_generator(engine_));
            events_.push({event_time, event_type, post_id});
        }
    }
};

int main(int argc, char* argv[]) {
    const int max_events_per_sec = 10;
    const int max_posts = 10000;
    const int work_time = 10;

    BlockingQueue<Event> events_q;

    auto gen_workflow = [&]() {
        EventGenerator gen(events_q, max_events_per_sec, max_posts);
        auto start = std::chrono::steady_clock::now();
        auto next = start;
        for(int ts = 0; ts < work_time; ts++) {
            gen.run();
            next += std::chrono::seconds{1};

            auto now = std::chrono::steady_clock::now();
            if(now < next) {
                std::this_thread::sleep_until(next);
            }

            events_q.close();
        }
    };

    std::thread gen_thread(gen_workflow);

    auto worker_workflow = [&]() {
        while(!events_q.is_closed()) {
            Event new_event;
            
            events_q.pop(new_event);

            std::cout << "time: " << new_event.timestamp << ", type: " << new_event.type << ", post id: " << new_event.post_id << std::endl;
        }
    };

    std::thread worker_thread(worker_workflow);

    gen_thread.join();
    
    return 0;
}