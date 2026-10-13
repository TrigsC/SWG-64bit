#include "swgproto/HamDeltaVector.h"
#include <exception>
namespace swgproto {
bool readHamVectorBaseline(std::vector<int32_t>& values,uint32_t& counter,bool& current,soe::PacketBuffer& buf,std::string& error) {
    try {
        const auto count=buf.readUint32(),nextCounter=buf.readUint32();
        if(count>9||count>buf.remaining()/4){error="HAM baseline exceeds nine attributes or remaining bytes";current=false;return false;}
        std::vector<int32_t> next;next.reserve(count);
        for(uint32_t i=0;i<count;++i)next.push_back(int32_t(buf.readUint32()));
        values=std::move(next);counter=nextCounter;current=count==9;return true;
    }catch(const std::exception& e){error=e.what();current=false;return false;}
}
bool readHamVectorPatch(HamVectorPatch& patch,soe::PacketBuffer& buf,std::string& error) {
    try {
        auto count=buf.readUint32();patch.counter=buf.readUint32();
        if(count>64||count>buf.remaining()){error="HAM operation count exceeds bound or remaining bytes";return false;}
        std::vector<HamVectorOperation> next;next.reserve(count);
        for(uint32_t i=0;i<count;++i) {
            HamVectorOperation op;op.tag=buf.readByte();
            switch(op.tag) {
                case 0:op.index=buf.readUint16();break;
                case 1:case 2:op.index=buf.readUint16();op.value=int32_t(buf.readUint32());break;
                case 4:break;
                default:error="Unknown HAM operation "+std::to_string(op.tag);return false;
            }
            next.push_back(op);
        }
        patch.operations=std::move(next);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool applyHamVectorPatch(std::vector<int32_t>& values,uint32_t& counter,bool& current,const HamVectorPatch& patch,std::string& error) {
    const uint32_t change=patch.counter-counter;
    // Duplicate/older counters are consumed without regressing state. The
    // same modular ordering handles uint32 wraparound in normal operation.
    if(change==0||change>0x80000000u)return true;
    if(change!=patch.operations.size()){current=false;counter=patch.counter;return true;}
    if(!current){counter=patch.counter;return true;}
    auto next=values;
    for(const auto& op:patch.operations) {
        switch(op.tag) {
            case 0:
                if(op.index>=next.size()){error="HAM removal index out of range";current=false;return false;}
                next.erase(next.begin()+op.index);break;
            case 1:
                if(op.index!=next.size()||next.size()>=9){error="HAM append index or size invalid";current=false;return false;}
                next.push_back(op.value);break;
            case 2:
                if(op.index>=next.size()){error="HAM update index out of range";current=false;return false;}
                next[op.index]=op.value;break;
            case 4:next.clear();break;
            default:error="Unsupported HAM operation";current=false;return false;
        }
    }
    values=std::move(next);counter=patch.counter;current=values.size()==9;return true;
}
bool formatHamVectorPatch(soe::PacketBuffer& buf,std::string& text,std::string& error) {
    HamVectorPatch patch;if(!readHamVectorPatch(patch,buf,error))return false;
    text="<HAM operations="+std::to_string(patch.operations.size())+" counter="+std::to_string(patch.counter)+">";return true;
}
}
