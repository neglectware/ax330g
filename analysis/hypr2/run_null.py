"""tests/hypr_null.py, single process, with the capture guard before every row."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the project root; sys.path.insert(0, ROOT)
import pyguard, tests.hypr_null as HN
_orig = HN.row_for
def guarded(job):
    pyguard._last[0] = 0.0
    pyguard.quiet()
    r = _orig(job)
    print('.', end='', file=sys.stderr, flush=True)
    return r
HN.row_for = guarded
sys.argv = ["hypr_null.py", "--jobs", "1"] + sys.argv[1:]
HN.main()
