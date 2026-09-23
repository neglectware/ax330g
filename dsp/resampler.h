// Rational polyphase resampler, the same design scipy.signal.resample_poly
// uses (firwin lowpass, kaiser window, cutoff 1/max(L,M), half length
// halfMult*max(L,M), default halfMult=10, scipy's own default), so the C++
// core can be nulled against the Python engine. Streaming: feed blocks, get
// whatever output is ready.
#pragma once
#include <cmath>
#include <vector>
#include <cstddef>

namespace ax30g {

inline double besselI0(double x) {
    double s = 1.0, t = 1.0; const double y = x * x / 4.0;
    for (int k = 1; k < 200; ++k) { t *= y / double(k * k); s += t; if (t < 1e-16 * s) break; }
    return s;
}

class Resampler {
public:
    // fsIn -> fsOut expressed as L/M (up/down). halfMult: low-pass half-length
    // in units of maxRate taps per side (scipy resample_poly's own default is
    // 10; 100 is flat to 19 kHz round-trip and is what the measured converter
    // chain needs in front of it -- see engine/render.py::resampler_window and
    // docs/chain-fit-2026-09-14.md). beta: kaiser parameter.
    Resampler(int L, int M, int halfMult = 10, double beta = 14.0) : L_(L), M_(M) {
        const int maxRate = std::max(L, M);
        half_ = halfMult * maxRate;
        const int N = 2 * half_ + 1;
        h_.resize(N);
        const double fc = 1.0 / maxRate;   // cycles per high-rate sample
        double sum = 0.0;
        const double i0b = besselI0(beta);
        for (int n = 0; n < N; ++n) {
            const double m = n - half_;
            const double sinc = m == 0 ? 1.0 : std::sin(M_PI * fc * m) / (M_PI * fc * m);
            const double r = 2.0 * n / (N - 1) - 1.0;
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            h_[n] = fc * sinc * w;
            sum += h_[n];
        }
        for (auto& v : h_) v *= double(L) / sum;   // unity DC gain after zero-stuffing by L
        // polyphase split: phase p uses h[p], h[p+L], h[p+2L], ...
        tapsPerPhase_ = (N + L - 1) / L;
        poly_.assign(size_t(L) * tapsPerPhase_, 0.0);
        for (int n = 0; n < N; ++n) poly_[size_t(n % L) * tapsPerPhase_ + n / L] = h_[n];
        hist_.assign(tapsPerPhase_ + 4, 0.0);
        reset();
    }
    void reset() { std::fill(hist_.begin(), hist_.end(), 0.0); t_ = 0; nIn_ = 0; }
    // group delay of the whole filter in *input* samples (scipy compensates it; so do we)
    double delayInputSamples() const { return double(half_) / L_; }

    // push n input samples; append output samples to out
    void process(const double* in, size_t n, std::vector<double>& out) {
        for (size_t i = 0; i < n; ++i) {
            // shift history (small: ~10*max/L taps)
            for (size_t k = hist_.size() - 1; k > 0; --k) hist_[k] = hist_[k - 1];
            hist_[0] = in[i];
            ++nIn_;
            // high-rate index of the newest input sample is (nIn_-1)*L; we can
            // emit outputs whose (t + half_) <= (nIn_-1)*L
            while (t_ + half_ <= (nIn_ - 1) * (long long)L_) {
                const long long tt = t_ + half_;              // delay-compensated tap centre
                const int phase = int(tt % L_);
                const long long base = tt / L_;               // input index of tap j=0
                const long long newest = nIn_ - 1;
                double acc = 0.0;
                const double* ph = &poly_[size_t(phase) * tapsPerPhase_];
                for (int j = 0; j < tapsPerPhase_; ++j) {
                    const long long idx = base - j;           // input sample index
                    const long long back = newest - idx;      // 0 = newest
                    if (back < 0 || back >= (long long)hist_.size()) continue;
                    acc += ph[j] * hist_[size_t(back)];
                }
                out.push_back(acc);
                t_ += M_;
            }
        }
    }
    int L() const { return L_; }
    int M() const { return M_; }
private:
    int L_, M_, half_, tapsPerPhase_;
    std::vector<double> h_, poly_, hist_;
    long long t_ = 0, nIn_ = 0;
};

} // namespace ax30g
