#!/usr/bin/env python3
"""Validate bounded movement telemetry, keeping raw wire frames separate from corrections."""
import argparse,json,math
from pathlib import Path

def analyze(rows,journal='fixture'):
 if len(rows)>10000:raise ValueError('journal exceeded event bound')
 for r in rows:
  if not all(math.isfinite(r[k]) for k in ['x','y','z','speed']):raise ValueError('nonfinite movement event')
 tx=[r for r in rows if r['event']=='tx_queued'];corrections=[r for r in rows if r['event']=='self_correction'];server=[r for r in rows if r['event']=='self_server'];remote=[r for r in rows if r['event']=='remote_server'];sync=[r for r in rows if r['event']=='self_sync']
 errors=[];queue_gaps=[];poll_gaps=[]
 clocks=['poll_elapsed_ms' in r and 'wire_timestamp' in r for r in tx]
 if clocks and any(clocks) and not all(clocks):errors.append('mixed or incomplete report timestamp schema')
 for r in tx:
  if ('poll_elapsed_ms' in r)!=('wire_timestamp' in r):errors.append('incomplete report timestamp schema')
  if 'poll_elapsed_ms' in r and 'wire_timestamp' in r and (r['poll_elapsed_ms']&0xffffffff)!=r['wire_timestamp']:errors.append('wire timestamp differs from poll clock')
 for x,y in zip(tx,tx[1:]):
  queue_gaps.append(y['elapsed_ms']-x['elapsed_ms'])
  clock='poll_elapsed_ms' if all(clocks) else 'elapsed_ms'
  gap=y[clock]-x[clock];poll_gaps.append(gap)
  if gap<(400 if clock=='poll_elapsed_ms' else 399):errors.append('report cadence below bound')
  delta=(y['counter']-x['counter'])&0xffffffff
  if not 0<delta<0x80000000:errors.append('outbound counter did not advance')
 last_remote={};remote_moved=set()
 for r in remote:
  pos=(r['parent_id'],r['x'],r['y'],r['z'])
  if r['object_id'] in last_remote and pos!=last_remote[r['object_id']]:remote_moved.add(r['object_id'])
  last_remote[r['object_id']]=pos
 stop_transitions=sum(x['speed']>0 and y['speed']==0 for x,y in zip(tx,tx[1:]))
 matches=[];offset_candidates=[]
 # Raw self_sync is in the same local/world wire frame as outbound records.
 # Historical self_correction was resolved to world space: outdoor only.
 for r in (sync if sync else corrections):
  eligible=[t for t in tx if t['elapsed_ms']<=r['elapsed_ms'] and t['object_id']==r['object_id'] and t['parent_id']==r['parent_id'] and (sync or not r['parent_id'])]
  exact=next((t for t in reversed(eligible) if t['counter']==r['counter']),None)
  def comparison(t):return {'counter':r['counter'],'sent_counter':t['counter'],'parent_id':r['parent_id'],'position_error_m':math.dist([t[k] for k in ['x','y','z']],[r[k] for k in ['x','y','z']]),'response_delay_ms':r['elapsed_ms']-t['elapsed_ms']}
  if exact:matches.append(comparison(exact))
  elif eligible and ((r['counter']-eligible[-1]['counter'])&0xffffffff)==1:offset_candidates.append(comparison(eligible[-1]))
 return {'journal':str(journal),'events':len(rows),'telemetry_valid':not errors,'errors':sorted(set(errors)),'queued_reports':len(tx),'cadence_clock':'poll_elapsed_ms' if clocks and all(clocks) else 'legacy_queue_elapsed_ms','minimum_poll_gap_ms':min(poll_gaps,default=None),'minimum_queue_gap_ms':min(queue_gaps,default=None),'moving_reports':sum(r['speed']>0 for r in tx),'zero_speed_reports':sum(r['speed']==0 for r in tx),'stop_transitions':stop_transitions,'server_self_updates':len(server),'raw_authoritative_syncs':len(sync),'applied_authoritative_updates':len(corrections),'remote_updates':len(remote),'remote_objects_observed_moving':sorted(remote_moved),'matched_wire_frame_syncs':matches,'matched_outdoor_syncs':[r for r in matches if not r['parent_id']],'counter_plus_one_unclassified':offset_candidates,'movement_and_sync_observed':bool(tx and stop_transitions and matches),'limits':['Queued packets are not delivery acknowledgments.','A counter-plus-one response is observed but not classified as an acknowledgment; it may be a corrective relocation.','Matched server responses do not establish Windows controller parity or ordinary-account anti-cheat behavior.','Indoor comparison requires raw synchronization with the same server parent; historical world-space corrections are excluded.']}

def main():
 p=argparse.ArgumentParser();p.add_argument('journal',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 rows=[json.loads(s) for s in a.journal.read_text().splitlines() if s.strip()]
 text=json.dumps(analyze(rows,a.journal),indent=2)+'\n'
 if a.output:a.output.write_text(text)
 print(text,end='')
if __name__=='__main__':main()
