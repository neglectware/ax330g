"""Python reference renderer for AX30G model specs (JSON in models/).

Runs the effect at the device sample rate (39062.5 Hz by default) with
resampling at the input and output so that a 48 kHz signal set goes in and a
48 kHz "capture" comes out. This is both the synthetic device for the analysis
harness and, later, the bit-level reference the JUCE runtime must match.
"""
from .render import load_spec, render_spec, render_file  # noqa: F401
