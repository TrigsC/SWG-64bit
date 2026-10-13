import unittest,copy,math
from analyze_frames import analyze

def rows(parent=0):
 q=[0,math.sin(.37/2),0,math.cos(.37/2)];c,s=math.cos(.37),math.sin(.37)
 local=[3,2,17];world=[100+c*3+s*17,42,-200-s*3+c*17] if parent else local
 return [dict(event='wire_frame',elapsed_ms=i*4000,counter=i+1,object_id=42,parent_id=parent,world_feet=world[:],wire_feet=local[:],world_rotation=q[:] if parent else [0,0,0,1],wire_rotation=[0,0,0,1],parent_origin=[100,40,-200] if parent else [0,0,0],parent_rotation=q[:] if parent else [0,0,0,1],model_world_feet=world[:]) for i in range(11)]
class FrameAuditTests(unittest.TestCase):
 def test_indoor_and_outdoor(self):
  self.assertTrue(analyze(rows())['passed']);self.assertTrue(analyze(rows(501),require_indoor=True)['passed'])
 def test_wrong_parent_and_legacy_heading_rejected(self):
  for mutation in ('parent_id','parent_rotation'):
   r=rows(501)
   if mutation=='parent_id':r[2][mutation]=0
   else:r[2][mutation]=[0,math.sin(.36/2),0,math.cos(.36/2)]
   self.assertFalse(analyze(r)['passed'])
 def test_wrong_model_rejected(self):
  r=rows(501);r[3]['model_world_feet'][0]+=.03;self.assertFalse(analyze(r)['passed'])
 def test_wrong_wire_orientation_rejected(self):
  r=rows(501);r[3]['wire_rotation']=[0,math.sin(.1/2),0,math.cos(.1/2)];self.assertFalse(analyze(r)['passed'])
 def test_opposite_sign_is_same_rotation(self):
  r=rows(501)
  for x in r:x['wire_rotation']=[0,0,0,-1]
  self.assertTrue(analyze(r)['passed'])
 def test_bad_quaternion_and_nonfinite(self):
  for key,value in [('parent_rotation',[0,0,0,0]),('world_feet',[math.nan,2,17])]:
   r=rows(501);r[3][key]=value;self.assertFalse(analyze(r)['passed'])
 def test_missing_fields_and_time(self):
  r=rows();del r[4]['wire_rotation'];self.assertFalse(analyze(r)['passed'])
  r=rows();r[4]['elapsed_ms']=0;self.assertFalse(analyze(r)['passed'])
  r=rows();r[4]['elapsed_ms']=math.nan;self.assertFalse(analyze(r)['passed'])
 def test_indoor_gate_not_inferred(self):
  self.assertFalse(analyze(rows(),require_indoor=True)['passed'])
 def test_coverage_bounds(self):
  self.assertFalse(analyze(rows()[:2])['passed'])
  with self.assertRaises(ValueError):analyze(rows()*400)
if __name__=='__main__':unittest.main()
