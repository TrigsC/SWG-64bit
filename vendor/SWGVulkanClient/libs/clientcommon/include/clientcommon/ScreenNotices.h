#pragma once
#include "ChatLog.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <set>
#include <stdexcept>
namespace clientcommon {
// Original normal SwgCuiSystemMessage FIFO, expressed with a 64-bit UI tick.
// UIClock ticks and effector notifications are deliberately separate.
class ScreenNotices {
public:
 struct Row {uint64_t sequence=0,startTick=0;std::u16string text;float opacity=0;bool enabled=true;int width=0,height=0,x=0,y=0,parentWidth=0;};
 ScreenNotices(float fadeIn=8,float fadeOut=1,float duration=1):fadeIn_(fadeIn),fadeOut_(fadeOut),holdTicks_(240.0*duration){
  if(!std::isfinite(fadeIn)||!std::isfinite(fadeOut)||!std::isfinite(duration)||fadeIn<=0||fadeOut<=0||duration<=0||duration>60)throw std::invalid_argument("Invalid screen-notice policy");
 }
 void synchronize(const ChatSnapshot& raw,const ChatSnapshot& presented,uint64_t tick){
  std::set<uint64_t> active;for(const auto& line:raw.lines)active.insert(line.sequence);
  std::erase_if(seen_,[&](uint64_t sequence){return !active.contains(sequence);});
  for(const auto& line:presented.lines)if(!line.text.empty()&&active.contains(line.sequence)&&!seen_.contains(line.sequence)){
   seen_.insert(line.sequence);
   if(line.kind==ChatLine::Kind::System&&!(line.displayType&2)&&!line.text.empty())admit(line.sequence,line.text,tick);
  }
 }
 void admit(uint64_t sequence,const std::u16string& text,uint64_t tick){
  if(text.empty()||text.size()>8192)return;
  const bool empty=rows_.empty();if(rows_.size()==256)rows_.pop_front();
  rows_.push_back({sequence,tick,text});++admissions_;if(empty)deadline_=double(tick)+holdTicks_;
 }
 void heartbeat(){for(auto& row:rows_)row.opacity=row.enabled?std::min(1.f,row.opacity+fadeIn_/60.f):std::max(0.f,row.opacity-fadeOut_/60.f);}
 void update(uint64_t tick){
  if(rows_.empty())return;
  if(double(tick)>deadline_){
   if(rows_.front().enabled)rows_.front().enabled=false;
   else if(rows_.front().opacity<=0){rows_.pop_front();deadline_=double(tick)+holdTicks_;}
  }
 }
 // Heights come from the original font's wrapped extents. UIComposite uses
 // half spacing at both outer edges, then scrolls overflow to the bottom.
 void packAndCull(int width,int height,int spacing,bool cull=true){
  if(width<=0||height<=0||spacing<0)return;
  int extent=spacing;for(const auto& row:rows_)extent+=row.height;
  if(rows_.size()>1)extent+=int(rows_.size()-1)*spacing;
  int at=spacing/2+std::max(0,height-extent)-std::max(0,extent-height);
  for(auto& row:rows_){row.x=(width-row.width)/2;row.y=at;at+=row.height+spacing;}
  while(cull&&!rows_.empty()&&rows_.front().y<0)rows_.pop_front(); // Inherits shared deadline.
 }
 static size_t unrollLimit(const Row& row,uint64_t tick,float speed=180){
  const auto elapsed=tick>=row.startTick?tick-row.startTick:0;
  const float progress=float(elapsed)/60.f*speed;
  if(!std::isfinite(progress)||progress>=row.text.size())return 0;
  return size_t(progress); // Original zero argument means unlimited, not empty.
 }
 static float textAlpha(const Row& row,float minimum=.8f,float parentOpacity=1.f){
  const float m=row.enabled?minimum:0.f;
  const float opacity=row.opacity*parentOpacity;float alpha=opacity*(1-m)+m;
  if(opacity<m&&opacity<.2f)alpha*=opacity/.2f;
  return alpha;
 }
 std::deque<Row>& rows(){return rows_;}
 const std::deque<Row>& rows()const{return rows_;}
 uint64_t admissions()const{return admissions_;}
 double deadline()const{return deadline_;}
 size_t seenCount()const{return seen_.size();}
private:
 std::deque<Row> rows_;std::set<uint64_t> seen_;float fadeIn_=8,fadeOut_=1;double holdTicks_=240,deadline_=0;uint64_t admissions_=0;
};
}
