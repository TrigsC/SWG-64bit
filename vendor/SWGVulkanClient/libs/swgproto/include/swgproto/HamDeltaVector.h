#pragma once
#include "swgproto/ObjectSchema.h"
#include <vector>

namespace swgproto {
// The nine-element creature attribute vectors use different baseline and
// delta encodings. Counters/validity are local metadata, never wire fields.
struct HamVectorOperation { uint8_t tag=0; uint16_t index=0; int32_t value=0; };
struct HamVectorPatch { uint32_t counter=0; std::vector<HamVectorOperation> operations; };
bool readHamVectorBaseline(std::vector<int32_t>& values,uint32_t& counter,bool& current,
                           soe::PacketBuffer& buf,std::string& error);
bool readHamVectorPatch(HamVectorPatch&,soe::PacketBuffer&,std::string& error);
bool applyHamVectorPatch(std::vector<int32_t>& values,uint32_t& counter,bool& current,
                        const HamVectorPatch&,std::string& error);
bool formatHamVectorPatch(soe::PacketBuffer&,std::string&,std::string& error);

template<auto Values,auto Counter,auto Current>
bool readHamBaseline(void* base,soe::PacketBuffer& buf,std::string& error) {
    using T=typename MemberPointerTraits<decltype(Values)>::ClassType;
    auto& obj=*static_cast<T*>(base);
    return readHamVectorBaseline(obj.*Values,obj.*Counter,obj.*Current,buf,error);
}
template<auto Values,auto Counter,auto Current>
bool readHamDelta(void* base,soe::PacketBuffer& buf,std::string& error) {
    using T=typename MemberPointerTraits<decltype(Values)>::ClassType;
    auto& obj=*static_cast<T*>(base);HamVectorPatch patch;
    if(!readHamVectorPatch(patch,buf,error)){obj.*Current=false;return false;}
    return applyHamVectorPatch(obj.*Values,obj.*Counter,obj.*Current,patch,error);
}
template<auto Values,auto Counter,auto Current>
constexpr FieldDescriptor hamVectorField(uint16_t index,const char* name) {
    static_assert(std::is_same_v<typename MemberPointerTraits<decltype(Values)>::MemberType,std::vector<int32_t>>);
    return {index,true,name,FieldKind::Int32Container,&readHamBaseline<Values,Counter,Current>,
        &decodeAndFormatTrampoline<FieldKind::Int32Container>,&readHamDelta<Values,Counter,Current>,&formatHamVectorPatch};
}
}
