import numpy as np
from ctrl import *
xd=np.load('xd_LIN.npy'); fs=FSD
def dbv(v): return 20*np.log10(max(v,1e-12))
notes=[47.90,48.81,49.71,50.61,51.50,52.41,53.31]
for tau in (5.0,20.0,50.0):
    E=detector(xd,tau)
    print('tau_det %.0f ms'%tau)
    for i,t0 in enumerate(notes):
        a=int((t0-0.05)*fs); b=int((t0+0.85)*fs)
        seg=E[a:b]; k=np.argmax(seg)
        # after the peak: the dB trajectory sampled
        pk=dbv(seg[k]); tpk=(a+k)/fs-t0
        # secondary rises: local maxima after the first 20 ms that rise > 1 dB above the running min since peak
        s=20*np.log10(np.maximum(seg[k:],1e-12)); runmin=np.minimum.accumulate(s); rise=s-runmin
        j=np.argmax(rise); 
        print('  note %d t=%.2f peak %.1f dB at +%.0f ms; biggest re-rise %.1f dB at +%.0f ms (from %.1f); level at +100/200/400 ms: %.1f %.1f %.1f; min before next note %.1f'%(i+1,t0,pk,1000*tpk,rise[j],1000*(tpk+j/fs),runmin[j],s[int(0.1*fs)] if len(s)>0.1*fs else 0,s[int(0.2*fs)],s[int(0.4*fs)], s.min()))
# other stimuli peaks
E=detector(xd,20.0)
for lab,a,b in [('clicks',1.9,10.6),('bursts',11.5,20.2),('sine20',21.1,25.3),('sine40',26.6,28.8),('noise',43.1,46.3)]:
    s=E[int(a*fs):int(b*fs)]; print(lab,'peak %.1f'%dbv(s.max()))
r=E[int(30.2*fs):int(35.2*fs)]; 
for L in (-21.5,-20.7,-20.0,-19.0): 
    k=np.argmax(20*np.log10(r+1e-12)>L+0); 
print('ramp E at trigger time 32.617s: %.2f dB'%dbv(E[int(32.617*fs)]))
for t in (32.60,32.61,32.615,32.62): print(t, '%.2f'%dbv(E[int(t*fs)]))
