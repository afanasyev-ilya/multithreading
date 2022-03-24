#include <iostream>
#include <algorithm>
#include <vector>
#include <random>
#include <thread>
#include <chrono>

using namespace std;

void swap(int *a,int *b)
{
    int temp = *a;
    *a=*b;
    *b = temp;
}

template <typename T>
int partition (vector<T> &array, int p, int r)
{
    T x = array[r];
    int i = p - 1;

    for (int j = p; j <= r- 1; j++)
    {
        if (array[j] <= x)
        {
            i++;
            swap (&array[i], &array[j]);
        }
    }
    swap (&array[i + 1], &array[r]);
    return (i + 1);
}

template <typename T>
void parallel_quick_sort(vector<T> &array, int left, int right, int depth)
{
    if (left < right)
    {
        int pivot = partition(array, left, right);

        if(depth <= 4)
        {
            std::thread tid1(parallel_quick_sort<T>, std::ref(array), left, pivot - 1, depth + 1);
            std::thread tid2(parallel_quick_sort<T>, std::ref(array), pivot + 1, right, depth + 1);
            tid1.join();
            tid2.join();
        }
        else
        {
            parallel_quick_sort(array, left, pivot - 1, depth + 1);
            parallel_quick_sort(array, pivot + 1, right, depth + 1);
        }
    }
}

template <typename T>
void rand_init(vector<T> &array)
{
    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> uni(0, 1000);
    for(auto & it : array)
    {
        it = uni(rng);
    }
}

int main()
{
    int size = 1000000;
    std::vector<int> data(size, 0);

    rand_init(data);

    auto t1 = std::chrono::high_resolution_clock::now();
    parallel_quick_sort(data, 0, size, 1);
    auto t2 = std::chrono::high_resolution_clock::now();
    std::cout << "parallel time: " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << std::endl;

    rand_init(data);

    t1 = std::chrono::high_resolution_clock::now();
    std::sort(data.begin(), data.end());
    t2 = std::chrono::high_resolution_clock::now();
    std::cout << "std sort time: " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << std::endl;


    return 0;
}
