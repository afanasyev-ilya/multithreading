#pragma once

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