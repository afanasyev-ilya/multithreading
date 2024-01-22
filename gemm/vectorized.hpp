#pragma once

void vectorized(const std::vector<float> &A,
                const std::vector<float> &B,
                std::vector<float> &C,
                int m_s, int n_s, int k_s) {
    auto start = std::chrono::high_resolution_clock::now();

    for(int i = 0; i < m_s; i++) {
        float *c = C.data() + i*n_s;

        for (int j = 0; j < n_s; j += 8)
            _mm256_storeu_ps(c + j, _mm256_setzero_ps());

        for(int k = 0; k < k_s; k++) {
            const float *b = B.data() + k*n_s;
            __m256 a = _mm256_set1_ps(A[i*k_s + k]);
            for (int j = 0; j < n_s; j += 16)
            {
                _mm256_storeu_ps(c + j + 0, _mm256_fmadd_ps(a,
                                                            _mm256_loadu_ps(b + j + 0), _mm256_loadu_ps(c + j + 0)));
                _mm256_storeu_ps(c + j + 8, _mm256_fmadd_ps(a,
                                                            _mm256_loadu_ps(b + j + 8), _mm256_loadu_ps(c + j + 8)));
            }
        }
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto time = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    std::cout << "vectorized perf = " << (m_s*n_s*k_s)/(time*1e3) << " Gflops, time = " << time << " ms\n";
}

