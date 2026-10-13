#!/usr/bin/env python3
"""Independent scalar checks of local/world wire and renderer model frames; not server acceptance."""
import argparse,hashlib,json,math
from pathlib import Path

def analyze(rows,minimum_seconds=30,require_indoor=False):
 if len(rows)>4000:raise ValueError('frame journal exceeded event bound')
 errors=[];wire=[];scene=[];max_position=max_model=max_angle=0
 def vector(r,k,n):
  v=r[k]
  if len(v)!=n or not all(math.isfinite(x) for x in v):raise ValueError('nonfinite/malformed '+k)
  return v
 def unit(q):
  n=math.sqrt(sum(x*x for x in q))
  if abs(n-1)>1e-5:raise ValueError('nonunit quaternion')
  return [x/n for x in q]
 def matrix(q):
  x,y,z,w=q
  return [[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],[2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],[2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]]
 def product(a,b):
  x,y,z,w=a;X,Y,Z,W=b
  return [w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z]
 previous=-1;times=[]
 for i,r in enumerate(rows):
  try:
   t=r['elapsed_ms']
   if not isinstance(t,(int,float)) or isinstance(t,bool) or not math.isfinite(t) or t<0 or t<previous:raise ValueError('invalid or regressed elapsed time')
   previous=t;times.append(t)
   if r['event']=='scene_frames':scene.append(r);continue
   if r['event']!='wire_frame':raise ValueError('unknown event')
   world=vector(r,'world_feet',3);local=vector(r,'wire_feet',3);model=vector(r,'model_world_feet',3)
   q=unit(vector(r,'wire_rotation',4));wq=unit(vector(r,'world_rotation',4));pq=unit(vector(r,'parent_rotation',4));origin=vector(r,'parent_origin',3)
   if not r['parent_id']:pq=[0,0,0,1];origin=[0,0,0]
   m=matrix(pq);delta=[a-b for a,b in zip(world,origin)];expected=[sum(m[j][i]*delta[j] for j in range(3)) for i in range(3)]
   position=math.dist(local,expected);model_error=math.dist(model,world)
   max_position=max(max_position,position);max_model=max(max_model,model_error)
   inverse=[-pq[0],-pq[1],-pq[2],pq[3]];expected_q=product(inverse,wq)
   cosine=min(1,abs(sum(a*b for a,b in zip(q,expected_q))));angle=2*math.acos(cosine);max_angle=max(max_angle,angle)
   if position>.002:errors.append(f'wire coordinate error at row {i}')
   if model_error>.002:errors.append(f'renderer model error at row {i}')
   if angle>.0005:errors.append(f'wire rotation error at row {i}')
   wire.append(r)
  except (KeyError,TypeError,ValueError,OverflowError) as e:errors.append(f'malformed row {i}: {e}')
 seconds=(times[-1]-times[0])/1000 if times else 0
 indoor=sum(bool(r['parent_id']) for r in wire)
 if len(wire)<10:errors.append('insufficient wire samples')
 if seconds<minimum_seconds:errors.append('insufficient duration')
 if require_indoor and not indoor:errors.append('no indoor wire frames observed')
 return dict(passed=not errors,events=len(rows),wire_samples=len(wire),indoor_wire_samples=indoor,elapsed_seconds=seconds,max_position_error_m=max_position,max_model_error_m=max_model,max_orientation_error_radians=max_angle,scene_snapshots=scene,errors=errors,server_acceptance_verified=False,original_controller_parity=False)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('journal',type=Path);p.add_argument('--output',type=Path);p.add_argument('--minimum-seconds',type=float,default=30);p.add_argument('--require-indoor',action='store_true');a=p.parse_args()
 if not math.isfinite(a.minimum_seconds) or a.minimum_seconds<0:p.error('invalid minimum duration')
 raw=a.journal.read_bytes();rows=[json.loads(l) for l in raw.splitlines(keepends=True) if l.endswith(b'\n')]
 r=analyze(rows,a.minimum_seconds,a.require_indoor);r.update(journal=str(a.journal),sha256=hashlib.sha256(raw).hexdigest())
 text=json.dumps(r,indent=2,allow_nan=False)+'\n'
 if a.output:a.output.write_text(text)
 print(text,end='');raise SystemExit(0 if r['passed'] else 1)
if __name__=='__main__':main()
