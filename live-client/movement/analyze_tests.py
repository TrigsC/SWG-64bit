import unittest
from analyze_session import analyze

def row(t,c,**extra):
 r=dict(event='tx_queued',elapsed_ms=t,poll_elapsed_ms=t,wire_timestamp=t&0xffffffff,counter=c,object_id=100,parent_id=0,x=1,y=2,z=3,speed=1);r.update(extra);return r
class MovementAuditTests(unittest.TestCase):
 def test_queue_jitter_uses_actual_poll_clock(self):
  r=analyze([row(400,1,elapsed_ms=403),row(800,2,elapsed_ms=800)])
  self.assertTrue(r['telemetry_valid']);self.assertEqual(r['minimum_queue_gap_ms'],397);self.assertEqual(r['minimum_poll_gap_ms'],400)
 def test_early_poll_and_corrupt_timestamp(self):
  self.assertFalse(analyze([row(400,1),row(798,2)])['telemetry_valid'])
  self.assertFalse(analyze([row(400,1,wire_timestamp=399)])['telemetry_valid'])
 def test_legacy_failure_retained(self):
  a,b=row(403,1),row(800,2)
  for r in (a,b):del r['poll_elapsed_ms'];del r['wire_timestamp']
  self.assertFalse(analyze([a,b])['telemetry_valid'])
 def test_wrap_and_incomplete_schema(self):
  t=0xffffffff-100;self.assertTrue(analyze([row(t,0xffffffff),row(t+400,0)])['telemetry_valid'])
  b=row(800,2);del b['wire_timestamp'];self.assertFalse(analyze([row(400,1),b])['telemetry_valid'])
 def test_only_causal_same_parent_raw_sync_matches(self):
  tx=row(400,1,parent_id=501)
  sync=row(460,1,parent_id=501,event='self_sync',x=1.001)
  self.assertAlmostEqual(analyze([tx,sync])['matched_wire_frame_syncs'][0]['position_error_m'],.001)
  self.assertEqual(analyze([tx,{**sync,'parent_id':502}])['matched_wire_frame_syncs'],[])
  self.assertEqual(analyze([{**tx,'elapsed_ms':500},sync])['matched_wire_frame_syncs'],[])
  self.assertEqual(analyze([tx,{**sync,'event':'self_correction'}])['matched_wire_frame_syncs'],[])
 def test_offset_remains_unclassified(self):
  r=analyze([row(400,0xffffffff),row(460,0,event='self_sync')])
  self.assertEqual(len(r['counter_plus_one_unclassified']),1);self.assertFalse(r['movement_and_sync_observed']);self.assertEqual(r['matched_wire_frame_syncs'],[])
 def test_nonfinite_and_repeated_counter(self):
  with self.assertRaises(ValueError):analyze([row(400,1,x=float('nan'))])
  self.assertFalse(analyze([row(400,1),row(800,1)])['telemetry_valid'])
if __name__=='__main__':unittest.main()
