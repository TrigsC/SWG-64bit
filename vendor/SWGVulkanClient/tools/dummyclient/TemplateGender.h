#pragma once
#include "OriginalHudAssets.h"
#include <list>

namespace dummyclient {
// Render-thread-only authored SCOT gender. Never derives gender from a filename.
class TemplateGender {
public:
 explicit TemplateGender(HudAssetReader reader):reader_(std::move(reader)){}
 void beginPass(){passLoads_=0;}
 std::optional<int32_t> effective(const std::string& base,const std::string& alternate={});
 size_t loads()const{return loads_;}
 size_t cachedTemplates()const{return records_.size();}
private:
 struct Record {bool valid=false,creature=false,genderLoaded=false,genderSupported=false;int32_t gender=0;std::string parent;};
 static Record decode(const std::vector<uint8_t>&);
 const Record* load(const std::string&);
 std::optional<int32_t> inherited(const std::string&,std::vector<std::string>&);
 HudAssetReader reader_;std::map<std::string,Record> records_;std::list<std::string> recent_;size_t passLoads_=0,loads_=0;
};
}
