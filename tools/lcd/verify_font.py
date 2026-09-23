"""Checks the A00 table against the dot states read from the real-time
start-up video (60 fps). Argument: a .npy of per-frame dot darkness,
shape [frame, cell, row, dotrow, dotcol], from the grid sampler.
    tools/lcd/verify_font.py dots.npy"""
import numpy as np,sys
sys.path.insert(0,'tools/lcd'); from a00_font import A00
v=np.load(sys.argv[1])>55   # [n,c,r,i,j]
screens={30:("   TONEWORKS    ","     GUITAR     "),100:("   TONEWORKS    "," HYPERFORMANCE  "),
         162:("   TONEWORKS    ","   PROCESSOR    "),220:("   TONEWORKS    ","     AX300G     "),
         320:("A11 MODEL REF   ","dst2-cho -SMOD-s")}
bad=0
for n,lines in screens.items():
  for r,line in enumerate(lines):
    for c,ch in enumerate(line):
      g=A00[ord(ch)]+[0]
      exp=np.array([[(g[i]>>(4-j))&1 for j in range(5)] for i in range(8)],bool)
      d=(exp!=v[n,c,r]).sum()
      if d: bad+=1; print(f"frame {n} row {r} col {c} {ch!r}: {d} dots differ")
print("cells checked:",sum(32 for _ in screens),"mismatching cells:",bad)
