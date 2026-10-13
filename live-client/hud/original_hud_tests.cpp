#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "OriginalHudAssets.h"
#include "assets/GameObjectType.h"
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifndef SWG_HUD_ASSET_ROOT
#define SWG_HUD_ASSET_ROOT "./tre_export"
#endif
using namespace dummyclient;
namespace {
using Files=std::map<std::string,std::vector<uint8_t>>;
std::optional<std::vector<uint8_t>> file(const std::string& path) {
    std::ifstream stream(std::string(SWG_HUD_ASSET_ROOT)+"/"+path,std::ios::binary);
    if (!stream) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),{});
}
Files fixtures() {
    Files result;
    for (const auto& p : {"ui/ui_root.ui","ui/ui_ground_hud.inc","ui/ui_styles.inc","ui/ui_effectors.inc","ui/ui_textstylemanager.inc","ui/font/verdana_bold_12.inc","string/en/ui.stf"}) {
        auto bytes=file(p); if (!bytes) throw std::runtime_error(std::string("Missing original fixture: ")+p);
        result.emplace(p,std::move(*bytes));
    }
    return result;
}
HudAssetReader reader(const Files& files) {
    return [&files](const std::string& p)->std::optional<std::vector<uint8_t>> {
        auto found=files.find(p); if (found==files.end()) return {}; return found->second;
    };
}
void replace(Files& files,const std::string& path,const std::string& before,const std::string& after) {
    auto& bytes=files.at(path); std::string text(bytes.begin(),bytes.end());
    auto at=text.find(before); if (at==std::string::npos) throw std::runtime_error("Missing mutation fixture");
    text.replace(at,before.size(),after); bytes.assign(text.begin(),text.end());
}
void put32(std::vector<uint8_t>& b,size_t at,uint32_t value) {
    for (size_t i=0;i<4;++i) b.at(at+i)=uint8_t(value>>(i*8));
}
std::vector<uint8_t> dds() {
    std::vector<uint8_t> bytes(128+24);
    put32(bytes,0,0x20534444);put32(bytes,4,124);put32(bytes,8,0x100f);
    put32(bytes,12,2);put32(bytes,16,2);put32(bytes,20,12);put32(bytes,76,32);
    put32(bytes,80,0x41);put32(bytes,88,32);put32(bytes,92,0xff0000);
    put32(bytes,96,0xff00);put32(bytes,100,0xff);put32(bytes,104,0xff000000);
    bytes[128]=10;bytes[129]=20;bytes[130]=30;bytes[131]=40;
    bytes[132]=100;bytes[133]=110;bytes[134]=120;bytes[135]=130;
    bytes[140]=1;bytes[141]=2;bytes[142]=3;bytes[143]=4;
    return bytes;
}
}
TEST_CASE("Original target resources resolve authored layout, palette, localization and flipped slices") {
    auto assets=OriginalHudAssets::load(file);
    CHECK(assets.targetWidth==255);CHECK(assets.targetHeight==38);CHECK(assets.targetOffsetY==0);
    CHECK(assets.targetPackWidthRatio==doctest::Approx(255.f/1024.f));
    CHECK(assets.targetFrameX==0);CHECK(assets.targetFrameWidth==237);CHECK(assets.targetFrameHeight==38);
    CHECK(assets.targetPostureFrameX==230);CHECK(assets.targetPostureFrameWidth==25);CHECK(assets.targetPostureFrameHeight==29);
    CHECK(assets.targetNameX==50);CHECK(assets.targetNameY==0);CHECK(assets.targetNameWidth==179);CHECK(assets.targetNameHeight==18);
    CHECK(assets.targetLabelX==7);CHECK(assets.targetLabelWidth==42);CHECK(assets.targetLabelHeight==15);
    CHECK(assets.nameColor[0]==doctest::Approx(98.f/255));CHECK(assets.nameColor[1]==1);
    CHECK(assets.labelColor[0]==doctest::Approx(150.f/255));CHECK(assets.frameColor[2]==doctest::Approx(251.f/255));
    CHECK(assets.targetLabel==u"Target:");CHECK(assets.targetLabelFromStf);
    REQUIRE(assets.frameSlices.size()==9);REQUIRE(assets.postureFrameSlices.size()==9);
    const auto& nw=assets.frameSlices.at("nw");
    CHECK(nw.texturePath=="texture/ui_rebel_final.dds");CHECK(nw.source.x0==506);CHECK(nw.source.x1==488);
    CHECK(nw.source.y0==150);CHECK(nw.source.y1==161);
    for (const auto& [_,image]:assets.postureFrameSlices) CHECK(image.texturePath=="texture/ui_rebel_final.dds");
}
TEST_CASE("Original bitmap font preserves advances, preadvance, source coordinates and fallback") {
    auto assets=OriginalHudAssets::load(file);
    CHECK(assets.fontLeading==12);REQUIRE(assets.glyphs.size()==192);
    const auto& bang=assets.glyphs.at('!');CHECK(bang.advance==4);CHECK(bang.advancePre==1);
    CHECK(bang.source.x0==12);CHECK(bang.source.x1==14);CHECK(bang.source.y1==12);
    CHECK(bang.texturePath=="texture/font/verdana_bold_12_000.dds");
    CHECK(assets.glyphs.at(' ').advance==3);CHECK(assets.glyphs.at(0).advancePre==1);
    CHECK(assets.glyphs.at(8364).code==8364);
    auto source=file("texture/font/verdana_bold_12_000.dds");REQUIRE(source);
    auto image=decodeHudRgbaDds(*source);CHECK(image.width==256);CHECK(image.height==256);
    CHECK(image.pixels.size()==256*256*4);CHECK(image.pixels[0]==255);CHECK(image.pixels[3]==0);
    bool ink=false;
    for (int y=bang.source.y0;y<bang.source.y1;++y) for (int x=bang.source.x0;x<bang.source.x1;++x)
        ink|=image.pixels[(y*image.width+x)*4+3]>0;
    CHECK(ink);
}
TEST_CASE("Original self status retains its distinct frame, near-left packing and name alignment") {
    auto assets=OriginalHudAssets::load(file);
    CHECK(assets.selfWidth==255);CHECK(assets.selfHeight==46);CHECK(assets.selfOffsetX==1);CHECK(assets.selfOffsetY==0);
    CHECK(assets.selfPackWidthRatio==doctest::Approx(255.f/1024));
    CHECK(assets.selfFrameX==18);CHECK(assets.selfFrameY==0);CHECK(assets.selfFrameWidth==237);CHECK(assets.selfFrameHeight==46);
    CHECK(assets.selfPostureFrameX==0);CHECK(assets.selfPostureFrameY==0);CHECK(assets.selfPostureFrameWidth==25);CHECK(assets.selfPostureFrameHeight==38);
    CHECK(assets.selfNameX==27);CHECK(assets.selfNameY==0);CHECK(assets.selfNameWidth==215);CHECK(assets.selfNameHeight==19);
    CHECK(assets.selfNameAlignmentVertical=="Center");CHECK(assets.selfNameAlignmentHorizontal=="Left");CHECK_FALSE(assets.selfNameHorizontalAlignmentAuthored);
    CHECK(assets.selfNameColor[0]==doctest::Approx(150.f/255));
    CHECK(assets.selfFrameColor==assets.frameColor);CHECK(assets.selfPostureFrameColor==assets.frameColor);
    REQUIRE(assets.selfFrameSlices.size()==9);REQUIRE(assets.selfPostureFrameSlices.size()==9);
    CHECK(assets.selfFrameSlices.at("nw").source.x0==469);CHECK(assets.selfFrameSlices.at("nw").source.x1==487);
    CHECK(assets.selfFrameSlices.at("nw").source.y0==150);
    CHECK(assets.selfPostureFrameSlices.at("nw").source.x0==490);
}
TEST_CASE("Original HAM rows retain authored nested geometry and intentionally partial rectangle styles") {
    auto assets=OriginalHudAssets::load(file);
    CHECK(assets.selfHam.composite.x==26);CHECK(assets.selfHam.composite.y==20);CHECK(assets.selfHam.composite.width==230);CHECK(assets.selfHam.composite.height==26);
    CHECK(assets.selfHam.ham.x==0);CHECK(assets.selfHam.ham.width==115);CHECK(assets.selfHam.ham.height==25);CHECK(assets.selfHam.ham.packSize=="f,a");
    CHECK(assets.targetHam.composite.x==0);CHECK(assets.targetHam.composite.y==18);CHECK(assets.targetHam.composite.width==229);CHECK(assets.targetHam.composite.height==20);
    CHECK(assets.targetHam.ham.x==114);CHECK(assets.targetHam.ham.width==115);CHECK(assets.targetHam.ham.height==20);CHECK(assets.targetHam.ham.packLocation=="fff,nfn");
    CHECK(assets.selfHam.hasAuthoredForceRow);CHECK_FALSE(assets.targetHam.hasAuthoredForceRow);
    const auto& force=assets.selfHam.rows.at("f");
    CHECK(force.current.appearance.color[0]==doctest::Approx(207.f/255));
    CHECK(force.current.appearance.color[1]==doctest::Approx(17.f/255));
    CHECK(force.current.appearance.color[2]==1);CHECK(force.currentMax.style.slices.empty());
    for (const auto* ham:{&assets.selfHam,&assets.targetHam}) {
        REQUIRE(ham->rows.size()==(ham->hasAuthoredForceRow?4:3));
        int y=0;
        for (const auto* key:{"h","a","m"}) {
            const auto& row=ham->rows.at(key);CHECK(row.layout.x==0);CHECK(row.layout.y==y);CHECK(row.layout.height==6);y+=6;
            CHECK(row.normalMax.layout.packSize=="p,1");CHECK(row.inner.packLocation=="nfn,cfc");
            CHECK(row.current.layout.width==83);CHECK(row.current.layout.height==6);CHECK(row.current.layout.packSize=="p,a");
            CHECK(row.current.appearance.hasColor);CHECK(row.current.appearance.hasBackgroundColor);
            CHECK(row.current.appearance.backgroundColor==std::array<float,4>{1,1,1,1});CHECK(row.current.appearance.backgroundOpacity==1);
            CHECK(row.current.appearance.color==std::array<float,4>{key[0]=='h'?1.f:0.f,key[0]=='a'?1.f:0.f,key[0]=='m'?1.f:0.f,1});
            CHECK(row.currentMax.appearance.hasBackgroundTint);CHECK_FALSE(row.currentMax.appearance.hasColor);
            CHECK(row.currentMax.appearance.backgroundTint[0]==doctest::Approx(10.f/255));
            CHECK(row.currentMax.appearance.backgroundTint[1]==doctest::Approx(54.f/255));
            CHECK(row.currentMax.appearance.backgroundTint[2]==doctest::Approx(69.f/255));
            REQUIRE(row.normalMax.style.slices.size()==1);CHECK(row.normalMax.style.slices.contains("c"));
            REQUIRE(row.current.style.slices.size()==3);CHECK(row.current.style.slices.contains("c"));CHECK(row.current.style.slices.contains("w"));CHECK(row.current.style.slices.contains("e"));
            CHECK_FALSE(row.current.style.slices.contains("nw"));CHECK_FALSE(row.current.style.slices.contains("n"));
            CHECK(row.current.style.slices.at("c").source.x0==489);CHECK(row.current.style.slices.at("c").source.x1==490);
            CHECK(row.normalMax.style.slices.at("c").source.y0==28);CHECK(row.normalMax.style.slices.at("c").source.y1==40);
            CHECK(row.currentMax.style.slices.at("e").source.x0==491);CHECK(row.currentMax.style.slices.at("e").source.y0==28);
            CHECK(row.current.style.color==std::array<float,4>{1,1,1,1});CHECK(row.current.style.opacity==1);
            CHECK(row.current.style.stretchCenterH);CHECK(row.current.style.stretchCenterV);CHECK(row.current.style.shrinkCenterH);CHECK(row.current.style.shrinkCenterV);
            CHECK(row.current.style.stretchWest);CHECK(row.current.style.stretchEast);
        }
    }
    const auto& self=assets.selfHam.rows.at("h");const auto& target=assets.targetHam.rows.at("h");
    CHECK(self.layout.width==115);CHECK(self.normalMax.layout.width==115);CHECK(self.inner.width==115);
    CHECK(self.currentMax.layout.x==101);CHECK(self.currentMax.layout.width==14);CHECK(self.currentMax.layout.height==6);
    CHECK(target.layout.width==113);CHECK(target.normalMax.layout.width==114);CHECK(target.inner.width==114);
    CHECK(target.currentMax.layout.x==100);CHECK(target.currentMax.layout.width==14);CHECK(target.currentMax.layout.height==8);
    CHECK(self.currentMax.layout.minimumHeight==6);CHECK(target.currentMax.layout.minimumHeight==8);
    CHECK(self.currentMax.layout.hasMinimumSize);CHECK(target.currentMax.layout.hasMinimumSize);
}
TEST_CASE("Named UI property paths are case insensitive and optional STF fallback is explicit") {
    auto files=fixtures();
    replace(files,"ui/ui_root.ui","/styles.palette.alpha_blue","/STYLES.PALETTE.ALPHA_BLUE");
    replace(files,"ui/ui_ground_hud.inc","Name='Target'","nAmE='tArGeT'");
    replace(files,"ui/font/verdana_bold_12.inc","Leading='12'","lEaDiNg='12'");
    files.erase("string/en/ui.stf");
    auto assets=OriginalHudAssets::load(reader(files));
    CHECK(assets.targetWidth==255);CHECK(assets.fontLeading==12);CHECK(assets.targetLabel==u"Target:");
    CHECK_FALSE(assets.targetLabelFromStf);
}
TEST_CASE("Named palette bindings override literal prototype colors") {
    auto files=fixtures();
    replace(files,"ui/ui_ground_hud.inc","PalText='contrast1'","PalText='AccentDark'");
    auto assets=OriginalHudAssets::load(reader(files));
    CHECK(assets.nameColor[0]==doctest::Approx(10.f/255));
    CHECK(assets.nameColor[1]==doctest::Approx(54.f/255));
    CHECK(assets.nameColor[2]==doctest::Approx(69.f/255));
    CHECK(assets.labelColor[0]==doctest::Approx(150.f/255));
}
TEST_CASE("Malformed required UI, font and localization assets fail closed within bounded parser") {
    auto files=fixtures();
    SUBCASE("missing root") { files.erase("ui/ui_root.ui"); }
    SUBCASE("truncated property") { files["ui/ui_ground_hud.inc"]={'<','P','a','g','e',' ','N','a','m','e','=','\'','x'}; }
    SUBCASE("unsupported packing") {
        auto& bytes=files.at("ui/ui_ground_hud.inc");std::string text(bytes.begin(),bytes.end());
        auto target=text.find("Name='Target'");REQUIRE(target!=std::string::npos);
        auto pack=text.find("PackLocation='fff,nfn'",target);REQUIRE(pack!=std::string::npos);
        text.replace(pack,std::string("PackLocation='fff,nfn'").size(),"PackLocation='cfc,cfc'");bytes={text.begin(),text.end()};
    }
    SUBCASE("UI depth bound") {
        std::string tree;
        for(int i=0;i<65;++i)tree+="<Namespace>";
        for(int i=0;i<65;++i)tree+="</Namespace>";
        files["ui/ui_styles.inc"]={tree.begin(),tree.end()};
    }
    SUBCASE("UI file size bound") { files["ui/ui_root.ui"].resize(8*1024*1024+1,' '); }
    SUBCASE("font coordinate bound") { replace(files,"ui/font/verdana_bold_12.inc","SourceRect='0,0,7,12'","SourceRect='0,0,999999,12'"); }
    SUBCASE("duplicate code point") { replace(files,"ui/font/verdana_bold_12.inc","Code='32'","Code='0'"); }
    SUBCASE("font traversal") { replace(files,"ui/font/verdana_bold_12.inc","SourceFile='font/verdana_bold_12_000'","SourceFile='../outside'"); }
    SUBCASE("invalid STF") { files["string/en/ui.stf"].resize(24); }
    CHECK_THROWS_AS(OriginalHudAssets::load(reader(files)),std::runtime_error);
}
TEST_CASE("Malformed self and HAM metadata fail rather than supplying a guessed rectangle or tint") {
    auto files=fixtures();
    SUBCASE("missing bar style") { replace(files,"ui/ui_ground_hud.inc","/Styles.mfd.statusbars_small.current.rs_default","/Styles.mfd.statusbars_small.absent.rs_default"); }
    SUBCASE("empty backing image") { replace(files,"ui/ui_styles.inc","SourceRect='493,28,494,40'","SourceRect='493,28,493,40'"); }
    SUBCASE("invalid page opacity") {
        auto& bytes=files.at("ui/ui_ground_hud.inc");std::string text(bytes.begin(),bytes.end());
        auto name=text.find("Name='NormalMax'");REQUIRE(name!=std::string::npos);
        auto page=text.rfind("<Page",name);auto alpha=text.find("BackgroundOpacity='1.00'",page);REQUIRE(alpha<name);
        text.replace(alpha,std::string("BackgroundOpacity='1.00'").size(),"BackgroundOpacity='NaN'");bytes={text.begin(),text.end()};
    }
    SUBCASE("invalid authored bar color") { replace(files,"ui/ui_ground_hud.inc","Color='#FF0000'","Color='#XX0000'"); }
    SUBCASE("unsupported self packing") {
        auto& bytes=files.at("ui/ui_ground_hud.inc");std::string text(bytes.begin(),bytes.end());
        auto self=text.find("Name='MFDStatus'");REQUIRE(self!=std::string::npos);
        auto pack=text.find("PackLocation='nfn,nfn'",self);REQUIRE(pack!=std::string::npos);
        text.replace(pack,std::string("PackLocation='nfn,nfn'").size(),"PackLocation='cfc,nfn'");bytes={text.begin(),text.end()};
    }
    CHECK_THROWS_AS(OriginalHudAssets::load(reader(files)),std::runtime_error);
}
TEST_CASE("DDS font decoder respects BGRA masks and padded row pitch") {
    auto image=decodeHudRgbaDds(dds());CHECK(image.width==2);CHECK(image.height==2);
    CHECK(image.pixels==std::vector<uint8_t>{30,20,10,40,120,110,100,130,3,2,1,4,0,0,0,0});
}
TEST_CASE("DDS font decoder rejects unsupported formats, masks, dimensions and truncated data") {
    auto bytes=dds();
    SUBCASE("compressed atlas") { put32(bytes,80,4);put32(bytes,84,0x35545844); }
    SUBCASE("overlapping channels") { put32(bytes,96,0xff0000); }
    SUBCASE("noncontiguous mask") { put32(bytes,92,0x550000); }
    SUBCASE("missing alpha") { put32(bytes,104,0); }
    SUBCASE("dimension bound") { put32(bytes,16,65535); }
    SUBCASE("short pitch") { put32(bytes,20,4); }
    SUBCASE("truncated row") { bytes.pop_back(); }
    SUBCASE("cubemap") { put32(bytes,112,0x200); }
    CHECK_THROWS_AS(decodeHudRgbaDds(bytes),std::runtime_error);
}

