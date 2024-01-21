#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>

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

void naive(const std::vector<float> &A,
           const std::vector<float> &B,
           std::vector<float> &C,
           int m_s, int n_s, int k_s) {
    auto start = std::chrono::high_resolution_clock::now();
    for(int i = 0; i < m_s; i++) {
        for(int j = 0; j < n_s; j++) {
            float sum = 0.0f;
            for(int k = 0; k < k_s; k++) {
                sum += A[i*k_s + k]*B[k*n_s + j];
            }
            C[i*n_s + j] = sum;
        }
    }
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto time = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << "naive perf = " << (m_s*n_s*k_s)/(time*1e3) << " Gflops, time = " << time << " ms\n";
}

void transposed(const std::vector<float> &A,
                const std::vector<float> &B,
                std::vector<float> &C,
                int m_s, int n_s, int k_s) {
    std::vector<float> B_tmp = B;
    for(int i = 0; i < k_s; i++) {
        for(int j = i + 1; j < n_s; j++) {
            std::swap(B_tmp[i * n_s + j], B_tmp[j * k_s + i]);
        }
    }

    auto start = std::chrono::high_resolution_clock::now();

    for(int i = 0; i < m_s; i++) {
        for(int j = 0; j < n_s; j++) {
            float sum = 0;
            for(int k = 0; k < k_s; k++) {
                sum += A[i*k_s + k]*B_tmp[j*k_s + k];
            }
            C[i*n_s + j] = sum;
        }
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto time = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << "transposed perf = " << (m_s*n_s*k_s)/(time*1e3) << " Gflops, time = " << time << " ms\n";
}

void reordered(const std::vector<float> &A,
               const std::vector<float> &B,
               std::vector<float> &C,
               int m_s, int n_s, int k_s) {
    auto start = std::chrono::high_resolution_clock::now();

    for(int i = 0; i < m_s; i++) {
        float *c = C.data() + i*n_s;
        for(int j = 0; j < n_s; j++) {
            c[j] = 0;
        }

        for(int k = 0; k < k_s; k++) {
            float a = A[i*k_s + k];
            const float *b = B.data() + k*n_s;
            for(int j = 0; j < n_s; j++) {
                c[j] += a * b[j];
            }
        }
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto time = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << "reordered perf = " << (m_s*n_s*k_s)/(time*1e3) << " Gflops, time = " << time << " ms\n";
}

bool combinedToleranceCompare(float x, float y)
{
    float maxXYOne = std::max( { 1.0f, std::fabs(x) , std::fabs(y) } ) ;
    return std::fabs(x - y) <= std::numeric_limits<float>::epsilon()*maxXYOne ;
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
    std::vector<float> C_t(m_s*n_s, 0);
    std::vector<float> C_reordered(m_s*n_s, 0);
    std::vector<float> A(m_s*k_s, 0);
    std::vector<float> B(k_s*n_s, 0);
    rand_init(A);
    rand_init(B);
    // heat
    naive(A, B, C, m_s, n_s, k_s);

    // different version
    naive(A, B, C, m_s, n_s, k_s);

    transposed(A, B, C_t, m_s, n_s, k_s);
    check(C, C_t);
    //print(C_t, m_s, n_s);

    reordered(A, B, C_reordered, m_s, n_s, k_s);
    check(C, C_reordered);
    //print(C_reordered, m_s, n_s);

    return 0;
}
