"""Bounded, time-averaged WORLD flame position; never changes native energy.

The original native point remains the ownership proof. Only a coincident,
continuous additive emitter on a proven fixed nozzle can supply a mean offset.
Metal bowls, logs and frames remain ordinary occluders. This is a steady light
proxy over an authored cycle, not the current particle pose or a particle replay.
"""
import math
import struct


def _axis(q):
    x,y,z,w=q;length=sum(v*v for v in q)
    return (2*(x*z+y*w)/length,2*(y*z-x*w)/length,1-2*(x*x+y*y)/length)


def _roots(a,b,c):
    if abs(a)<1e-15:return [] if abs(b)<1e-15 else [-c/b]
    discriminant=b*b-4*a*c
    if discriminant<0:return []
    r=math.sqrt(discriminant)
    return [(-b-r)/(2*a),(-b+r)/(2*a)]


def _segment_bounds(a,b):
    # A rotated axis is quadratic/quadratic along a normalized quaternion
    # segment. Its derivative is quadratic, so extrema need no sampled guess.
    delta=tuple(y-x for x,y in zip(a,b))
    def polynomial(sample):
        c=sample(0);mid=sample(.5);end=sample(1);aa=2*(end+c-2*mid)
        return aa,end-c-aa,c
    def quaternion(t):return tuple(x+t*y for x,y in zip(a,delta))
    d,e,f=polynomial(lambda t:sum(v*v for v in quaternion(t)))
    result=[]
    for k in range(3):
        aa,bb,cc=polynomial(lambda t:_axis(quaternion(t))[k]*sum(v*v for v in quaternion(t)))
        candidates=[0,1]+[t for t in _roots(aa*e-bb*d,2*(aa*f-cc*d),bb*f-cc*e) if 0<t<1]
        values=[_axis(quaternion(t))[k] for t in candidates]
        result.append((min(values),max(values)))
    return result


def _mean_axis(blob,bone,local):
    from world_scene_builder import array,unpack
    from world_lights_builder import constant_track,static_bone_point
    anchor=static_bone_point(blob,bone,local) # Reject moving nozzles, billboard axes and nonrigid parents.
    globals_count,_=unpack('<II',blob,20)
    count,offset=unpack('<II',blob,28)
    if globals_count or count!=1:raise ValueError('Requires one inline default animation clock')
    duration=unpack('<I',blob,offset+4)[0]
    if not 0<duration<=600000 or not unpack('<I',blob,offset+12)[0]&0x20:
        raise ValueError('Unsupported external or unbounded animation')
    bones,bone_offset=unpack('<II',blob,44);rotation=None;seen=set()
    while bone>=0:
        if bone>=bones or bone in seen:raise ValueError('Invalid bone chain')
        seen.add(bone);at=bone_offset+bone*88;_,flags,parent=unpack('<iIh',blob,at)
        if flags&0x7f:raise ValueError('Unsupported inherited/billboard basis')
        if flags&0x200:
            if constant_track(blob,at+56,'<3f',(1.,1.,1.))!=(1.,1.,1.):raise ValueError('Nonrigid particle basis')
            mode,clock,nt,ot,nk,ok=unpack('<hh4I',blob,at+36)
            if mode not in (0,1) or clock!=-1 or nt!=nk or nt>1:raise ValueError('Unsupported rotation clock')
            if nt:
                tc,to=unpack('<II',blob,ot);kc,ko=unpack('<II',blob,ok)
                if not 0<tc==kc<=128:raise ValueError('Unsupported rotation key count')
                times=[r[0] for r in array(blob,to,tc,'<I')]
                if times[0]!=0 or times[-1]>duration or any(x>=y for x,y in zip(times,times[1:])):
                    raise ValueError('Invalid rotation times')
                keys=[]
                for packed in array(blob,ko,kc,'<4h'):
                    q=tuple((v-32767 if v>0 else v+32767)/32767 for v in packed)
                    norm=sum(v*v for v in q)
                    if not .98<=norm<=1.02:raise ValueError('Invalid normalized rotation')
                    q=tuple(v/math.sqrt(norm) for v in q)
                    if keys and sum(x*y for x,y in zip(keys[-1],q))<0:q=tuple(-v for v in q)
                    keys.append(q)
                if any(abs(q[0])+abs(q[1])+abs(q[2])>1e-4 for q in keys):
                    if rotation is not None:raise ValueError('Multiple rotating parent bases')
                    if times[-1]<duration:times.append(duration);keys.append(keys[-1])
                    if len(times)==1:times.append(duration);keys.append(keys[0])
                    rotation=mode,times,keys
        bone=parent
    if rotation is None:return anchor,(0.,0.,1.),(0.,0.,1.),(0.,0.,1.),duration
    mode,times,keys=rotation;mean=[0.,0.,0.];low=[1.,1.,1.];high=[-1.,-1.,-1.]
    for i,(start,end) in enumerate(zip(times,times[1:])):
        a,b=keys[i:i+2]
        bounds=_segment_bounds(a,b) if mode else [(min(x,y),max(x,y)) for x,y in zip(_axis(a),_axis(b))]
        for k,(lo,hi) in enumerate(bounds):low[k]=min(low[k],lo);high[k]=max(high[k],hi)
        for j in range(128):
            t=(j+.5)/128 if mode else 0.
            axis=_axis(tuple(x+(y-x)*t for x,y in zip(a,b)))
            for k in range(3):mean[k]+=axis[k]*(end-start)/duration/128
    return anchor,tuple(mean),tuple(low),tuple(high),duration


