"""Differential measurements: two captures that differ in one parameter.
The ratio of their loop responses isolates that parameter's block, whatever
the storage does on every pass.
"""
import numpy as np
from .clicks import fit_loop


def damp_from_reference(cl, cl_ref, key=None):
    """High Damp: G2(damp)/G2(damp=0) = H_damp(f). Fit a one-pole to it."""
    # With the filter inside the loop the first repeat already carries one
    # pass of it, and R1 has the best SNR, so prefer G1(damp)/G1(damp=0);
    # fall back to the G2 ratio (needed when the store shapes G1).
    # G1 is the better source when the spectra come from the sweep IR (real
    # unit); with click windows (harness's companded / sub-rate stores) the
    # G2 ratio is the validated choice.
    if key is None:
        from_ir = cl.get("spectra_source") == "sweep_ir" and cl_ref.get("spectra_source") == "sweep_ir"
        key = "g1_mean" if (from_ir and "g1_mean" in cl and "g1_mean" in cl_ref) else "g2_mean"
    if key not in cl or key not in cl_ref:
        return {"error": "missing loop spectra"}
    f = np.array(cl["bands_hz"])
    r = np.array(cl[key]) / np.array(cl_ref[key])
    mask = np.array(cl_ref.get("shape_mask", [True] * len(f)))
    g, fc, err = fit_loop(f, r, mask=mask)
    return {"damp_hz": fc, "gain": g, "err_db": err, "from": key, "ratio_db": (20 * np.log10(np.maximum(r, 1e-9))).tolist(),
            "bands_hz": f.tolist()}
