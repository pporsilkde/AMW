#!/usr/bin/env python3

def recommend(vendor='unknown', software=False, cores=8, vram=0, mp100=207, model=''):
    if software or vendor == 'microsoft':
        return 0
    level = 1 if vendor == 'unknown' and cores <= 4 else 2
    if vendor == 'intel':
        if 'arc' in model or vram >= 4096: level = 4 if cores >= 8 else 3
        elif 'iris xe' in model or vram >= 1536: level = 2
        else: level = 1
    elif vendor == 'apple':
        if 'm4' in model or 'm3 max' in model or 'm2 max' in model: level = 5
        elif 'm3' in model or 'm2 pro' in model or 'm1 max' in model: level = 4
        else: level = 3
    elif vram > 0:
        if vram >= 12288 and cores >= 8: level = 5
        elif vram >= 8192 and cores >= 6: level = 4
        elif vram >= 6144: level = 4
        elif vram >= 4096: level = 3
        elif vram >= 2048: level = 2
        else: level = 1
    if cores <= 2: level = min(level,1)
    elif cores <= 4: level = min(level,3)
    if mp100 >= 800 and vram < 8192 and vendor != 'apple': level=max(0,level-1)
    return max(0,min(5,level))

def auto_level(auto_select, recommended):
    return recommended if auto_select else 2

def preset_shadows(level):
    enabled = level >= 1
    return dict(master=enabled, player=enabled, actor=enabled,
                object=level>=3, terrain=level>=4, indoor=level>=2,
                distance=min(16384,[4096,6144,8192,10240,12288,16384][level]))

assert recommend(software=True)==0
assert recommend(vendor='unknown', software=False, cores=8, mp100=207)==2
assert recommend(vendor='unknown', software=False, cores=4, mp100=207)==1
assert recommend(vendor='unknown', software=False, cores=8, mp100=829)==1
assert auto_level(True,4)==4
assert auto_level(False,4)==2
expected=[
    (False,False,False,False,False),
    (True,True,False,False,False),
    (True,True,False,False,True),
    (True,True,True,False,True),
    (True,True,True,True,True),
    (True,True,True,True,True),
]
for level,e in enumerate(expected):
    s=preset_shadows(level)
    got=(s['master'],s['actor'],s['object'],s['terrain'],s['indoor'])
    assert got==e,(level,got,e)
    assert s['distance']<=16384
print('X032 launcher preset harness: PASS')
