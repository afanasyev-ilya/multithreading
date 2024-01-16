#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <thread>
#include <chrono>

void prepare_file(const std::string &file_name) {
    std::ofstream file(file_name);
    std::vector<std::string> words = {"code", "leet", "yadro", "samsung", "yandex"};
    std::unordered_map<std::string, int> counts;
    if (file.is_open()) {
        std::random_device rand_dev;
        std::mt19937 generator(rand_dev());
        std::uniform_int_distribution<int> distr(0, words.size() - 1);
        for(int i = 0; i < 5000; i++) {
            int word_idx = distr(generator);
            file << words[word_idx] << " ";
            counts[words[word_idx]]++;
        }
        file.close();
        std::cout << "File written successfully." << std::endl;

        for(auto [word, freq]: counts) {
            std::cout << word << ": " << freq << std::endl;
        }
    } else {
        std::cerr << "Unable to open file." << std::endl;
    }
}

size_t get_file_size(const std::string &file_name) {
    std::ifstream file("example.txt");
    std::string line;

    if (file.is_open()) {
        file.seekg(0, std::ios::end);
        std::streampos file_size = file.tellg();
        file.close();
        return file_size;
    } else {
        std::cerr << "Unable to open file." << std::endl;
        exit(1);
    }
}

int get_num_occurances(const std::string &data, const std::string &pattern) {
    size_t pos = 0;
    int occurances = 0;
    while((pos = data.find(pattern, pos)) != std::string::npos) {
        occurances++;
        pos++;
    }
    return occurances;
}

std::mutex mtx;

void process_part_of_file(const std::string &file_name, const std::string &pattern, size_t start, size_t end, size_t file_size, int *result) {
    std::ifstream file(file_name);
    if (file.is_open()) {
        file.seekg(start, std::ios::beg);

        if(start > 0) {
            char c = 0;
            while (file.tellg() <= end && file.get(c) && c != ' ') {}
        }

        std::string data;
        char c = 0;
        while (file.tellg() <= end && file.get(c)) {
            data += c;
        }

        if(c != ' ' && end != file_size) {
            char c;
            while (file.get(c) && c != ' ') {
                data += c;
            }
        }

        int num_occurances = get_num_occurances(data, pattern);

        file.close();

        {
            std::unique_lock<std::mutex> lk(mtx);
            *result += num_occurances;
        }
    } else {
        std::cerr << "Unable to open file." << std::endl;
        exit(1);
    }
}

int process(const std::string &file_name, int num_threads) {
    auto start = std::chrono::high_resolution_clock::now();
    size_t file_size = get_file_size(file_name);

    int result = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        size_t size_per_thread = (file_size - 1) / num_threads + 1;
        size_t start = i*size_per_thread;
        size_t end = std::min((i+1)*size_per_thread, file_size);
        std::thread worker(process_part_of_file, file_name, "yadro", start, end, file_size, &result);
        threads.push_back(std::move(worker));
    }

    for(auto &worker: threads) {
        worker.join();
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
            elapsed).count();
    std::cout << "num threads " << num_threads << " took " << microseconds << " ms" << std::endl;

    return result;
}

int main() {
    std::string file_name = "example.txt";
    prepare_file(file_name);

    int result = 0;

    std::vector<int> threads = {5, 2, 1};
    int iters = 2;

    for(auto threads_count: threads) {
        for(int i = 0; i < iters; i++) {
            result = process(file_name, threads_count);
            std::cout << "result: " << result << std::endl;
        }
    }

    return 0;
}
