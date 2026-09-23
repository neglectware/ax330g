"""Building blocks. Every block is a plain Python object with a per-sample
call; speed is not the point here, explicitness is. Each numeric block can
carry a wordlength/rounding so fixed-point behaviour is a parameter, not an
accident.
"""
import math

LN1P_MU = math.log(256.0)


def quantize(v, wordlength, rounding="truncate", overflow="saturate"):
    """Quantize a value in [-1, 1) to a signed fixed-point word."""
    if not wordlength:
        return v
    scale = float(1 << (wordlength - 1))
    q = v * scale
    if rounding == "round":
        q = math.floor(q + 0.5)
    else:  # truncate toward -inf, what a shift right does
        q = math.floor(q)
    hi = scale - 1.0
    lo = -scale
    if q > hi or q < lo:
        if overflow == "wrap":
            span = 2.0 * scale
            q = ((q - lo) % span) + lo
        else:
            q = hi if q > hi else lo
    return q / scale


def mulaw_encode(x, mu=255.0):
    s = -1.0 if x < 0 else 1.0
    a = abs(x)
    if a > 1.0:
        a = 1.0
    return s * math.log1p(mu * a) / LN1P_MU


def mulaw_decode(y, mu=255.0):
    s = -1.0 if y < 0 else 1.0
    return s * (math.expm1(abs(y) * LN1P_MU)) / mu


class Storage:
    """Delay-memory storage format: word size, companding, and a rate divider.

    rate_div N means the line is written once every N device samples (the
    prefilter decides what gets written: the last sample or the mean of N) and
    read back by hold or linear interpolation between stored frames.
    """

    def __init__(self, cfg):
        cfg = cfg or {}
        self.bits = cfg.get("bits", 16)
        self.compand = cfg.get("compand", "none")
        self.rate_div = int(cfg.get("rate_div", 1))
        self.prefilter = cfg.get("prefilter", "none")
        self.upsample = cfg.get("upsample", "hold")
        self.rounding = cfg.get("rounding", "round")

    def encode(self, x):
        if self.compand == "mu":
            return quantize(mulaw_encode(x), self.bits, self.rounding)
        return quantize(x, self.bits, self.rounding)

    def decode(self, y):
        if self.compand == "mu":
            return mulaw_decode(y)
        return y


class DelayLine:
    """Circular delay line with a Storage model.

    write(x) is called once per device sample. read(delay_samples) returns the
    signal delay_samples ago; delay must be >= 2*rate_div samples.
    """

    def __init__(self, max_samples, storage, interp="none"):
        self.st = storage
        rd = self.st.rate_div
        self.rd = rd
        self.n = int(max_samples // rd) + 8
        self.buf = [0.0] * self.n
        self.frames = 0          # completed decimated frames
        self.count = 0           # device samples written
        self.phase = 0
        self.acc = 0.0
        self.interp = interp

    def write(self, x):
        rd = self.rd
        if rd == 1:
            self.buf[self.frames % self.n] = self.st.encode(x)
            self.frames += 1
        else:
            if self.st.prefilter == "avg":
                self.acc += x
            self.phase += 1
            if self.phase == rd:
                v = self.acc / rd if self.st.prefilter == "avg" else x
                self.buf[self.frames % self.n] = self.st.encode(v)
                self.frames += 1
                self.phase = 0
                self.acc = 0.0
        self.count += 1

    def read(self, delay):
        if self.interp == "none":
            delay = float(int(delay + 0.5))
        # position in decimated frames, relative to the sample about to be written
        pos = (self.count - delay) / self.rd
        i = int(math.floor(pos))
        frac = pos - i
        n = self.n
        st = self.st
        a = st.decode(self.buf[i % n])
        if self.st.upsample == "linear" or (self.rd == 1 and self.interp == "linear"):
            b = st.decode(self.buf[(i + 1) % n])
            return a + (b - a) * frac
        return a


class OnePoleLP:
    def __init__(self, fs, cutoff_hz=None, wordlength=None, rounding="truncate"):
        self.y = 0.0
        self.wl = wordlength
        self.rounding = rounding
        self.set_cutoff(fs, cutoff_hz)

    def set_cutoff(self, fs, cutoff_hz):
        if cutoff_hz is None or cutoff_hz >= fs / 2:
            self.a = 1.0
        else:
            self.a = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / fs)

    def __call__(self, x):
        if self.a >= 1.0:
            return x
        self.y += self.a * (x - self.y)
        if self.wl:
            self.y = quantize(self.y, self.wl, self.rounding)
        return self.y


class Gain:
    def __init__(self, g, wordlength=None, rounding="truncate", overflow="saturate"):
        self.g = g
        self.wl = wordlength
        self.rounding = rounding
        self.overflow = overflow

    def __call__(self, x):
        v = self.g * x
        if self.wl:
            v = quantize(v, self.wl, self.rounding, self.overflow)
        return v


class LFO:
    """Triangle, sine, or measured-table LFO, optionally updated only every
    `update_every` samples (control-rate, zero-order hold in between).
    Output in [-1, 1].

    shape="table" reads a table of N points in [0, 1] (linear interpolation
    between points, phase in cycles; table[0] is the LFO's minimum, i.e. the
    nominal/undeflected delay, per the AX30G's unipolar-above modulation),
    and returns 2*v - 1 so it plugs into the same base + depth*lfo() formula
    as tri/sin without changing the caller."""

    def __init__(self, fs, rate_hz, shape="tri", update_every=1, phase=0.0, table=None):
        self.fs = fs
        self.rate = rate_hz
        self.shape = shape
        self.upd = max(1, int(update_every))
        self.ph = phase % 1.0
        self.inc = rate_hz / fs
        self.k = 0
        self.table = list(table) if table else None
        self.held = self._value()

    def _value(self):
        p = self.ph
        if self.shape == "sin":
            return math.sin(2.0 * math.pi * p)
        if self.shape == "table":
            tbl = self.table
            n = len(tbl)
            pos = p * n
            i0 = int(math.floor(pos)) % n
            i1 = (i0 + 1) % n
            frac = pos - math.floor(pos)
            v = tbl[i0] + (tbl[i1] - tbl[i0]) * frac
            return 2.0 * v - 1.0
        # triangle: -1 at 0, +1 at 0.5, -1 at 1
        return 4.0 * abs(p - 0.5) * -1.0 + 1.0

    def __call__(self):
        if self.k == 0:
            self.held = self._value()
        self.k += 1
        if self.k >= self.upd:
            self.k = 0
        self.ph += self.inc
        if self.ph >= 1.0:
            self.ph -= 1.0
        return self.held
