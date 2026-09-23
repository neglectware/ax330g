"""Type-2 odd branch: square-wave-times-envelope vs hard clip. Knee on a 0.25 dB grid (-32..-18 dBFS
device) and weight in closed form, fitted on sine20 H1/H3/H5 + sine40 H1/H3 of the six T2 rows jointly
(one knee, one weight per H). Held out: ramp H1 and H3 at 12 levels (-39..-1 dBFS file), every T2 and
T1 row, for the square model, the hard-clip grid fit (driver_fit2.json) and the 09-18 table."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fit_driver as FD, fit_driver2 as FD2
from q import *
ROOT="/path/to/ax30g"; sys.path.insert(0, ROOT)
import engine.hypr as EH
from scipy.signal import stft
xd = FD.xd; FSD = FD.FSD
RAMP = xd[int(30.2*FSD):int(35.2*FSD)]
LV = np.array([-39,-36,-33,-30,-27,-24,-21,-18,-15,-10,-5,-1.5])
def ramp_h(y):
    f,t,Z = stft(y, FSD, nperseg=4096, noverlap=3072, boundary=None, padded=False)
    A = np.abs(Z)*2
    L = -40+40*(t+2048/FSD)/5
    idx = [np.argmin(abs(L-v)) for v in LV]
    out=[]
    for k in (1,3):
        i=int(round(1000*k*4096/FSD)); out.append(20*np.log10(np.max(A[i-2:i+3,:],axis=0)[idx]+1e-15))
    return np.array(out)
def cap_ramp(n):
    r=F[n]['ramp']; t=np.array(r['t']); L=-40+40*(t-30.2+4096/2/48000)/5
    idx=[np.argmin(abs(L-v)) for v in LV]
    return np.array([np.array(r['H1'])[idx], np.array(r['H3'])[idx]])
def sq(xs, knee_db, rel=10.0):
    E = EH._decaying_peak(xs, rel, FSD); k=10**(knee_db/20)
    return FD.post(np.sign(xs)*np.minimum(E,k)/k)
res={}
best=None
for kn in np.arange(-32,-17.99,0.25):
    h20=FD.harm(sq(FD.S20,kn),0.6,5)[[0,2,4]]; h40=FD.harm(sq(FD.S40,kn),0.3,3)[[0,2]]
    m=np.concatenate([h20,h40]); tot=0; ws={}
    for H in FD.HS:
        t20,t40=FD.TARGET[(2,H)]; t=np.concatenate([t20[[0,2,4]],t40[[0,2]]])
        w,e=FD2.best_w(m,t); ws[H]=w; tot+=e**2
    e=np.sqrt(tot/6)
    if best is None or e<best[1]: best=(float(kn),float(e),ws,m)
kn,e,ws,m=best
print("square: knee %.2f dBFS (device), weights %s, fit rms %.2f dB"%(kn,{H:round(v,1) for H,v in ws.items()},e))
d2=json.load(open('driver_fit2.json'))
old=json.load(open(ROOT+'/models/ax30g-hypr.json'))['blocks']['driver']['table']
yr_sq=sq(RAMP,kn)
for T in (1,2):
  for H in FD.HS:
    n=FD.row_name(T,H); c=cap_ramp(n)
    g,w,_,_,_=d2['odd']['%d,%d'%(T,H)]
    hc=ramp_h(FD.post(np.clip(10**(g/20)*RAMP,-1,1)))+w
    def rm(mo):
        live=c>-105; d=(mo-c)[live]; d1=(mo[0]-c[0])
        return np.sqrt(np.mean((d-np.mean(d1))**2)), np.sqrt(np.mean((d1-d1.mean())**2))
    line='T%d H%-2d ramp held-out (H1+H3 rms after one gain / H1-curve shape rms): hardclip %.1f/%.1f'%((T,H)+rm(hc))
    if T==2:
        ms=ramp_h(yr_sq)+ws[H]; line+='  square %.1f/%.1f'%rm(ms)
    res['%d,%d'%(T,H)]=line
    print(line, flush=True)
json.dump({'knee_dbfs':kn,'fit_rms':e,'w_db':ws,'lines':res},open('eval_driver.json','w'),indent=1)
