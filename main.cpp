#include <iostream>
#include <algorithm>
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include <map>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

using namespace std;

class AsyncExecutor {
private:
    std::map<std::chrono::time_point<std::chrono::system_clock>, vector<std::function<void()>>> events;
    std::chrono::time_point<std::chrono::system_clock> init_time;
    std::thread worker_thread;

    std::atomic<bool> abort_required;

    std::condition_variable cv;
    std::mutex cv_m;

    std::mutex events_mutex;

    bool get_next_event(std::pair<std::chrono::time_point<std::chrono::system_clock>, vector<std::function<void()>>> &res)
    {
        if(!events.empty())
        {
            res = *(events.begin());
            return true;
        }
        else
        {
            return false;
        }
    }
public:
    AsyncExecutor(): abort_required(false)
    {
        init_time = std::chrono::high_resolution_clock::now();
        worker_thread = std::thread([this]()
            {
                std::cout << "worker started" << std::endl;
                while(true)
                {
                    events_mutex.lock();
                    std::pair<std::chrono::time_point<std::chrono::system_clock>, vector<std::function<void()>>> next_event;
                    bool event_found = get_next_event(next_event);
                    events_mutex.unlock();

                    //auto wait_for = next_event.first + this->init_time - std::chrono::high_resolution_clock::now();

                    if(event_found)
                    {
                        using namespace std::chrono_literals;
                        std::unique_lock<std::mutex> lk(cv_m);
                        auto now = std::chrono::system_clock::now();
                        if(cv.wait_until(lk, next_event.first, [&next_event]()
                            {
                                return std::chrono::high_resolution_clock::now() > next_event.first;
                            }))
                            std::cerr << "Thread finished waiting. " << std::endl;
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        continue;
                    }

                    events_mutex.lock();
                    events.erase(events.begin());
                    events_mutex.unlock();
                    for(auto func: next_event.second)
                        func();

                    if(abort_required)
                        break;
                }
            });
    }

    void abort_child()
    {
        abort_required = true;
    }

    ~AsyncExecutor()
    {
        worker_thread.join();
    }

    void exec(std::function<void()> func, int delay_ms = 0)
    {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto event_time = current_time + std::chrono::milliseconds(delay_ms);

        events_mutex.lock();
        events[event_time].push_back(func);
        events_mutex.unlock();
    }

    void run_all_seq()
    {
        for(auto [time, functions]: events)
        {
            std::cout << "starting after " << std::chrono::high_resolution_clock::to_time_t(time) << " pause" << std::endl;
            for(const auto& func: functions)
                func();
        }
    }
};

int main()
{
    AsyncExecutor executor;

    executor.exec([](){ std::cout << 5 << std::endl; }, 5000);

    executor.exec([](){ std::cout << 6 << std::endl; }, 10000);
    executor.exec([](){ std::cout << 1 << std::endl; }, 100);
    executor.exec([](){ std::cout << 2 << std::endl; }, 200);

    std::cout << "master going into sleep" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(10));
    executor.abort_child();

    return 0;
}
