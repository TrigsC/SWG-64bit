#pragma once
#include "OriginalHudAssets.h"
#include "clientcommon/CommandSubmission.h"
#include <map>
#include <optional>

namespace dummyclient {
struct CommandDefinition {
    std::string name,clientHook;
    bool clientOwned=false,disabled=false;
    int targetType=0;
    std::u16string label;
};
struct ChatCommandContext {uint64_t actor=0,target=0;uint16_t mood=0;int32_t language=0;};
struct ParsedCommand {
    std::string name;
    std::u16string arguments,error;
    uint64_t target=0;
    bool empty=false,spatial=false;
};
class CommandCatalog {
public:
    static CommandCatalog load(const HudAssetReader& reader);
    ParsedCommand parse(const std::u16string& text,const ChatCommandContext& context) const;
    std::optional<std::u16string> errorText(const clientcommon::CommandEvent& event) const;
    uint16_t sayType() const{return sayType_;}
    const std::u16string& sayVerb() const{return sayVerb_;}
    const std::map<std::string,CommandDefinition>& definitions() const{return definitions_;}
private:
    std::map<std::string,CommandDefinition> definitions_;
    std::map<uint32_t,std::u16string> labels_;
    std::map<std::string,std::u16string> errors_,stateNames_,locomotionNames_;
    std::map<int,std::string> states_,locomotions_;
    uint16_t sayType_=0;std::u16string sayVerb_;
};
}
