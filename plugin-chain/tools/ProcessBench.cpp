// AX330GProcessBench: processBlock() cost with every slot filled (0.12.0 build 25,
// added for the level meters' before/after numbers). Builds the processor straight
// from src/ (no host, no audio device, no editor), fills the eight slots with the
// eight block types in factory order (Stereo Delay .. Compressor), all On, feeds
// a -12 dBFS 440 Hz sine plus a little noise at 48 kHz in 512-sample blocks, and
// times every processBlock() call with the high-resolution counter.
//   AX330GProcessBench [blocks-per-run] [runs]
// Prints per run: mean, median, 99th percentile and worst block in microseconds,
// and the real-time budget share (512 samples at 48 kHz = 10,667 us). Uses only
// the processor's public API, so the same file measures a build before and after
// a change.
#include "../src/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace juce;

int main(int argc, char** argv) {
    ScopedJuceInitialiser_GUI init;
    const int blocksPerRun = argc > 1 ? std::max(100, std::atoi(argv[1])) : 3000;
    const int runs = argc > 2 ? std::max(1, std::atoi(argv[2])) : 5;
    constexpr int kBlock = 512;
    constexpr double kRate = 48000.0;

    AX330GChainProcessor proc;
    proc.setPlayConfigDetails(2, 2, kRate, kBlock);
    proc.prepareToPlay(kRate, kBlock);
    const int nTypes = ax30g::BlockFactory::count() - 1;   // without "Off"
    for (int k = 0; k < ax30g::N_SLOTS; ++k) {
        auto* t = proc.apvts.getParameter("s" + String(k + 1) + "_type");
        t->setValueNotifyingHost(t->convertTo0to1((float) (1 + k % nTypes)));
        proc.apvts.getParameter("s" + String(k + 1) + "_on")->setValueNotifyingHost(1.0f);
    }
    MessageManager::getInstance()->runDispatchLoopUntil(200);   // the Type-defaults timer pushes BlockInfo defaults

    AudioBuffer<float> buf(2, kBlock);
    MidiBuffer midi;
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    double phase = 0.0;
    const float amp = std::pow(10.0f, -12.0f / 20.0f);
    auto fill = [&] {
        for (int i = 0; i < kBlock; ++i) {
            const float x = amp * (float) std::sin(phase) + 0.01f * noise(rng);
            phase += 2.0 * MathConstants<double>::pi * 440.0 / kRate;
            buf.setSample(0, i, x);
            buf.setSample(1, i, x);
        }
    };
    for (int i = 0; i < 200; ++i) { fill(); proc.processBlock(buf, midi); }   // warm-up: allocations, caches

    std::printf("AX330G %s, 8 slots filled (", AX330GChainProcessor::pluginVersionString().toRawUTF8());
    for (int k = 0; k < ax30g::N_SLOTS; ++k)
        std::printf("%s%s", k ? ", " : "", ax30g::BlockFactory::entries()[(size_t) (1 + k % nTypes)].name);
    std::printf("), all on, %d-sample blocks at %.0f Hz\n", kBlock, kRate);
    const double budgetUs = 1.0e6 * kBlock / kRate;
    std::vector<double> means;
    for (int r = 0; r < runs; ++r) {
        std::vector<double> us((size_t) blocksPerRun);
        for (int b = 0; b < blocksPerRun; ++b) {
            fill();
            const double t0 = Time::getMillisecondCounterHiRes();
            proc.processBlock(buf, midi);
            us[(size_t) b] = (Time::getMillisecondCounterHiRes() - t0) * 1000.0;
        }
        double sum = 0.0;
        for (double v : us) sum += v;
        std::sort(us.begin(), us.end());
        const double mean = sum / blocksPerRun;
        means.push_back(mean);
        std::printf("run %d: %d blocks, mean %.1f us, median %.1f us, p99 %.1f us, worst %.1f us (mean %.2f %% of the %.0f us budget)\n",
                    r + 1, blocksPerRun, mean, us[us.size() / 2], us[(size_t) (0.99 * (double) us.size())], us.back(),
                    100.0 * mean / budgetUs, budgetUs);
    }
    std::sort(means.begin(), means.end());
    std::printf("median of run means: %.1f us\n", means[means.size() / 2]);
    return 0;
}
