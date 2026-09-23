CXX ?= clang++
CXXFLAGS ?= -O2 -std=c++17 -Wall
ax30g-render: main.cpp $(wildcard ../../dsp/*.h) ../../dsp/ax30g_sdly.h ../../dsp/resampler.h ../../dsp/wav.h ../../dsp/chain.h ../../dsp/chain_stage.h ../../dsp/chain_table.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp
