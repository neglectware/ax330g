import numpy as np
from ctrl import *
xd=np.load('xd_LIN.npy'); fs=FSD
notes=[47.90,48.81,49.71,50.61,51.50,52.41,53.31,54.2]
Ton=-22.3
for tau in (1.0,3.0,5.0,10.0,20.0,50.0):
    E=20*np.log10(np.maximum(detector(xd,tau),1e-12))
    out=[]
    for i in range(7):
        a=int((notes[i]-0.05)*fs); b=int((notes[i+1]-0.05)*fs)
        s=E[a:b]
        above=s>=Ton
        k=np.argmax(above)  # first crossing
        # subsequent re-crossings: segments where above goes False then True
        res=[]; j=k
        while True:
            nb=np.flatnonzero(~above[j:]); 
            if not len(nb): break
            j0=j+nb[0]
            na=np.flatnonzero(above[j0:])
            if not len(na): 
                res.append(('end',round(s[j0:].min()-Ton,1))); break
            j1=j0+na[0]
            res.append((round(1000*(j1-k)/fs), round(s[j0:j1].min()-Ton,1)))
            j=j1
        out.append(res)
    print('tau %4.1f: '%tau + ' | '.join('n%d %s'%(i+1,r) for i,r in enumerate(out)))
