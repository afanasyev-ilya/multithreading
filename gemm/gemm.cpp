#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <immintrin.h>
#include "vectorized.hpp"
#include "reordered.hpp"
#include "naive.hpp"
#include "transposed.hpp"

void rand_init(std::vector<float> &matrix) {
    std::random_device rand_dev;
    std::mt19937 generator(rand_dev());
    std::uniform_real_distribution<float> distr(-1, 1);
    for(auto &val: matrix) {
        val = distr(generator);
    }
}

void print(const std::vector<float> &matrix, int m, int n) {
    if(m < 10 && n < 10)
        for(int i = 0; i < m; i++) {
            for(int j = 0; j < n; j++) {
                std::cout << matrix[i*n + j] << " ";
            }
            std::cout << std::endl;
        }
}

bool combinedToleranceCompare(float x, float y)
{
    //float maxXYOne = std::max( { 1.0f, std::fabs(x) , std::fabs(y) } ) ;
    //return std::fabs(x - y) <= std::numeric_limits<float>::epsilon()*maxXYOne ;
    return std::fabs(x - y) <= 0.0001;
}

void check(const std::vector<float> &reference, const std::vector<float> &current) {
    bool equal = std::equal(current.begin(), current.end(), reference.begin(),
                            [](float value1, float value2)
                            {
                                constexpr float epsilon = 0.001; // Choose whatever you need here
                                return combinedToleranceCompare(value1, value2);
                            });
    std::cout << "check: " << equal << std::endl;
}

int main() {
    int size = 1024;
    int m_s = size, n_s = size, k_s = size;
    std::vector<float> C(m_s*n_s, 0);
    std::vector<float> A(m_s*k_s, 0);
    std::vector<float> B(k_s*n_s, 0);
    rand_init(A);
    rand_init(B);
    // heat
    naive(A, B, C, m_s, n_s, k_s);

    // different version
    naive(A, B, C, m_s, n_s, k_s);

    std::vector<float> C_transposed(m_s*n_s, 0);
    transposed(A, B, C_transposed, m_s, n_s, k_s);
    check(C, C_transposed);
    C_transposed.resize(0);


    std::vector<float> C_reordered(m_s*n_s, 0);
    reordered(A, B, C_reordered, m_s, n_s, k_s);
    check(C, C_reordered);
    C_reordered.resize(0);

    std::vector<float> C_vectorized(m_s*n_s, 0);
    vectorized(A, B, C_vectorized, m_s, n_s, k_s);
    check(C, C_vectorized);
    C_vectorized.resize(0);

    return 0;
}
