"""In-process CPU guard: call quiet() often; it returns at once unless a capture file was modified
in the last 90 s, in which case it sleeps until 90 s pass with no new write. Checks the directory at
most every 3 s."""
import os, time
D = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "captures")
_last = [0.0]
def newest():
    m = 0.0
    with os.scandir(D) as it:
        for e in it:
            try:
                m = max(m, e.stat().st_mtime)
            except OSError:
                pass
    return m
def quiet():
    now = time.time()
    if now - _last[0] < 3.0:
        return
    _last[0] = now
    waited = 0
    while True:
        age = time.time() - newest()
        if age >= 90:
            break
        time.sleep(min(91 - age, 30))
        waited += 1
    if waited:
        _last[0] = time.time()
