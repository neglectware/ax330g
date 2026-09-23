import json, re, numpy as np, os
H2=os.path.dirname(os.path.abspath(__file__))
F=json.load(open(os.path.join(H2,'features.json')))
D=json.load(open(os.path.join(H2,'cache','diag.json')))
def P(n):
    o={}
    for k in ('Type','Harmonics','Sensitivity','Depth','Decay','Resonance','DirectLevel','EffectLevel'):
        m=re.search(k+r'-(\d+)',n); 
        if m: o[k]=int(m.group(1))
    m=re.search('Polarity-(UP|DOWN)',n); o['Pol']=m.group(1) if m else None
    o['in']='MAX' if 'IN-MAX' in n else 'LIN'; o['take2']='_take2' in n
    o['new']= D[n]['mtime']>1790000000 if n in D else None
    return o
def find(**kw):
    out=[]
    for n in F:
        if not n.startswith('AX30G_HYPR'): continue
        p=P(n)
        if all(p.get(k)==v for k,v in kw.items()): out.append(n)
    return sorted(out, key=lambda n: D[n]['mtime'])
def short(n):
    p=P(n); return 'T%s H%s S%s %s D%s Dec%s R%s Dir%s Eff%s %s%s%s'%(p['Type'],p['Harmonics'],p['Sensitivity'],p['Pol'],p['Depth'],p['Decay'],p['Resonance'],p['DirectLevel'],p['EffectLevel'],p['in'],' t2' if p['take2'] else '',' NEW' if p['new'] else ' old')
