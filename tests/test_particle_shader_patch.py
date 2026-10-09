#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.203 particle mask patch of the game's own pixel shaders (src/proxy/particle_shader_patch.h) == the Python oracle
patch() in renderer/particle_shader_patch.py, byte for byte and reason for reason, accept and reject alike, on synthetic
ps_2_0/ps_3_0 bytecode: the three blend kinds, the rejection set and a seeded corpus of generated programs. A small
interpreter then runs original and patched programs: RT0 must be identical and oC1 must be the mask value of the kind.
Also the variant cache (build once, remember rejections, forget on address reuse, release). Native clang++ with
ASan/UBSan; no client, game, graphics device or Wine."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import random,struct,subprocess,tempfile,unittest
from particle_shader_patch import patch,dst,src,register,REASONS,SAT

HERE=Path(__file__).resolve().parent
def binary(words):return struct.pack('<%dI'%len(words),*words)

def run_patcher(programs,workdir):
    """C++ patch() over (kind, bytes) programs; returns (reason, patched bytes or None) for each."""
    exe=Path(workdir)/'particle-shader-patch'
    if not exe.exists():
        subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        *fp.test_include_flags(),str(HERE/'test_particle_shader_patch.cpp'),'-o',str(exe)],check=True)
    (Path(workdir)/'in.bin').write_bytes(b''.join(struct.pack('<II',k,len(p)//4)+p for k,p in programs))
    print(subprocess.check_output([str(exe),str(Path(workdir)/'in.bin'),str(Path(workdir)/'out.bin')],text=True),end='')
    data=(Path(workdir)/'out.bin').read_bytes();results=[];offset=0
    while offset<len(data):
        reason,n=struct.unpack_from('<II',data,offset);offset+=8
        results.append((REASONS[reason],data[offset:offset+4*n] if n else None));offset+=4*n
    assert len(results)==len(programs)
    return results

def oracle(code,kind):
    try:return 'ok',patch(code,kind)[0]
    except ValueError as e:return str(e),None
    except (IndexError,struct.error):return 'crash',None   # a malformed program must never crash the oracle either

# ---- a tiny interpreter: mov add mul mad min max dp3 (and cmp/lrp not needed), temporaries, constants, inputs, oC0/oC1; no flow control
def run(words,constants,inputs):
    """Executes a straight-line shader; returns {'oC0':[4 floats],'oC1':[...] or None}."""
    regs={};consts=dict(constants);outs={}
    p=1;major=(words[0]>>8)&255
    def read(t):
        k,i=register(t)
        v=list(consts.get(i,[0.]*4)) if k==2 else list(regs.get((k,i),inputs.get((k,i),[0.]*4)))
        sw=(t>>16)&255;v=[v[(sw>>(2*c))&3] for c in range(4)]
        if ((t>>24)&15)==1:v=[-x for x in v]
        return v
    def write(t,v):
        k,i=register(t);m=(t>>16)&15;sat=bool(t&SAT);cur=list(regs.get((k,i),[0.]*4))
        for c in range(4):
            if m>>c&1:cur[c]=min(1.,max(0.,v[c])) if sat else v[c]
        regs[(k,i)]=cur
    while p<len(words):
        op=words[p]&65535
        if op==65535:break
        if op==65534:p+=((words[p]>>16)&32767)+1;continue
        n=(words[p]>>24)&15;a=words[p+1:p+1+n]
        if op==81:consts[register(a[0])[1]]=[struct.unpack('<f',struct.pack('<I',x))[0] for x in a[1:5]]
        elif op==31:pass
        else:
            f={1:lambda s:s[0],2:lambda s:[x+y for x,y in zip(*s)],5:lambda s:[x*y for x,y in zip(*s)],4:lambda s:[x*y+z for x,y,z in zip(*s)],
               10:lambda s:[min(x,y) for x,y in zip(*s)],11:lambda s:[max(x,y) for x,y in zip(*s)]}[op]
            srcs=[read(t) for t in a[1:]];write(a[0],f(srcs))
        p+=n+1
    return {'oC0':regs.get((8,0)),'oC1':regs.get((8,1))}

def sat(x):return min(1.,max(0.,x))
def expected_mask(c,kind,f=None):
    """oC1 for the kind. With the fog factor: red = (1-f) times the weight; the additive kinds' weights at 1/4. Without: the original blue-only values at full weight (red = the blue value, masked out by the renderer)."""
    m=sat(max(c[0],c[1],c[2]));aware=f is not None and kind<3;r=sat(1-f) if aware else 1.
    if kind==0:return [r,1.,1.,sat(c[3])]
    if kind==1:return [r,1.,1.,sat(c[3])*m*(.25 if aware else 1.)]
    if kind==3:return [m]*4
    m*= .25 if aware else 1.;return [m*r if aware else m,m,m,m]

# ---- synthetic programs
def ps(major,body,dcls=True,fog=None):
    """fog: (input register, write mask, usage index) of a dcl_fog; ps_3_0 only."""
    w=[0xffff0000|(major<<8)]
    if dcls:w+=[0x0200001f,0x80000000,dst(1 if major==3 else 1,0),0x0200001f,0x80000005 if major==3 else 0x80000000,dst(1 if major==3 else 3,1)]
    if fog:w+=[0x0200001f,0x8000000b|(fog[2]<<16),dst(1,fog[0],fog[1])]
    return binary(w+body+[65535])
def ins(op,*t):return [op|(len(t)<<24),*t]
def game_like(major=3):
    # r0 = t * v ; r0.rgb *= 2 ; oC0 = r0 (a particle shader's essence)
    return ps(major,ins(5,dst(0,0),src(1,0),src(1,1))+ins(5,dst(0,0,7),src(0,0),src(0,0))+ins(1,dst(8,0),src(0,0)))

class Synthetic(unittest.TestCase):
    def test_accepts_rejects_match_python(self):
        cases=[]
        for major in (2,3):cases+=[(game_like(major),k) for k in (0,1,2)]
        base=ins(5,dst(0,0),src(1,0),src(1,1))
        base=ins(5,dst(0,0),src(1,0),src(1,1))
        rejects={   # name: (program, kind, the reason)
            'unknown kind':(game_like(),4,'unknown kind'),
            'empty':(b'',0,'empty'),
            'vertex shader':(binary([0xfffe0300,65535]),0,'unsupported shader model'),
            'oC0 half written':(ps(3,base+ins(1,dst(8,0,7),src(0,0))),0,'incomplete output'),
            'oC1 already written':(ps(3,base+ins(1,dst(8,0),src(0,0))+ins(1,dst(8,1),src(0,0))),0,'MRT/depth write'),
            'oDepth written':(ps(3,base+ins(1,dst(8,0),src(0,0))+ins(1,dst(9,0,1),src(0,0))),0,'MRT/depth write'),
            'oC0 inside if':(ps(3,ins(40,src(14,0))+base+ins(1,dst(8,0),src(0,0))+ins(43)),0,'conditional output'),
            'endif without if':(ps(3,ins(43)+base+ins(1,dst(8,0),src(0,0))),0,'unbalanced control flow'),
            'rep':(ps(3,ins(38,src(7,0))+base+ins(1,dst(8,0),src(0,0))+ins(39)),0,'unsupported control/opcode'),
            'predicated':(ps(3,[0x10000000|5|(3<<24),dst(0,0),src(1,0),src(1,1)]+ins(1,dst(8,0),src(0,0))),0,'predicated/coissued'),
            'all 32 temporaries used':(ps(3,sum([ins(1,dst(0,i),src(1,0)) for i in range(32)],[])+ins(1,dst(8,0),src(0,31))),0,'no free temporary'),
            'one temporary left, kind 1 needs two':(ps(3,sum([ins(1,dst(0,i),src(1,0)) for i in range(31)],[])+ins(1,dst(8,0),src(0,30))),1,'no free temporary'),
            'all constants used':(ps(3,sum([ins(1,dst(0,0),src(2,i)) for i in range(224)],[])+ins(1,dst(8,0),src(0,0))),0,'no free constant'),
            'all constants used, additive by colour with the fog factor (the weight scale needs one)':(ps(3,sum([ins(1,dst(0,0),src(2,i)) for i in range(224)],[])+ins(1,dst(8,0),src(0,0)),fog=(2,1,0)),2,'no free constant'),
            'relative constant':(ps(3,ins(1,dst(0,0),src(2,3)|0x2000,src(3,0))+ins(1,dst(8,0),src(0,0))),0,'relative constant'),
        }
        accepts={
            'if/else around other work, oC0 outside':(ps(3,ins(40,src(14,0))+ins(1,dst(0,1),src(1,0))+ins(42)+ins(1,dst(0,1),src(1,1))+ins(43)+base+ins(1,dst(8,0),src(0,0))),0),
            'oC0 in two writes':(ps(3,base+ins(1,dst(8,0,7),src(0,0))+ins(1,dst(8,0,8),src(0,0))),1),
            'highest constant used: another is chosen':(ps(3,ins(1,dst(0,0),src(2,223))+ins(1,dst(8,0),src(0,0))),0),
            'additive by colour without the fog factor needs no constant (the original words)':(ps(3,sum([ins(1,dst(0,0),src(2,i)) for i in range(224)],[])+ins(1,dst(8,0),src(0,0))),2),
            'SRCCOLOR/ONE needs none either':(ps(3,sum([ins(1,dst(0,0),src(2,i)) for i in range(224)],[])+ins(1,dst(8,0),src(0,0)),fog=(2,1,0)),3),
            'comment tokens':(binary([0xffff0300,0xfffe|(2<<16),0x41424344,0x45464748,*ins(5,dst(0,0),src(1,0),src(1,1)),*ins(1,dst(8,0),src(0,0)),65535]),0),
        }
        cases+=[(c,k) for c,k,_ in rejects.values()]+list(accepts.values())
        with tempfile.TemporaryDirectory(prefix='particle-patch-') as tmp:
            results=run_patcher([(k,c) for c,k in cases],tmp)
        for (code,kind),result in zip(cases,results):
            self.assertEqual(result,oracle(code,kind),(code.hex(),kind))
        tail=results[len(cases)-len(rejects)-len(accepts):]
        for (name,(_,_,reason)),result in zip(rejects.items(),tail):self.assertEqual(result[0],reason,name);self.assertIsNone(result[1],name)
        for (name,_),result in zip(accepts.items(),tail[len(rejects):]):self.assertEqual(result[0],'ok',name);self.assertIsNotNone(result[1],name)
        for major in (2,3):   # a patched shader keeps its model; the 1s need a constant (kinds 0, 1), the colour maximum a second temporary (kinds 1, 2)
            for kind in (0,1,2):
                out,info=patch(game_like(major),kind)
                self.assertEqual(struct.unpack('<I',out[:4])[0],0xffff0000|(major<<8));self.assertEqual(info['model'],major)
                self.assertEqual(info['const'] is not None,kind<2);self.assertEqual(info['temp2'] is not None,kind>0);self.assertEqual(info['scale'],1.0)

# ---- generated corpus
def generated(rng):
    major=rng.choice((2,3));w=[0xffff0000|(major<<8)]
    if rng.random()<.02:w[0]=rng.choice((0xfffe0300,0xffff0101,0xffff0400))
    def i(op,*tokens,flags=0):w.extend([op|(len(tokens)<<24)|flags,*tokens])
    temps=32 if major==3 else 12
    if rng.random()<.3:w.extend([0xfffe|(2<<16),0x42415443,rng.getrandbits(32)])
    for _ in range(rng.randrange(4)):i(31,0x80000000|rng.choice((5,10,0,11,11))|(rng.choice((0,0,1))<<16),dst(rng.choice((1,3,10)),rng.randrange(4),rng.choice((15,15,1,2,4,8))))
    if rng.random()<.5:i(81,dst(2,rng.randrange(8)),*[rng.getrandbits(32) for _ in range(4)])
    def operand(first):
        if first:
            kind,index=rng.choice([(0,rng.randrange(temps))]*4+[(8,0)]+([(8,1),(9,0),(8,0)] if rng.random()<.1 else []))
            t=dst(kind,index,rng.choice((15,15,15,7,8,1)))
            return [t|0x2000,src(3,0)] if rng.random()<.01 else [t]
        kind=rng.choice((0,0,1,2));t=src(kind,rng.randrange(temps if kind==0 else 8 if kind==1 else 224),rng.choice((0xe4,0xff,0x00,0x55)))
        if rng.random()<.04:t=[t|0x2000,src(rng.choice((3,15,15,0)),0)]
        else:t=[t]
        return t
    ops={1:2,2:3,4:4,5:3,8:3,10:3,11:3,19:2,20:3,21:3,22:3,23:3,24:3,6:2,7:2,66:3,65:1,15:2}
    depth=0
    for _ in range(rng.randrange(1,10)):
        r=rng.random()
        if r<.05 and depth<2:i(40,src(14,0));depth+=1;continue
        if r<.09 and depth:i(43);depth-=1;continue
        if r<.10:i(rng.choice((42,27,38,28,30)),src(14,0));continue
        op=rng.choice(list(ops));n=ops[op];tokens=[]
        for k in range(n):tokens+=operand(k==0)
        if rng.random()<.03:tokens.append(src(0,0))
        i(op,*tokens,flags=0x10000000 if rng.random()<.02 else 0)
    for _ in range(depth):
        if rng.random()<.85:i(43)
    if rng.random()<.9:i(1,dst(8,0,rng.choice((15,15,15,3))),src(0,rng.randrange(temps)))
    if rng.random()<.2:i(0)
    w.append(0xffff)
    if rng.random()<.03:w=w[:-rng.randrange(1,4)]
    return binary(w)

class Fog(unittest.TestCase):
    def test_fog_input_lookup_and_writes(self):
        body=ins(5,dst(0,0),src(1,0),src(1,1))+ins(1,dst(8,0),src(0,0))
        cases=[(ps(3,body,fog=(2,1,0)),k) for k in (0,1,2,3)]+[(ps(3,body,fog=(5,2,0)),0),(ps(3,body,fog=(7,8,0)),2),(ps(3,body,fog=(2,1,1)),0),(ps(3,body),0),(ps(2,body),0)]
        with tempfile.TemporaryDirectory(prefix='particle-patch-') as tmp:
            results=run_patcher([(k,c) for c,k in cases],tmp)
        for (code,kind),result in zip(cases,results):self.assertEqual(result,oracle(code,kind),(kind,code.hex()))
        info=lambda code,kind:patch(code,kind)[1]['fog']
        self.assertEqual([info(ps(3,body,fog=(2,1,0)),k) for k in (0,1,2,3)],[True,True,True,False])   # SRCCOLOR/ONE writes no f
        self.assertTrue(info(ps(3,body,fog=(5,2,0)),0));self.assertTrue(info(ps(3,body,fog=(7,8,0)),2))     # any input register, any component (.y, .w)
        self.assertFalse(info(ps(3,body,fog=(2,1,1)),0))     # FOG usage index 1 is not the game's fog factor
        self.assertFalse(info(ps(3,body),0));self.assertFalse(info(ps(2,body),0))   # no dcl_fog, and ps_2_0 has no fog input
        # the written words: oC1.x reads the input's component, saturated; without fog the old words (xyz = the constant) are byte-identical
        plain=patch(ps(3,body),0)[0];fogged=patch(ps(3,body,fog=(5,2,0)),0)[0]
        self.assertGreater(len(fogged),len(plain)+12)
        self.assertEqual(struct.pack('<I',dst(8,1,1)|SAT) in fogged,True);self.assertEqual(struct.pack('<I',src(1,5,0x55)|(1<<24)) in fogged,True)   # -f.y

class Corpus(unittest.TestCase):
    def test_generated_corpus_matches_python(self):
        rng=random.Random(203);programs=[(rng.randrange(4),generated(rng)) for _ in range(6000)]
        with tempfile.TemporaryDirectory(prefix='particle-patch-') as tmp:
            results=run_patcher(programs,tmp)
        accepted=0;reasons={}
        for n,((kind,code),result) in enumerate(zip(programs,results)):
            expected=oracle(code,kind)
            self.assertEqual(result,expected,'program %d kind %d: %s'%(n,kind,code.hex()))
            accepted+=expected[0]=='ok';reasons[expected[0]]=reasons.get(expected[0],0)+1
        print('generated: %d programs, %d accepted; %s'%(len(programs),accepted,sorted(reasons.items(),key=lambda x:-x[1])))
        self.assertGreater(accepted,400);self.assertGreater(len(programs)-accepted,400);self.assertNotIn('crash',reasons)

# ---- semantics
def straight_line(rng,major):
    """A straight-line colour shader: inputs v0 (diffuse) t0/v1 (a second input), constants, a few arithmetic ops, oC0 written whole (sometimes in two parts)."""
    w=[0xffff0000|(major<<8)]
    def i(op,*t):w.extend([op|(len(t)<<24),*t])
    fog=None
    if major==3 and rng.random()<.6:
        fog=(rng.randrange(2,6),rng.choice((1,2,4,8,3,15)));w+=[0x0200001f,0x8000000b,dst(1,fog[0],fog[1])]
    used_c=rng.sample(range(0,8),3)
    for c in used_c:i(81,dst(2,c),*[struct.unpack('<I',struct.pack('<f',rng.uniform(-1,2)))[0] for _ in range(4)])
    srcs=[(1,0),(1,1)]+[(2,c) for c in used_c]
    temps=[]
    for _ in range(rng.randrange(2,6)):
        op=rng.choice((1,2,4,5,10,11));n={1:1,2:2,4:3,5:2,10:2,11:2}[op];t=rng.randrange(0,4)
        ss=[src(*rng.choice(srcs+[(0,x) for x in temps]),swizzle=rng.choice((0xe4,0xff,0x00,0x55,0x1b))) for _ in range(n)]
        if rng.random()<.2:ss[0]|=1<<24
        i(op,dst(0,t,rng.choice((15,15,7))|0)|(SAT if rng.random()<.3 else 0),*ss);temps.append(t)
    last=temps[-1] if temps else None
    if last is None:i(1,dst(0,0),src(1,0));last=0
    if rng.random()<.3:i(1,dst(8,0,7),src(0,last));i(1,dst(8,0,8),src(0,last,0xff))
    else:i(1,dst(8,0)|(SAT if rng.random()<.5 else 0),src(0,last))
    w.append(65535)
    return w,fog

class Semantics(unittest.TestCase):
    def test_rt0_identical_and_mask_values(self):
        rng=random.Random(2031);checked=0
        for _ in range(1500):
            major=rng.choice((2,3));w,fog=straight_line(rng,major);code=binary(w)
            inputs={(1,0):[rng.uniform(-.5,1.5) for _ in range(4)],(1,1):[rng.uniform(-.5,1.5) for _ in range(4)]}
            f=None
            if fog:
                inputs[(1,fog[0])]=[rng.uniform(-.5,1.5) for _ in range(4)];f=inputs[(1,fog[0])][(fog[1]&-fog[1]).bit_length()-1]
            try:before=run(w,{},inputs)
            except (KeyError,TypeError):continue
            if before['oC0'] is None:continue
            for kind in (0,1,2,3):
                try:patched=patch(code,kind)[0]
                except ValueError:continue
                pw=list(struct.unpack('<%dI'%(len(patched)//4),patched))
                after=run(pw,{},inputs)
                self.assertEqual(after['oC0'],before['oC0'],'kind %d: RT0 differs: %s'%(kind,code.hex()))
                want=expected_mask(before['oC0'],kind,f if kind<3 else None)
                for g,e in zip(after['oC1'],want):
                    if e is not None:self.assertAlmostEqual(g,e,places=6,msg='kind %d: %s'%(kind,code.hex()))
                if kind<2:self.assertEqual(after['oC1'][1:3],[1.,1.])
                self.assertEqual(patch(code,kind)[1]['fog'],fog is not None and kind<3)
                if fog is None:self.assertAlmostEqual(after['oC1'][0],want[0] if want[0] is not None else after['oC1'][0],places=6)   # no fog input: red = blue's value (f = 1)
                checked+=1
        print('semantics: %d patched programs executed'%checked)
        self.assertGreater(checked,1000)
if __name__=='__main__':unittest.main()
