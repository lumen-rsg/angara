// =============================================================================
// Angara SIMD module — NEON/SSE intrinsics wrappers for manual vectorization
// =============================================================================
//
// These functions operate on unboxed f64[] arrays (Phase 1).  Declare them in
// Angara via `foreign func`:
//
//   foreign func simd_f64x4_dot(n as i64, a as f64[], b as f64[]) -> f64;
//   foreign func simd_f64x4_add(n as i64, a as f64[], b as f64[], out as f64[]);
//   foreign func simd_f64x4_mul(n as i64, a as f64[], b as f64[], out as f64[]);
//   foreign func simd_f64x4_fma(n as i64, a as f64, x as f64[], y as f64[]);
//
// The FFI marshals f64[] as a raw double* pointer (SIMD-4).

#if defined(__aarch64__) || defined(__arm64__)
#include <arm_neon.h>
#define HAS_NEON 1
#elif defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define HAS_SSE 1
#endif

// ---------------------------------------------------------------------------
// simd_f64x4_dot — NEON-accelerated dot product
//   result = Σ(a[i] * b[i]) for i in [0, n)
// Processes 4 doubles per iteration using NEON FMA.
// ---------------------------------------------------------------------------
double simd_f64x4_dot(int64_t n, const double* a, const double* b) {
    double sum = 0.0;
#if HAS_NEON
    int64_t i = 0;
    // Main loop: 4 elements per iteration via NEON
    if (n >= 4) {
        float64x2_t sum0 = vdupq_n_f64(0.0);
        float64x2_t sum1 = vdupq_n_f64(0.0);
        int64_t vec_end = n - (n % 4);
        for (; i < vec_end; i += 4) {
            float64x2_t a0 = vld1q_f64(a + i);
            float64x2_t a1 = vld1q_f64(a + i + 2);
            float64x2_t b0 = vld1q_f64(b + i);
            float64x2_t b1 = vld1q_f64(b + i + 2);
            sum0 = vfmaq_f64(sum0, a0, b0);  // sum0 += a0 * b0
            sum1 = vfmaq_f64(sum1, a1, b1);  // sum1 += a1 * b1
        }
        // Horizontal reduce: sum = sum0[0] + sum0[1] + sum1[0] + sum1[1]
        float64x2_t sum01 = vaddq_f64(sum0, sum1);
        sum = vgetq_lane_f64(sum01, 0) + vgetq_lane_f64(sum01, 1);
    }
    // Scalar remainder
    for (; i < n; i++) {
        sum += a[i] * b[i];
    }
#elif HAS_SSE
    int64_t i = 0;
    if (n >= 4) {
        __m256d sum0 = _mm256_setzero_pd();
        __m256d sum1 = _mm256_setzero_pd();
        int64_t vec_end = n - (n % 8);
        for (; i < vec_end; i += 8) {
            __m256d av0 = _mm256_loadu_pd(a + i);
            __m256d av1 = _mm256_loadu_pd(a + i + 4);
            __m256d bv0 = _mm256_loadu_pd(b + i);
            __m256d bv1 = _mm256_loadu_pd(b + i + 4);
            sum0 = _mm256_fmadd_pd(av0, bv0, sum0);
            sum1 = _mm256_fmadd_pd(av1, bv1, sum1);
        }
        __m256d sum01 = _mm256_add_pd(sum0, sum1);
        __m128d lo = _mm256_castpd256_pd128(sum01);
        __m128d hi = _mm256_extractf128_pd(sum01, 1);
        __m128d s = _mm_add_pd(lo, hi);
        sum = _mm_cvtsd_f64(_mm_add_pd(s, _mm_unpackhi_pd(s, s)));
    }
    for (; i < n; i++) sum += a[i] * b[i];
#else
    for (int64_t i = 0; i < n; i++) sum += a[i] * b[i];
#endif
    return sum;
}

