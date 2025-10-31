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
    bool push(const T &val) {
        std::unique_lock<std::mutex> lk(mtx_);
        if(is_closed_)
            return false;

        data_.push_back(val);

        lk.unlock();

        cv_not_empty_.notify_one();
        return true;
    }

    bool pop(T &out) {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_not_empty_.wait(lk, [&] { return is_closed_ || !data_.empty(); });
        if(data_.empty()) {
            return false;
        }
        out = data_.front();
        data_.pop_front();
        return true;
    }

    bool is_closed() {
        std::lock_guard<std::mutex> lk(mtx_);
        return is_closed_;
    }

    void close() {
        std::lock_guard<std::mutex> lk(mtx_);
        is_closed_ = true;
        cv_not_empty_.notify_all();
    }
};

uint64_t get_now_ms_utc() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string ts2date_and_time_utc(uint64_t timestamp_ms) {
    std::time_t time_sec = timestamp_ms / 1000;
    std::tm *tm_utc = std::gmtime(&time_sec);
    char buffer[30];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_utc);
    return std::string(buffer);
}

std::string ts2date_and_time_msk(uint64_t timestamp_ms) {
    auto utc_time = ts2date_and_time_utc(timestamp_ms);
    // Moscow is UTC+3
    std::time_t time_sec = timestamp_ms / 1000 + 3 * 3600;
    std::tm *tm_msk = std::gmtime(&time_sec);
    char buffer[30];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_msk);
    return std::string(buffer); 
}

class EventGenerator {
    std::random_device rd_;
    std::mt19937 engine_;

    int max_events_per_sec_;
    int max_post_id_;

    BlockingQueue<Event> &events_;
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
            if(!events_.push({event_time, event_type, post_id}))
                break;
        }
        std::cout << "generated " << num_events_this_seconds << " events" << std::endl;
    }
};

class WindowAggregator {
    int window_sec_;
    int bucket_sec_;

    struct Bucket {
        uint64_t start_time_ms;
        uint64_t end_time_ms;
        std::unordered_map<int, int> likes_count; // post id -> like
        std::unordered_map<int, int> views_count; // post id -> view
        void add_like(int post_id) {
            likes_count[post_id]++;
        }
        void add_view(int post_id) {
            views_count[post_id]++;
        }
    };

    std::unordered_map<int, int> total_likes_count; // post id -> like
    std::unordered_map<int, int> total_views_count; // post id -> view

    int num_buckets_;
    std::vector<Bucket> buckets_;
    int cur_bucket_;
public:
    WindowAggregator(int window_sec, int bucket_sec) {
        assert(window_sec % bucket_sec == 0);
        this->window_sec_ = window_sec;
        this->bucket_sec_ = bucket_sec;
        this->num_buckets_ = window_sec_ / bucket_sec_;
        this->buckets_.resize(num_buckets_);
        this->cur_bucket_ = 0;
    }

    void process_event(Event &e) {
        advance_to(e.timestamp);
        add_event_to_current_bucket(e);
    }

    void add_event_to_current_bucket(Event &e) {
        if(e.type == VIEW) {
            buckets_[cur_bucket_].add_view(e.post_id);
            total_views_count[e.post_id] += 1;
        } else if(e.type == LIKE) {
            buckets_[cur_bucket_].add_like(e.post_id);
            total_likes_count[e.post_id] += 1;
        }
    }

    void advance_to(uint64_t timestamp) {
        static uint64_t bucket_start_time = 0;
        if(bucket_start_time == 0) {
            // set time for first bucket
            buckets_[cur_bucket_].start_time_ms = timestamp;
            buckets_[cur_bucket_].end_time_ms = timestamp + bucket_sec_ * 1000;
            bucket_start_time = timestamp;
            return;
        }

        uint64_t elapsed_ms = timestamp - bucket_start_time;
        uint64_t bucket_duration_ms = bucket_sec_ * 1000;
        int buckets_to_advance = elapsed_ms / bucket_duration_ms;
        
        for(int i = 0; i < buckets_to_advance; i++) {
            cur_bucket_ = (cur_bucket_ + 1) % num_buckets_;

            // remove the old bucket from total counts
            drop_stats_from_total(cur_bucket_);

            buckets_[cur_bucket_] = Bucket(); // reset the bucket

            // set times in milliseconds
            uint64_t new_bucket_start = bucket_start_time + i * bucket_duration_ms;
            buckets_[cur_bucket_].start_time_ms = new_bucket_start;
            buckets_[cur_bucket_].end_time_ms = new_bucket_start + bucket_duration_ms;

            bucket_start_time += bucket_duration_ms;
        }
    }

    void drop_stats_from_total(int bucket_idx) {
        for(const auto &[post_id, like_count] : buckets_[bucket_idx].likes_count) {
            total_likes_count[post_id] -= like_count;
            if(total_likes_count[post_id] <= 0) {
                total_likes_count.erase(post_id);
            }
        }

        for(const auto &[post_id, view_count] : buckets_[bucket_idx].views_count) {
            total_views_count[post_id] -= view_count;
            if(total_views_count[post_id] <= 0) {
                total_views_count.erase(post_id);
            }
        }
    }

    int get_total_likes(int post_id) {
        return total_likes_count[post_id];
    }

    int get_total_views(int post_id) {
        return total_views_count[post_id];
    }

    void print_event_stats() {
        std::cout << "Total Likes:" << std::endl;
        for(const auto &[post_id, like_count] : total_likes_count) {
            std::cout << "Post ID: " << post_id << ", Likes: " << like_count << std::endl;
        }

        std::cout << "Total Views:" << std::endl;
        for(const auto &[post_id, view_count] : total_views_count) {
            std::cout << "Post ID: " << post_id << ", Views: " << view_count << std::endl;
        }
    }

    void print_bucket_stats() {
        for(int i = 0; i < num_buckets_; i++) {
            std::cout << "Bucket " << i << " Likes:" << std::endl;
            for(const auto &[post_id, like_count] : buckets_[i].likes_count) {
                std::cout << "Post ID: " << post_id << ", Likes: " << like_count << std::endl;
            }

            std::cout << "Bucket " << i << " Views:" << std::endl;
            for(const auto &[post_id, view_count] : buckets_[i].views_count) {
                std::cout << "Post ID: " << post_id << ", Views: " << view_count << std::endl;
            }
            std::cout << "Bucket time range: " << ts2date_and_time_msk(buckets_[i].start_time_ms) <<
                 " - " << ts2date_and_time_msk(buckets_[i].end_time_ms) << " (UTC)" << std::endl;
            std::cout << "------------------------" << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    const int max_events_per_sec = 1000;
    const int max_posts = 1000;
    const int work_time = 30; // seconds

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
        }

        events_q.close();
    };

    std::thread gen_thread(gen_workflow);

    auto worker_workflow = [&]() {
        const int window_time = 10; // seconds
        const int seconds_per_bucket = 2; // seconds
        Event e;
        WindowAggregator aggr(window_time, seconds_per_bucket);
        while(events_q.pop(e)) {
            aggr.process_event(e);
        }

        aggr.print_bucket_stats();
        //aggr.print_event_stats();
    };

    std::thread worker_thread(worker_workflow);

    gen_thread.join();
    worker_thread.join();
    
    return 0;
}