def native_fixture_offsets(assets,path,blob,sources):
    """Native source index -> original anchor, mean offset and bounded evidence."""
    if not path.lower().replace('/','\\').startswith('world\\'):return {}
    from world_scene_builder import array,unpack
    from world_lights_builder import constant_track,static_bone_point
    from world_emitter_coverage import particle_sources,flame_motion_centroid
    from build_equipment_emitters import lifetime_track
    import collections
    try:
        native,native_offset=unpack('<II',blob,264);count,offset=unpack('<II',blob,296)
        if not 1<=native<=64 or not 1<=count<=128:return {}
        n,off=unpack('<II',blob,60);vertices=array(blob,off,n,'<3f4B4B3f4f')
        if not vertices:return {}
        extent=max(max(v[k] for v in vertices)-min(v[k] for v in vertices) for k in range(3))
        if not math.isfinite(extent) or extent<=0:return {}
        tolerance=extent*.001 # Coincident authored nozzle; scales with the model, not a filename.
        particles=particle_sources(assets,path,blob,{},collections.Counter())
        stride=492 if unpack('<I',blob,16)[0]&512 else 476
        result={}
        for source in sources:
            if source[5]!=2 or not source[6]&2 or not 0<=source[4]<native:continue
            raw=native_offset+source[4]*156;kind,bone,*point=unpack('<Hh3f',blob,raw)
            if kind!=1 or math.dist(static_bone_point(blob,bone,point),source[0])>tolerance:continue
            matches=[p for p in particles if math.dist(static_bone_point(blob,p['bones'][0],p['local_position']),source[0])<=tolerance]
            if len(matches)!=1:continue
            particle=matches[0];p=offset+particle['particle']*stride
            nozzle=static_bone_point(blob,particle['bones'][0],particle['local_position'])
            if sum(s[5]==2 and math.dist(nozzle,s[0])<=tolerance for s in sources)!=1:continue
            if constant_track(blob,p+132,'<f',(0.,))[0] or unpack('<f',blob,p+372)[0]:continue
            variation=constant_track(blob,p+72,'<f',(0.,))[0]
            latitude=constant_track(blob,p+92,'<f',(0.,))[0];azimuth=constant_track(blob,p+112,'<f',(0.,))[0]
            if not 0<=variation<=1 or not 0<=latitude<math.pi/2 or (latitude and not math.isclose(azimuth,2*math.pi,abs_tol=1e-5)):continue
            flags=unpack('<I',blob,p+4)[0];blend,emitter=unpack('<BB',blob,p+40)
            height=flame_motion_centroid(blob,p,flags,emitter,particle['lifetime'],lifetime_track(blob,p+260,'<3f'),
                lifetime_track(blob,p+276,'<h') if blend==4 else [(0,(32767,))],lifetime_track(blob,p+292,'<2f'))
            if height is None or not 0<height<=min(extent,source[3]*.25):continue
            anchor,mean,low,high,duration=_mean_axis(blob,particle['bones'][0],particle['local_position'])
            radius=height*math.sqrt(sum(max(abs(low[k]-mean[k]),abs(high[k]-mean[k]))**2 for k in range(3)))
            if radius>height*.5:continue # A broad orbit has no representative compact point.
            position=tuple(anchor[k]+height*mean[k] for k in range(3))
            result[source[4]]={'anchor':source[0],'offset':tuple(position[k]-source[0][k] for k in range(3)),
                'position':position,'motion_radius':radius,'centroid_low':tuple(anchor[k]+height*low[k] for k in range(3)),
                'centroid_high':tuple(anchor[k]+height*high[k] for k in range(3)),'period_ms':duration,'particle':particle['particle'],
                'basis':'Time-averaged authored luminous volume; bounded steady proxy, not current particle replay'}
        return result
    except (ValueError,IndexError,KeyError,FileNotFoundError,struct.error):return {}
