#pragma once
#include "worldmodel/CreatureObject.h"
#include "worldmodel/PlayerObject.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace clientcommon {
// SWGEmu.exe 0x12de9f0, table 0x1600c88: do not infer icons from
// locomotion speed or substitute a later client's posture enumeration.
inline constexpr std::array<std::string_view,15> postureNames{
    "upright","crouched","prone","sneaking","blocking","climbing","flying",
    "lyingDown","sitting","skillAnimating","drivingVehicle","ridingCreature",
    "knockedDown","incapacitated","dead"};
struct HamBarStatus {
    int32_t current=0,maximum=0,wounds=0;
    float currentFraction() const {
        if(maximum<=0)return 0;
        return float(std::clamp<int64_t>(current,0,std::clamp<int64_t>(int64_t(maximum)-wounds,0,maximum)))/maximum;
    }
    float usableFraction() const {
        if(maximum<=0)return 0;
        return float(std::clamp<int64_t>(int64_t(maximum)-wounds,0,maximum))/maximum;
    }
};
struct CreatureStatus {
    uint64_t id=0;
    std::optional<uint8_t> posture;
    std::optional<uint64_t> states;
    std::optional<bool> tef,mountVehicle;
    std::array<std::optional<HamBarStatus>,3> ham;
    uint32_t hamCounter=0,maxHamCounter=0,woundsCounter=0;
    bool forceKnown=false;
    std::optional<HamBarStatus> force;
    std::optional<int32_t> sharedMaximum() const {
        int32_t maximum=0;
        for(const auto& row:ham) {if(!row||row->maximum<=0)return {};maximum=std::max(maximum,row->maximum);}
        return maximum;
    }
};
// Stable horizontal branch of SWGEmu.exe's SwgCuiMfdStatusBar::setValue
// (0xe166d0): integer divisions occur at each nested page, not just once
// at the final current/sharedMax ratio. Authored current margins are zero.
struct HamBarGeometry { int normal=0,usable=0,current=0; };
inline HamBarGeometry hamBarGeometry(const HamBarStatus& value,int32_t scale,int width) {
    HamBarGeometry result;
    if(width<=0||scale<=0||value.maximum<=0)return result;
    result.normal=int(std::clamp<int64_t>(int64_t(width)*value.maximum/scale,0,width));
    auto usable=std::clamp<int64_t>(int64_t(value.maximum)-value.wounds,0,value.maximum);
    result.usable=int(int64_t(result.normal)*usable/value.maximum);
    if(usable>0)result.current=int(int64_t(result.usable)*std::clamp<int64_t>(value.current,0,usable)/usable);
    return result;
}
inline void setPlayerForce(CreatureStatus& status,const worldmodel::PlayerObject& player) {
    // PLAY8 is self-only. Require observed containment in the same creature
    // rather than choosing an arbitrary PlayerObject from the scene.
    status.forceKnown=false;status.force.reset();
    if(!player.containmentMessagesSeen||player.containerId!=status.id||!player.base8||!player.base8->forceCurrent)return;
    const auto& b=*player.base8;
    if(b.forcePowerMax<0)return;
    status.forceKnown=true;
    if(b.forcePowerMax>0)status.force=HamBarStatus{b.forcePower,b.forcePowerMax,0};
}
inline CreatureStatus creatureStatus(const worldmodel::CreatureObject& object) {
    CreatureStatus result;result.id=object.objectId;
    if(object.base3&&object.base3->postureCurrent&&object.base3->posture<postureNames.size())
        result.posture=object.base3->posture;
    if(object.base3&&object.base3->statesCurrent)result.states=object.base3->stateBitmask;
    if(object.pvpStatusBitmask&&(!object.base3||!object.base3->creatureLinkId))
        result.tef=bool(*object.pvpStatusBitmask&8);
    if(!object.base3||!object.base6)return result;
    const auto& a=*object.base3;const auto& b=*object.base6;
    result.hamCounter=b.hamCounter;result.maxHamCounter=b.maxHamCounter;result.woundsCounter=a.woundsCounter;
    if(!b.hamCurrent||!b.maxHamCurrent||!a.woundsCurrent||b.ham.size()!=9||b.maxHam.size()!=9||a.wounds.size()!=9)return result;
    // Core3 CreatureAttribute: health=0, action=3, mind=6; remaining
    // attributes are strength/constitution, quickness/stamina, focus/willpower.
    for(size_t row=0;row<3;++row) {
        auto i=row*3;
        if(b.maxHam[i]>0&&a.wounds[i]>=0&&a.wounds[i]<=b.maxHam[i])
            result.ham[row]=HamBarStatus{b.ham[i],b.maxHam[i],a.wounds[i]};
    }
    return result;
}
}