TEST_CASE("posture resources preserve original widgets, flipped UVs and animated fallback") {
    auto a=OriginalHudAssets::load(file);
    CHECK(a.selfPostureIcon.layout.width==24);CHECK(a.selfPostureIcon.layout.height==24);
    CHECK(a.selfPostureIcon.layout.x==0);CHECK(a.selfPostureIcon.layout.y==7);
    CHECK(a.targetPostureIcon.layout.x==0);CHECK(a.targetPostureIcon.layout.y==2);
    CHECK(a.selfPostureIcon.color[0]==doctest::Approx(84.f/255));
    CHECK(a.selfPostureIcon.color==a.targetPostureIcon.color);
    for(size_t i=0;i<15;++i) {
        const bool missing=i==5||i==6||i==10||i==11;
        CHECK(a.postureIcons[i].fallback==missing);
        CHECK(a.postureIcons[i].frames.size()==(missing?6:1));
        for(const auto& f:a.postureIcons[i].frames)CHECK(f.image.texturePath=="texture/ui_rebel_icons.dds");
    }
    const auto& knocked=a.postureIcons[12].frames.at(0).image.source;
    CHECK(knocked.x0==140);CHECK(knocked.x1==164);CHECK(knocked.y0==26);CHECK(knocked.y1==2);
    const auto& dead=a.postureIcons[14].frames.at(0).image.source;
    CHECK(dead.x1-dead.x0==22);CHECK(dead.y1-dead.y0==22);
    // UI clock is shared across style changes. These independently selected
    // 60Hz ticks cover every fallback offset, reversal, empty endpoint, wrap.
    const auto& fallback=a.postureIcons[5];
    for(size_t i=0;i<6;++i) {CHECK(fallback.frames[i].duration==1);CHECK(fallback.frames[i].proportionalX==int(i)*20);}
    CHECK(fallback.frameAt(0)==0);CHECK(fallback.frameAt(3)==1);CHECK(fallback.frameAt(6)==2);
    CHECK(fallback.frameAt(9)==3);CHECK(fallback.frameAt(12)==4);CHECK(fallback.frameAt(15)==5);
    CHECK_FALSE(fallback.frameAt(18));CHECK(fallback.frameAt(21)==3);CHECK(fallback.frameAt(27)==1);CHECK(fallback.frameAt(30)==0);
}

