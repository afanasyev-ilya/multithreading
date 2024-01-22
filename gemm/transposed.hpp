#pragma once

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
