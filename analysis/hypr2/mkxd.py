import sys, os, json, numpy as np
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # the project root; sys.path.insert(0,ROOT)
from engine.render import load_spec, render_spec
import engine.effects as EF
import engine.hypr
from meas import X, FS
H2=os.path.dirname(os.path.abspath(__file__))
store={}
def cap(xd, spec, fs):
    store['xd']=np.array(xd[:,0] if xd.ndim==2 else xd)
    return np.zeros_like(xd), {}
EF.RENDERERS["HYPR"]=cap
sp=os.path.join(ROOT,'models','ax30g-hypr.json')
for lab,lvl in (('LIN',0.0),('MAX',14.0509)):
    spec=load_spec(sp); spec=json.loads(json.dumps(spec))
    st=dict(spec.get('input_stage') or {}); st['input_level_db']=lvl; spec['input_stage']=st
    render_spec(spec, X, FS, spec_path=sp)
    np.save(os.path.join(H2,'xd_%s.npy'%lab), store['xd'])
    xd=store['xd']; print(lab, len(xd), 20*np.log10(np.max(np.abs(xd))))
