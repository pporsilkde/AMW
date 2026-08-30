PRESETS = [
    ('Minimum','simple',256,False,2,4.0,1,32.0),
    ('Low','simple',256,False,3,4.0,2,28.0),
    ('Balanced','new',512,True,3,3.0,4,20.0),
    ('Medium','new',512,True,4,3.0,4,18.0),
    ('High','new',1024,True,5,2.0,6,14.0),
    ('Ultra','new',2048,True,5,1.0,8,10.0),
]
for name,mode,rtt,refract,refl,scale,ripples,cull in PRESETS:
    assert mode in ('simple','new')
    assert rtt in (256,512,1024,2048)
    assert 0 <= refl <= 5 and scale in (1.0,2.0,3.0,4.0)
    assert 0 <= ripples <= 16 and 0 <= cull <= 64
assert [p[1] for p in PRESETS[:2]] == ['simple','simple']
assert all(p[1]=='new' for p in PRESETS[2:])
assert [p[2] for p in PRESETS] == [256,256,512,512,1024,2048]
CHAT = ['visible','transparent30','transparent60','autohide','hidden']
assert CHAT[(CHAT.index('hidden')+1)%len(CHAT)] == 'visible'
print('X033 logic: OK')
