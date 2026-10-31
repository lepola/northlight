#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Production GTAO versus cosine-weighted rays through finite authored boxes.

No game data. Thin, suspended and deep occluders; three camera pitches. The
reference uses exact ray/box intersections, not the GTAO horizon calculation.
"""
from pathlib import Path
import argparse,ctypes,hashlib,importlib.util,json,struct,subprocess,sys,time
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def intersect(origin,direction,low,high):
    inverse=np.divide(1,direction,out=np.full_like(direction,1e20),where=np.abs(direction)>1e-12)
    a,b=(low-origin)*inverse,(high-origin)*inverse
    enter=np.maximum(np.minimum(a,b).max(-1),0);leave=np.maximum(a,b).min(-1)
    return np.where(leave>=enter,enter,np.inf)

def main():
    parser=argparse.ArgumentParser(__doc__);parser.add_argument('--backend',action='append',required=True);parser.add_argument('--shader',type=Path,default=fp.COMPILED/'GTAOContactBloom.bin');args=parser.parse_args()
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.CreateMutexW.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_wchar_p];kernel.CreateMutexW.restype=ctypes.c_void_p;kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong];kernel.ReleaseMutex.argtypes=[ctypes.c_void_p];kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    mutex=kernel.CreateMutexW(None,0,'Local\\NorthlightOfflineGPU')
    if not mutex or kernel.WaitForSingleObject(mutex,0) not in (0,128):raise RuntimeError('Another offline GPU test is active')
    try:
        import windows_gpu_guard as guard
        def closed():
            if any(p['name'].lower().startswith('wow') and p['name'].lower().endswith('.exe') for p in guard.processes()):raise RuntimeError('Game is open; defer GPU tests')
        closed();out=fp.output_dir()/f'gtao-silhouettes-{time.time_ns()}';out.mkdir(parents=True)
        env=fp.zig_env();env['DXVK_LOG_PATH']=str(out);flags={'creationflags':subprocess.BELOW_NORMAL_PRIORITY_CLASS|subprocess.CREATE_NO_WINDOW}
        for key,name in [('TEMP','temp'),('TMP','temp'),('DXVK_SHADER_CACHE_PATH','shader-cache'),('DXVK_STATE_CACHE_PATH','state-cache')]:
            (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
        exe=out/'gtao.exe'
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-return-type','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'gtao_silhouette_gpu.cpp'),'-luser32','-o',str(exe)],env=env,check=True,**flags)
        width,height=640,360;near,far,part=.2,1000,.94;sx,sy=1.2686,1.9626;radius,strength=2,.6
        rx,ry=np.meshgrid((2*(np.arange(width)+.5)/width-1)/sx,(1-2*(np.arange(height)+.5)/height)/sy)
        rays=np.stack([rx,ry,np.ones_like(rx)],-1);camera=np.array([0,1.65,0]);count=4096
        t=(np.arange(count)+.5)/count;azimuth=np.arange(count)*2.399963229728653
        directions=np.stack([np.sqrt(t)*np.cos(azimuth),np.sqrt(1-t),np.sqrt(t)*np.sin(azimuth)],-1)
        results=[]
        # MAE and excess occlusion ceilings in AO visibility units. Shallow
        # views allow a larger thin silhouette error because it spans few pixels.
        limits=[{'thin':(.0074,.0034),'floating':(.0046,.0019),'deep':(.0172,.001)},
                {'thin':(.0071,.0022),'floating':(.0051,.0010),'deep':(.121,.001)},
                {'thin':(.0078,.0022),'floating':(.0053,.0004),'deep':(.099,.001)}]
        for view,(pitch,center) in enumerate([(.05,8),(.3,4),(.65,4)]):
            basis=np.array([[1,0,0],[0,np.cos(pitch),-np.sin(pitch)],[0,np.sin(pitch),np.cos(pitch)]]);local=rays@basis.T
            floor=-camera[1]/local[:,:,1];floor=np.where(floor>near,floor,far)
            for kind,half_width,half_depth,gap in [('thin',.18,.18,0),('deep',2,2,0),('floating',.18,.18,.7)]:
                folder=out/f'{view}-{kind}';folder.mkdir();low=np.array([-half_width,gap,center-half_depth]);high=np.array([half_width,gap+1.8,center+half_depth])
                box=intersect(camera,local,low,high);depth=np.clip(np.minimum(floor,box),near,far);raw=np.round(far/(far-near)*(1-near/depth)*part*16777215)/16777215
                (folder/'depth.fgr').write_bytes(struct.pack('<5I',0x31524746,width,height,114,4)+raw.astype('<f4').tobytes())
                constants=np.zeros((7,4),np.float32);constants[0]=[1/width,1/height,near,far];constants[1]=[sx,sy,strength,0];constants[2,1]=radius;constants[3]=[0,1/part,part,0];(folder/'constants.f32').write_bytes(constants.tobytes())
                points=(rays[1::2,1::2]*floor[1::2,1::2,None])@basis.T+camera
                distance=np.sqrt(np.maximum(abs(points[:,:,0])-half_width,0)**2+np.maximum(abs(points[:,:,2]-center)-half_depth,0)**2)
                mask=(floor[1::2,1::2]<box[1::2,1::2])&(distance<2.2)&(points[:,:,2]>center-4)&(points[:,:,2]<center+4)
                indices=np.flatnonzero(mask)[::8];p=points.reshape(-1,3)[indices].copy();p[:,1]=.0001;assert len(p)>200
                reference=[]
                for chunk in np.array_split(p,max(1,len(p)//64)):
                    hit=intersect(chunk[:,None,:],directions[None,:,:],low,high)
                    reference.extend(1-strength*np.clip((radius-hit)/(radius*.5),0,1).mean(-1))
                reference=np.array(reference);contact=distance.reshape(-1)[indices]<.4;assert contact.any()
                np.savez(folder/'reference.npz',indices=indices,visibility=reference)
                for index,backend in enumerate(args.backend):
                    closed();run=subprocess.run([str(exe),str(folder)+'/',str(args.shader.resolve()),str(fp.COMPILED/'AOBlur.bin'),str(Path(backend).resolve())],cwd=folder,env=env,capture_output=True,text=True,timeout=60,**flags)
                    if run.returncode:raise RuntimeError(run.stdout+run.stderr)
                    for name in ['raw','blur']:
                        image=np.fromfile(folder/(name+'.f32'),np.float32)
                        # The unchanged weighted blur can round one ULP above 1.
                        assert image.size==width*height//4 and np.isfinite(image).all() and image.min()>=0 and image.max()<=1+4*np.finfo(np.float32).eps
                        (folder/f'{name}-{index}.f32').write_bytes(image.tobytes())
                    actual=image[indices];mae=float(np.mean(abs(actual-reference)));excess=float(np.maximum(reference-actual,0).mean());contact_visibility=float(actual[contact].mean())
                    maximum_mae,maximum_excess=limits[view][kind]
                    assert mae<maximum_mae and excess<maximum_excess,(view,kind,backend,mae,excess)
                    # Contact must survive; a blank/disabled AO image fails this.
                    if kind!='floating':assert .65<contact_visibility<.91,(view,kind,contact_visibility)
                    result={'view':view,'kind':kind,'backend':str(backend),'points':len(p),'mae':mae,'excess':excess,'contact_visibility':contact_visibility};results.append(result);print(result,flush=True)
        report={'source_sha256':hashlib.sha256((fp.SHADERS/'effects.hlsl').read_bytes()).hexdigest(),'shader_sha256':hashlib.sha256(args.shader.read_bytes()).hexdigest(),'game_launched':False,'reference':'4096 cosine-distributed rays, finite boxes, exact slabs, 2-yard falloff','results':results}
        (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('GTAO_SILHOUETTES_PASS',out,flush=True)
    finally:
        kernel.ReleaseMutex(mutex);kernel.CloseHandle(mutex)

if __name__=='__main__':main()