// ---------------------------------------------------------------------------
// simd_f64x4_add — element-wise vector add: out[i] = a[i] + b[i]
// ---------------------------------------------------------------------------
void simd_f64x4_add(int64_t n, const double* a, const double* b, double* out) {
#if HAS_NEON
    int64_t i = 0;
    for (; i + 3 < n; i += 4) {
        float64x2_t a0 = vld1q_f64(a + i);
        float64x2_t a1 = vld1q_f64(a + i + 2);
        float64x2_t b0 = vld1q_f64(b + i);
        float64x2_t b1 = vld1q_f64(b + i + 2);
        vst1q_f64(out + i,     vaddq_f64(a0, b0));
        vst1q_f64(out + i + 2, vaddq_f64(a1, b1));
    }
    for (; i < n; i++) out[i] = a[i] + b[i];
#elif HAS_SSE
    int64_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256d av0 = _mm256_loadu_pd(a + i);
        __m256d av1 = _mm256_loadu_pd(a + i + 4);
        __m256d bv0 = _mm256_loadu_pd(b + i);
        __m256d bv1 = _mm256_loadu_pd(b + i + 4);
        _mm256_storeu_pd(out + i,     _mm256_add_pd(av0, bv0));
        _mm256_storeu_pd(out + i + 4, _mm256_add_pd(av1, bv1));
    }
    for (; i < n; i++) out[i] = a[i] + b[i];
#else
    for (int64_t i = 0; i < n; i++) out[i] = a[i] + b[i];
#endif
}

// ---------------------------------------------------------------------------
// simd_f64x4_mul — element-wise vector multiply: out[i] = a[i] * b[i]
// ---------------------------------------------------------------------------
void simd_f64x4_mul(int64_t n, const double* a, const double* b, double* out) {
#if HAS_NEON
    int64_t i = 0;
    for (; i + 3 < n; i += 4) {
        float64x2_t a0 = vld1q_f64(a + i);
        float64x2_t a1 = vld1q_f64(a + i + 2);
        float64x2_t b0 = vld1q_f64(b + i);
        float64x2_t b1 = vld1q_f64(b + i + 2);
        vst1q_f64(out + i,     vmulq_f64(a0, b0));
        vst1q_f64(out + i + 2, vmulq_f64(a1, b1));
    }
    for (; i < n; i++) out[i] = a[i] * b[i];
#elif HAS_SSE
    int64_t i = 0;
    for (; i + 7 < n; i += 8) {
        __m256d av0 = _mm256_loadu_pd(a + i);
        __m256d av1 = _mm256_loadu_pd(a + i + 4);
        __m256d bv0 = _mm256_loadu_pd(b + i);
        __m256d bv1 = _mm256_loadu_pd(b + i + 4);
        _mm256_storeu_pd(out + i,     _mm256_mul_pd(av0, bv0));
        _mm256_storeu_pd(out + i + 4, _mm256_mul_pd(av1, bv1));
    }
    for (; i < n; i++) out[i] = a[i] * b[i];
#else
    for (int64_t i = 0; i < n; i++) out[i] = a[i] * b[i];
#endif
}

// ---------------------------------------------------------------------------
// simd_f64x4_fma — fused multiply-add: y[i] = a * x[i] + y[i]  (SAXPY)
// ---------------------------------------------------------------------------
void simd_f64x4_fma(int64_t n, double a, const double* x, double* y) {
#if HAS_NEON
    int64_t i = 0;
    float64x2_t va = vdupq_n_f64(a);
    for (; i + 3 < n; i += 4) {
        float64x2_t x0 = vld1q_f64(x + i);
        float64x2_t x1 = vld1q_f64(x + i + 2);
        float64x2_t y0 = vld1q_f64(y + i);
        float64x2_t y1 = vld1q_f64(y + i + 2);
        vst1q_f64(y + i,     vfmaq_f64(y0, x0, va));  // y0 + x0 * a
        vst1q_f64(y + i + 2, vfmaq_f64(y1, x1, va));
    }
    for (; i < n; i++) y[i] = a * x[i] + y[i];
#elif HAS_SSE
    int64_t i = 0;
    __m256d va = _mm256_set1_pd(a);
    for (; i + 7 < n; i += 8) {
        __m256d xv0 = _mm256_loadu_pd(x + i);
        __m256d xv1 = _mm256_loadu_pd(x + i + 4);
        __m256d yv0 = _mm256_loadu_pd(y + i);
        __m256d yv1 = _mm256_loadu_pd(y + i + 4);
        _mm256_storeu_pd(y + i,     _mm256_fmadd_pd(xv0, va, yv0));
        _mm256_storeu_pd(y + i + 4, _mm256_fmadd_pd(xv1, va, yv1));
    }
    for (; i < n; i++) y[i] = a * x[i] + y[i];
#else
    for (int64_t i = 0; i < n; i++) y[i] = a * x[i] + y[i];
#endif
}
