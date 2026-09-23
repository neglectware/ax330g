import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fctrack as FT
from fctrack import *
FT.NPER, FT.HOP = 512, 64
ref = ref_row()
out={}
for (a,b,lab) in [(21.0,22.6,'s20'),(43.0,44.6,'noise'),(32.4,33.9,'ramp'),(47.7,49.5,'di1')]:
    yr=cut(load(ref),FS,a,b)
    t,Br=FT.band_spec(yr)
    out[lab]={'t':(t+a).tolist()}
    for n in find(Type=2, Harmonics=50, Resonance=25, DirectLevel=0):
        _,Bn=FT.band_spec(cut(load(n),FS,a,b))
        fc,err,nl=FT.track(Bn,Br,margin=10.0)
        out[lab][n]={'fc':fc.tolist(),'err':err.tolist()}
json.dump(out,open(os.path.join(H2,'fine.json'),'w'))
print('done')
