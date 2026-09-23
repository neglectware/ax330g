from q import *
from meas import load, cut, FS
import numpy as np
from scipy.signal import welch
def psd(n,t0,t1):
    c=cut(load(n),FS,t0,t1); f,Pp=welch(c,FS,nperseg=8192); return f,10*np.log10(Pp+1e-30)
ref=find(Type=2,Harmonics=50,Resonance=25,Depth=0,DirectLevel=0)[0]
f,R=psd(ref,0.2,1.9)
ref0=find(Type=2,Harmonics=50,Resonance=0,Depth=0,DirectLevel=0,EffectLevel=50,take2=True,**{'in':'LIN'})[0]
_,R0=psd(ref0,0.2,1.9)
fc=[100,200,300,389,500,700,1000,1500,2000,3000,4000,5000,6000,8000,10000,12000,14000,16000]
def at(P): return [np.mean(P[(f>c*0.94)&(f<c*1.06)]) for c in fc]
print('Hz       ', ' '.join('%6d'%c for c in fc))
print('R25-R0   ', ' '.join('%6.1f'%v for v in np.array(at(R))-np.array(at(R0))))
for n in find(Type=2,Harmonics=50,Resonance=25,Pol='DOWN')+find(Type=2,Harmonics=50,Decay=50,Depth=50):
    for (a,b,lab) in [(0.2,1.9,'silence'),(20.3,21.1,'preS20'),(29.3,30.1,'preRamp'),(35.4,36.6,'postRamp'),(42.0,43.1,'preNoise'),(46.4,47.6,'preDI')]:
        _,P=psd(n,a,b)
        print('%-8s %s'%(lab,short(n)[:30]), ' '.join('%6.1f'%v for v in np.array(at(P))-np.array(at(R0))))
