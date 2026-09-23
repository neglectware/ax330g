// Block interface: the unit for one pure-DSP effect in a Chain (dsp/chain.h).
// Per docs/chain-plugin-spec.md "Block interface (C++, header-only in dsp/)".
// Every block runs at the device rate (39,062.5 Hz nominal) on one sample at
// a time, in place, stereo. Values are integers as displayed on the unit's
// own panel; a block clamps out-of-range values itself.
#pragma once

namespace ax30g {

struct BlockInfo { const char* name; int nParams; const char* pname[8]; int pmin[8], pmax[8], pdef[8]; };

class Block {
public:
    virtual ~Block() = default;
    virtual const BlockInfo& info() const = 0;
    virtual void setParam(int i, int v) = 0;     // integer, as displayed on the unit
    virtual void reset() = 0;
    virtual void process(double& l, double& r) = 0;   // one device-rate sample
};

} // namespace ax30g
