#pragma once
#include "CommandCatalog.h"
#include "clientcommon/TellSubmission.h"
#include "clientcommon/TellPresentation.h"
namespace dummyclient {
struct ParsedTell {
 bool recognized=false,reply=false;std::vector<swgproto::ChatAvatarId> targets;
 std::u16string text,nativeError;std::string errorKey;std::vector<swgproto::ChatAvatarId> duplicates;bool multiple=false;
};
ParsedTell parseTell(const std::u16string&,const CommandCatalog&,const clientcommon::TellSubmission::Context&,const std::optional<std::u16string>& targetedPlayerName={});
}
