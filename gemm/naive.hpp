#pragma once


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