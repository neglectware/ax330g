#include <cstdlib>
// AX330GLcdSnapshot: a headless verification tool for axlcd::LcdDisplay
// (docs/lcd-startup-2026-09-22.md). Drives the exact same boot sequence the
// plugin editor plays, renders it at a handful of caller-chosen times, and
// writes each one as a PNG -- so the start-up animation can be reviewed
// frame-by-frame without opening a DAW.
//
// Usage: AX330GLcdSnapshot <outdir> <width> <height> t1 t2 ...
//
// Not part of the AX330G plugin target (see plugin-chain/CMakeLists.txt) --
// this is a juce_add_console_app of its own, sharing only LcdDisplay.cpp.
#include "../src/lcd/LcdDisplay.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

using namespace juce;

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "usage: AX330GLcdSnapshot <outdir> <width> <height> t1 t2 ...\n";
        return 1;
    }

    // Needed for Image/Graphics/PNGImageFormat and Component::paint() to
    // work at all outside a real app -- see juce_Initialisation.h.
    ScopedJuceInitialiser_GUI juceInit;

    const File outDir{String(argv[1])};
    outDir.createDirectory();
    const int width = String(argv[2]).getIntValue();
    const int height = String(argv[3]).getIntValue();

    // Pair each requested time with the ORIGINAL command-line text (not a
    // reformatted number) so a filename like "lcd_4.20.png" comes out
    // exactly as asked for, even though 4.20 and 4.2 are the same double --
    // then sort by the parsed value, since tick() must be fed non-decreasing
    // times.
    std::vector<std::pair<double, String>> times;
    for (int i = 4; i < argc; ++i)
        times.emplace_back(String(argv[i]).getDoubleValue(), String(argv[i]));
    std::stable_sort(times.begin(), times.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

    axlcd::LcdDisplay lcd;
    lcd.setSize(width, height);
    // LCD_LINE1 / LCD_CHAIN override the play page (for design mockups).
    const char* l1 = std::getenv("LCD_LINE1");
    const char* ch = std::getenv("LCD_CHAIN");
    lcd.setPlayPage(l1 ? l1 : "--- INIT", ch ? ch : "comp-DST1-3BEQ-asim-SMOD-rev");
    lcd.startBootSequence(0.0);

    double t = 0.0;
    constexpr double kStep = 1.0 / 240.0;

    for (const auto& [ti, label] : times) {
        while (t < ti - 1.0e-9) {
            t = jmin(t + kStep, ti);
            lcd.tick(t);
        }

        const Image img = lcd.createComponentSnapshot(lcd.getLocalBounds(), true, 1.0f);
        const File outFile = outDir.getChildFile("lcd_" + label + ".png");
        outFile.deleteFile();
        if (auto stream = std::unique_ptr<FileOutputStream>(outFile.createOutputStream())) {
            PNGImageFormat png;
            png.writeImageToStream(img, *stream);
            std::cout << outFile.getFullPathName() << "\n";
        } else {
            std::cerr << "failed to open " << outFile.getFullPathName() << " for writing\n";
            return 1;
        }
    }

    return 0;
}