TEST_CASE("original state volume colors and fallback paths retain authored differences") {
    auto a=OriginalHudAssets::load(file);
    CHECK(a.selfStates.volume.width==115);CHECK(a.selfStates.volume.y==3);CHECK(a.selfStates.cellWidth==18);
    CHECK(a.targetStates.volume.width==114);CHECK(a.targetStates.volume.y==0);CHECK(a.targetStates.composite.height==20);
    CHECK(a.selfStates.sampleColor[0]==doctest::Approx(84.f/255));CHECK(a.targetStates.sampleColor[0]==1);
    REQUIRE(a.stateColors[1]);CHECK((*a.stateColors[1])[0]==1);CHECK((*a.stateColors[1])[1]==0);
    CHECK_FALSE(a.stateColors[13]);CHECK(a.stateIcons[20].fallback);CHECK(a.stateIcons[21].fallback);CHECK(a.stateIcons[35].fallback);
    REQUIRE(a.stateColors[35]);CHECK((*a.stateColors[35])[1]==1);CHECK((*a.stateColors[35])[0]==0);
    CHECK(a.stateIcons[29].frames[0].image.texturePath=="texture/ui_space_hud.dds");
    CHECK(a.stateFadeSpeed==8);CHECK_FALSE(a.stateIcons[34].fallback);CHECK_FALSE(a.stateIcons[36].fallback);
}
TEST_CASE("vehicle category reads explicit integers and bounded DERV inheritance") {
    CHECK(assets::gameObjectType("object/mobile/vehicle/shared_speederbike.iff",file)==0x10001);
    CHECK(assets::gameObjectType("object/mobile/vehicle/shared_landspeeder_x34.iff",file)==0x10001);
    CHECK(assets::gameObjectType("object/mobile/vehicle/shared_vehicle_base.iff",file)==0x400);
    CHECK_FALSE(assets::gameObjectType("object/mobile/vehicle/missing.iff",file));
    auto parent=file("object/mobile/vehicle/shared_landspeeder_base.iff");REQUIRE(parent);
    std::string key="gameObjectType";auto at=std::search(parent->begin(),parent->end(),key.begin(),key.end());REQUIRE(at!=parent->end());
    *(at+key.size()+1)=2;
    CHECK_FALSE(assets::gameObjectType("object/mobile/vehicle/shared_speederbike.iff",[&](auto name){return name=="object/mobile/vehicle/shared_landspeeder_base.iff"?parent:file(name);}));
    CHECK_FALSE(assets::gameObjectType("object/mobile/vehicle/shared_speederbike.iff",[](auto)->std::optional<std::vector<uint8_t>>{throw std::runtime_error("missing archive");}));
}
