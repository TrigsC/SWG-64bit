#pragma once
#include "CreatureStatus.h"
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace clientcommon {
// Original executable table 0x1600ba8 plus controller-only driving,
// sneaking and TEF names. These last three are not additional wire bits.
inline constexpr std::array<std::string_view,37> stateIconNames{
    "cover","combat","peace","aiming","alert","berserk","feignDeath",
    "combatAttitudeEvasive","combatAttitudeNormal","combatAttitudeAggressive",
    "tumbling","rallied","stunned","blinded","dizzy","intimidated",
    "immobilized","frozen","swimming","sittingOnChair","crafting","glowingJedi",
    "maskScent","poisoned","bleeding","diseased","onFire","ridingMount",
    "mountedCreature","pilotingShip","shipOperations","shipGunner","shipInterior",
    "pilotingPobShip","driving","sneaking","tef"};
struct StateIcon { uint8_t key=0;float opacity=0; };
struct IndicatorCreature {
    std::optional<uint32_t> pvp;
    std::optional<uint64_t> link;
    std::optional<uint32_t> options;
    uint64_t container=0;
    uint32_t objectCrc=0;
    bool containmentKnown=false;
};
using IndicatorScene=std::unordered_map<uint64_t,IndicatorCreature>;
inline std::optional<bool> creatureTef(uint64_t id,const IndicatorScene& scene) {
    std::unordered_set<uint64_t> visited;
    for(size_t depth=0;depth<32;++depth) {
        auto it=scene.find(id);if(it==scene.end()||!visited.insert(id).second)return {};
        const auto& c=it->second;
        if(!c.link)return {}; // stale/missing BASE3 cannot supply a reliable link
        if(*c.link&&scene.contains(*c.link)){id=*c.link;continue;}
        return c.pvp?std::optional<bool>(bool(*c.pvp&8)):std::optional<bool>{};
    }
    return {};
}
inline std::optional<bool> creatureVehicle(uint64_t id,const IndicatorScene& scene,
        const std::function<std::optional<uint32_t>(uint32_t)>& type) {
    auto it=scene.find(id);if(it==scene.end()||!it->second.containmentKnown)return {};
    if(!it->second.container)return false;
    auto parent=scene.find(it->second.container);if(parent==scene.end())return false;
    if(!parent->second.options)return {};
    if(!(*parent->second.options&0x1000))return false;
    auto value=type(parent->second.objectCrc);if(!value)return {};
    return *value==0x10000||(*value&0xffffff00)==0x10000;
}
class CreatureIndicators {
public:
    const std::vector<StateIcon>& icons() const {return icons_;}
    void clear(){icons_.clear();}
    // A UIManager clock notification performs one Effect call, even when
    // Advance catches up several ticks. Repeated notifications add 8/60.
    void heartbeat(float speed=8.f) {
        for(auto& icon:icons_)icon.opacity=std::min(1.f,icon.opacity+speed/60.f);
    }
    void reconcile(const CreatureStatus* status) {
        if(!status){clear();return;}
        std::optional<uint8_t> previous;
        auto update=[&](uint8_t key,bool active) {
            auto found=std::find_if(icons_.begin(),icons_.end(),[&](const auto& i){return i.key==key;});
            if(!active){if(found!=icons_.end())icons_.erase(found);return;}
            if(found==icons_.end()) {
                auto position=icons_.end();
                if(previous){position=std::find_if(icons_.begin(),icons_.end(),[&](const auto& i){return i.key==*previous;});if(position!=icons_.end())++position;}
                icons_.insert(position,StateIcon{key,0});
            }
            previous=key;
        };
        for(uint8_t i=0;i<34;++i) {
            if(i>=7&&i<=9)continue;
            const bool active=status->states&&(*status->states&(uint64_t(1)<<i));
            if(i==27) {
                if(!active){update(34,false);update(27,false);}
                else if(status->mountVehicle)update(*status->mountVehicle?34:27,true);
                else {update(34,false);update(27,false);} // unavailable parent/type evidence
            } else update(i,active);
        }
        update(35,status->posture&&*status->posture==3);
        update(36,status->tef.value_or(false));
    }
private:
    std::vector<StateIcon> icons_;
};
}
