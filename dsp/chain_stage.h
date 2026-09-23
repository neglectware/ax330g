// ChainStage: the unit's measured converter chain (models/ax30g-chain.json,
// docs/chain-fit-2026-09-14.md), applied once at the I/O (host) rate after
// the up-resampler. Mirrors engine/render.py::_hpf1_sos / chain_fir /
// apply_chain exactly, so a C++ render nulls against the Python engine (see
// docs/chain-cpp-2026-09-14.md).
//
// LF: a cascade of three first-order high-passes per channel, bilinear with
// the corner prewarped (engine/render.py::_hpf1_sos), corners from
// dsp/chain_table.h::kChainLfHpfHz (three at 5.491 Hz).
//
// HF: the remaining measured response (dsp/chain_table.h::kChainHfRe/Im, a
// 20 Hz grid to 22 kHz) turned into a short FIR at prepare() time
// (engine/render.py::chain_fir): interpolate re/im onto the rfft grid of a
// 32768-point transform at the stage's rate, take the real inverse DFT
// directly (only the ~336 output taps that survive the pre/post window are
// ever needed, so the DFT sum is evaluated only at those n, never the full
// 32768-point h[]), keep -2 ms..+5 ms around t=0 (pre = round(0.002*fs)
// taps of "negative time", post = round(0.005*fs) taps of "positive time"),
// and apply 1 ms / 1.6 ms raised-cosine tapers at each end.
//
// Non-causal by construction: taps before index `pre` in the array are the
// response at negative time, i.e. this stage's honest group delay is zero
// (the model was fit that way -- see chain-fit-2026-09-14.md "Method
// changes"). A real-time direct-form convolution can't emit output before
// its input arrives, so this class instead runs an ordinary causal
// convolution and reports `pre` samples of algorithmic latency
// (latencySamples()) for the caller to compensate (tools/render's
// --chain-stage drops the first `pre` output samples of its render, exactly
// mirroring the Python engine's own pre-delay compensation; the plugin adds
// it to the resamplers' latency and lets host PDC handle it).
#pragma once
#include "chain_table.h"
#include <array>
#include <cmath>
#include <vector>

// M_PI is a POSIX/BSD <cmath> extension, not standard C++ -- MSVC doesn't
// define it. Same literal value glibc/libc++ use, so this changes no
// numerics on any platform, on macOS included.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace ax30g {

class ChainStage {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (int ch = 0; ch < 2; ++ch) {
            for (int s = 0; s < 3; ++s) {
                const double fc = kChainLfHpfHz[s];
                const double K = 2.0 * fs_ * std::tan(M_PI * fc / fs_);
                const double c = 2.0 * fs_;
                const double a0 = c + K;
                hpf_[ch][s] = Hpf1{c / a0, -c / a0, (K - c) / a0, 0.0, 0.0};
            }
        }
        buildFir();
        reset();
    }

    // samples of algorithmic latency this stage adds (the FIR's pre-delay)
    int latencySamples() const { return pre_; }

    // debug/verification only (docs/chain-cpp-2026-09-14.md): the built HF
    // FIR taps, same layout as engine/render.py::chain_fir's `h` return.
    const std::vector<double>& firTaps() const { return taps_; }

    void reset() {
        for (int ch = 0; ch < 2; ++ch) {
            for (int s = 0; s < 3; ++s) { hpf_[ch][s].x1 = 0.0; hpf_[ch][s].y1 = 0.0; }
            std::fill(hist_[ch].begin(), hist_[ch].end(), 0.0);
            histPos_[ch] = 0;
        }
    }

    void process(double* l, double* r, int n) {
        for (int i = 0; i < n; ++i) {
            l[i] = processOne(0, l[i]);
            r[i] = processOne(1, r[i]);
        }
    }

