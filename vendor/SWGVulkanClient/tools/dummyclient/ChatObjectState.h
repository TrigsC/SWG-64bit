#pragma once
#include "worldmodel/WorldObject.h"
#include "swgproto/StringId.h"
#include <optional>
#include <string>

namespace dummyclient {
struct ChatObjectState {
 uint64_t id=0;uint32_t objectCrc=0;bool creature=false,nameBaseline=false,appearanceKnown=false;
 std::u16string customName;swgproto::StringId nameId;std::string alternateAppearance;
};
// Copy only bounded name/appearance fields while the object-store lock is held.
// Archive lookup and formatter work happen after the snapshot has been copied.
template<class Object> ChatObjectState chatObjectState(const Object& object){
 ChatObjectState out;out.id=object.objectId;out.objectCrc=object.objectCrc;out.creature=object.typeTag==worldmodel::ObjectTypeTag::Creature;
 auto name=[&](const auto& base){out.nameBaseline=true;if(base.customObjectName.size()<=8192)out.customName=base.customObjectName;else out.nameBaseline=false;
  if(base.objectName.file.size()<=1024&&base.objectName.stringId.size()<=1024)out.nameId=base.objectName;else out.nameBaseline=false;};
 if constexpr(requires{object.base3->tangible.customObjectName;object.base3->tangible.objectName;}){if(object.base3)name(object.base3->tangible);}
 else if constexpr(requires{object.base3->intangible.customObjectName;object.base3->intangible.objectName;}){if(object.base3)name(object.base3->intangible);}
 else if constexpr(requires{object.base3->customObjectName;object.base3->objectName;}){if(object.base3)name(*object.base3);}
 else if constexpr(requires{object.tangible3Prefix->customObjectName;object.tangible3Prefix->objectName;}){if(object.tangible3Prefix)name(*object.tangible3Prefix);}
 if constexpr(requires{object.base6->alternateAppearance;})if(object.base6&&object.base6->alternateAppearance.size()<=1024){out.appearanceKnown=true;out.alternateAppearance=object.base6->alternateAppearance;}
 return out;
}
}