private:
    struct Hpf1 { double b0 = 1.0, b1 = -1.0, a1 = 0.0, x1 = 0.0, y1 = 0.0; };

    double processOne(int ch, double x) {
        double v = x;
        for (int s = 0; s < 3; ++s) {
            Hpf1& f = hpf_[ch][s];
            const double y = f.b0 * v + f.b1 * f.x1 - f.a1 * f.y1;
            f.x1 = v;
            f.y1 = y;
            v = y;
        }
        return firSample(ch, v);
    }

    // direct-form circular-buffer convolution: y[n] = sum_k taps_[k] * x[n-k]
    double firSample(int ch, double x) {
        std::vector<double>& h = hist_[ch];
        const int K = int(taps_.size());
        int& pos = histPos_[ch];
        h[size_t(pos)] = x;
        double acc = 0.0;
        int idx = pos;
        for (int k = 0; k < K; ++k) {
            acc += taps_[size_t(k)] * h[size_t(idx)];
            idx = (idx == 0) ? K - 1 : idx - 1;
        }
        pos = (pos + 1 == K) ? 0 : pos + 1;
        return acc;
    }

    // engine/render.py::chain_fir onto this stage's rate.
    void buildFir() {
        constexpr int nfft = 32768;
        const int half = nfft / 2;   // rfft bins 0..half, matches np.fft.rfftfreq(nfft, 1/fs)

        // frequency grid Hre[k]/Him[k], k = 0..half, via linear interpolation
        // of kChainHfRe/kChainHfIm (0 above the table's 22 kHz edge -- matches
        // np.interp(..., right=0.0)); the table's own edge value is already
        // ~0, so the two conventions agree there too.
        std::vector<double> Hre(size_t(half) + 1), Him(size_t(half) + 1);
        const double lastF = double(kChainHfN - 1) * kChainHfStepHz;
        for (int k = 0; k <= half; ++k) {
            const double f = double(k) * fs_ / double(nfft);
            double re, im;
            if (f > lastF) {
                re = 0.0; im = 0.0;
            } else if (f <= 0.0) {
                re = kChainHfRe[0]; im = kChainHfIm[0];
            } else {
                double pos = f / kChainHfStepHz;
                int i0 = int(std::floor(pos));
                if (i0 >= kChainHfN - 1) i0 = kChainHfN - 2;
                const double frac = pos - double(i0);
                re = kChainHfRe[i0] + (kChainHfRe[i0 + 1] - kChainHfRe[i0]) * frac;
                im = kChainHfIm[i0] + (kChainHfIm[i0 + 1] - kChainHfIm[i0]) * frac;
            }
            Hre[size_t(k)] = re;
            Him[size_t(k)] = im;
        }

        pre_ = int(std::lround(0.002 * fs_));
        const int post = int(std::lround(0.005 * fs_));
        taps_.assign(size_t(pre_ + post), 0.0);

        // h[n] = (1/nfft) * sum_{k=0}^{half} c_k * (Hre[k]*cos(2 pi k n/nfft)
        //        - Him[k]*sin(2 pi k n/nfft)), c_0 = c_half = 1, else 2 --
        // the real inverse DFT of a conjugate-symmetric spectrum. Only the
        // ~336 output taps actually kept (n in [nfft-pre, nfft-1] "negative
        // time", and n in [0, post-1] "positive time") are ever evaluated.
        const double twoPiOverN = 2.0 * M_PI / double(nfft);
        auto hAt = [&](int n) {
            double acc = 0.0;
            for (int k = 0; k <= half; ++k) {
                const double ck = (k == 0 || k == half) ? 1.0 : 2.0;
                const double ang = twoPiOverN * double(k) * double(n);
                acc += ck * (Hre[size_t(k)] * std::cos(ang) - Him[size_t(k)] * std::sin(ang));
            }
            return acc / double(nfft);
        };
        for (int i = 0; i < pre_; ++i) taps_[size_t(i)] = hAt(nfft - pre_ + i);
        for (int i = 0; i < post; ++i) taps_[size_t(pre_ + i)] = hAt(i);

        // raised-cosine tapers, matching np.linspace(0, pi, e)-based weights
        // exactly (denominator e-1, not e).
        const int e = int(0.001 * fs_);
        if (e > 1) {
            for (int i = 0; i < e && i < int(taps_.size()); ++i) {
                const double w = 0.5 - 0.5 * std::cos(M_PI * double(i) / double(e - 1));
                taps_[size_t(i)] *= w;
            }
        }
        const int e2 = int(0.0016 * fs_);
        if (e2 > 1) {
            const int K = int(taps_.size());
            for (int i = 0; i < e2 && i < K; ++i) {
                // r2[::-1][i] = 0.5 - 0.5*cos(pi*(e2-1-i)/(e2-1))
                const double w = 0.5 - 0.5 * std::cos(M_PI * double(e2 - 1 - i) / double(e2 - 1));
                taps_[size_t(K - e2 + i)] *= w;
            }
        }

        for (int ch = 0; ch < 2; ++ch) {
            hist_[ch].assign(taps_.size(), 0.0);
            histPos_[ch] = 0;
        }
    }

    double fs_ = 48000.0;
    int pre_ = 0;
    std::array<std::array<Hpf1, 3>, 2> hpf_{};
    std::vector<double> taps_;
    std::vector<double> hist_[2];
    int histPos_[2] = {0, 0};
};

} // namespace ax30g
