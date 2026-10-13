#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <type_traits>
#include "assets/TreArchive.h"
#include "assets/StaticMesh.h"
#include "assets/LodFile.h"
#include "assets/AppearanceTemplate.h"
#include "assets/SharedObjectTemplate.h"
#include "assets/BuildingLayout.h"
#include "assets/SkeletalAppearance.h"
#include "assets/SkeletalMeshLod.h"
#include "assets/SkeletalMesh.h"
#include "assets/Skeleton.h"
#include "assets/ShaderTemplate.h"
#include "assets/DdsImage.h"
#include "terrain/TrnFile.h"
#include "terrain/TerrainGenerator.h"
#include "terrain/TerrainMesh.h"
#include "terrain/Boundary.h"

namespace fs = std::filesystem;
using assets::Float3;
std::string quote(const std::string& s) {
    std::string r="\"";
    for (unsigned char c:s) { if(c=='"'||c=='\\') r+='\\'; if(c=='\n') r+="\\n"; else if(c>=32) r+=c; }
    return r+'"';
}
struct Archive { std::string name; std::unique_ptr<assets::TreArchive> reader; };
struct Store {
    std::vector<Archive> archives;
    std::map<std::string,size_t> index;
    std::map<std::string,size_t> used;
    explicit Store(const fs::path& root) {
        std::vector<std::pair<int,std::string>> names;
        std::ifstream cfg(root/"swgemu_live.cfg"); std::string line;
        std::regex pattern(R"(searchTree_[0-9]+_([0-9]+)\s*=\s*([^\s]+\.tre))");
        while(std::getline(cfg,line)) { std::smatch m; if(std::regex_search(line,m,pattern)) names.push_back({std::stoi(m[1]),m[2]}); }
        std::stable_sort(names.begin(),names.end(),[](auto& a,auto& b){return a.first>b.first;});
        std::set<std::string> seen;
        for(auto& [priority,name]:names) if(seen.insert(name).second) {
            if(!fs::exists(root/name)) throw std::runtime_error("Configured archive missing: "+name);
            archives.push_back({name,std::make_unique<assets::TreArchive>((root/name).string())});
        }
        if(archives.empty()) throw std::runtime_error("No archive search order found in swgemu_live.cfg");
        for(size_t i=0;i<archives.size();++i) for(auto& name:archives[i].reader->listFiles()) index.try_emplace(name,i);
        std::cout<<"Indexed "<<index.size()<<" assets from "<<archives.size()<<" archives\n";
    }
    std::vector<uint8_t> get(std::string name) {
        std::replace(name.begin(),name.end(),'\\','/');
        auto it=index.find(name); if(it==index.end()) throw std::runtime_error("Asset missing: "+name);
        used[name]=it->second; return archives[it->second].reader->extract(name);
    }
};
// A tightly packed, API-independent vertex. Metal's shader uses packed vector fields.
struct Vertex { Float3 p,n; assets::Float2 uv; Float3 color; };
static_assert(sizeof(Vertex)==44);
struct Draw {
    std::string label,texturePath;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t width=1,height=1;
    std::vector<uint8_t> rgba={255,255,255,255};
};
struct Scene { std::string name,source,note; std::vector<Draw> draws; Float3 center; float radius=1; };
using RGBA=std::array<uint8_t,4>;
RGBA rgb565(uint16_t v) { return {uint8_t(((v>>11)&31)*255/31),uint8_t(((v>>5)&63)*255/63),uint8_t((v&31)*255/31),255}; }
std::vector<uint8_t> decode(const assets::DdsImageData& image) {
    const bool bc1=image.format==assets::DdsBlockFormat::Bc1;
    const bool bc2=image.format==assets::DdsBlockFormat::Bc2;
    const size_t stride=bc1?8:16, nx=(image.width+3)/4, ny=(image.height+3)/4;
    if(!image.width || !image.height || image.width>8192 || image.height>8192 || image.blockData.size()!=nx*ny*stride)
        throw std::runtime_error("Invalid DDS base mip size");
    std::vector<uint8_t> out(size_t(image.width)*image.height*4);
    for(size_t by=0;by<ny;++by) for(size_t bx=0;bx<nx;++bx) {
        auto b=image.blockData.data()+(by*nx+bx)*stride; auto c=b+(bc1?0:8);
        uint16_t c0=c[0]|(c[1]<<8),c1=c[2]|(c[3]<<8);
        std::array<RGBA,4> palette={rgb565(c0),rgb565(c1),RGBA{},RGBA{}};
        for(int k=0;k<3;++k) {
            if(c0>c1||!bc1) {palette[2][k]=(2*palette[0][k]+palette[1][k])/3; palette[3][k]=(palette[0][k]+2*palette[1][k])/3;}
            else {palette[2][k]=(palette[0][k]+palette[1][k])/2; palette[3][k]=0;}
        }
        palette[2][3]=255; palette[3][3]=(c0>c1||!bc1)?255:0;
        uint32_t bits=uint32_t(c[4])|(uint32_t(c[5])<<8)|(uint32_t(c[6])<<16)|(uint32_t(c[7])<<24);
        std::array<uint8_t,8> alpha{}; uint64_t alphaBits=0;
        if(!bc1&&!bc2) {
            alpha[0]=b[0]; alpha[1]=b[1];
            if(b[0]>b[1]) for(int k=2;k<8;++k) alpha[k]=((8-k)*b[0]+(k-1)*b[1])/7;
            else {for(int k=2;k<6;++k) alpha[k]=((6-k)*b[0]+(k-1)*b[1])/5; alpha[6]=0; alpha[7]=255;}
            for(int k=0;k<6;++k) alphaBits|=uint64_t(b[2+k])<<(8*k);
        }
        for(size_t y=0;y<4;++y) for(size_t x=0;x<4;++x) {
            size_t n=y*4+x,px=bx*4+x,py=by*4+y; if(px>=image.width||py>=image.height) continue;
            auto color=palette[(bits>>(2*n))&3];
            if(bc2) color[3]=((b[n/2]>>(4*(n%2)))&15)*17;
            else if(!bc1) color[3]=alpha[(alphaBits>>(3*n))&7];
            std::copy(color.begin(),color.end(),out.begin()+(py*image.width+px)*4);
        }
    }
    return out;
}
void texture(Store& store,Draw& draw,const std::string& shader) {
    if(shader.empty()) return;
    auto material=assets::ShaderTemplate::parse(store.get(shader));
    if(material.mainTextureFilename.empty()) return;
    draw.texturePath=material.mainTextureFilename;
    auto image=assets::DdsImage::parse(store.get(draw.texturePath));
    draw.width=image.width; draw.height=image.height; draw.rgba=decode(image);
}
template<class Mesh> Draw meshDraw(Store& store,const std::string& name,const std::string& shader,const Mesh& m) {
    Draw d; d.label=name; d.indices=m.indices;
    if(m.positions.empty()||d.indices.empty()||d.indices.size()%3) throw std::runtime_error("Empty or malformed mesh: "+name);
    for(size_t i=0;i<m.positions.size();++i) {
        auto p=m.positions[i]; auto n=i<m.normals.size()?m.normals[i]:Float3{0,1,0};
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) throw std::runtime_error("Non-finite geometry");
        d.vertices.push_back({p,n,i<m.uv0.size()?m.uv0[i]:assets::Float2{}, {1,1,1}});
    }
    for(auto i:d.indices) if(i>=d.vertices.size()) throw std::runtime_error("Mesh index out of bounds");
    texture(store,d,shader); return d;
}
void bounds(Scene& scene) {
    Float3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
    for(auto& d:scene.draws) for(auto& v:d.vertices) {lo={std::min(lo.x,v.p.x),std::min(lo.y,v.p.y),std::min(lo.z,v.p.z)}; hi={std::max(hi.x,v.p.x),std::max(hi.y,v.p.y),std::max(hi.z,v.p.z)};}
    scene.center={(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2};
    float x=hi.x-lo.x,y=hi.y-lo.y,z=hi.z-lo.z; scene.radius=std::sqrt(x*x+y*y+z*z)/2;
    if(!std::isfinite(scene.radius)||scene.radius<=0) throw std::runtime_error("Invalid scene bounds");
}
Scene terrainScene(Store& store) {
    Scene scene{"Tatooine terrain","terrain/tatooine.trn","384 m patch at world (-1200, 600). Asset-derived height and color; no flora or terrain texture splatting."};
    auto trn=terrain::TrnFile::parse(store.get(scene.source));
    auto gen=terrain::TerrainGenerator::parse(trn.generatorForm());
    // Patched terrain and its color ramps can reside in different archives.
    // Resolve every dependency through the same configured search order.
    auto loadRamps=[&](auto&& self,const std::vector<terrain::Layer>& layers)->void {
        for(auto& layer:layers) {
            for(auto& affector:layer.affectors) std::visit([&](const auto& a) {
                using T=std::decay_t<decltype(a)>;
                if constexpr(std::is_same_v<T,terrain::AffectorColorRampHeight>||std::is_same_v<T,terrain::AffectorColorRampFractal>) {
                    if(!gen.rampImageCache.contains(a.imageName)) gen.rampImageCache.emplace(a.imageName,assets::TgaImage::parse(store.get(a.imageName)));
                }
            },affector.affector);
            self(self,layer.subLayers);
        }
    };
    loadRamps(loadRamps,gen.topLevelLayers);
    terrain::ChunkHeightData chunk; chunk.verticesPerSide=129; chunk.tileWidthInMeters=3;
    chunk.worldOriginX=-1392; chunk.worldOriginZ=408;
    for(uint32_t z=0;z<129;++z) for(uint32_t x=0;x<129;++x) {
        float wx=chunk.worldOriginX+x*3,wz=chunk.worldOriginZ+z*3;
        chunk.heights.push_back(gen.queryHeight(wx,wz)); chunk.colors.push_back(gen.queryColor(wx,wz));
    }
    auto m=terrain::buildChunkMesh(chunk); Draw d; d.label=scene.source; d.indices=m.indices;
    for(auto& v:m.vertices) d.vertices.push_back({v.position,v.normal,{0,0},v.color});
    scene.draws.push_back(std::move(d)); bounds(scene); return scene;
}
void staticAppearance(Store& store,Scene& scene,std::string path,int depth=0) {
    if(depth>6) throw std::runtime_error("Appearance reference cycle");
    if(path.ends_with(".apt")) return staticAppearance(store,scene,assets::AppearanceTemplate::parse(store.get(path)).referencedFilename,depth+1);
    if(path.ends_with(".lod")) {auto p=assets::LodFile::parse(store.get(path)).highestDetailMeshFilename; return staticAppearance(store,scene,p.starts_with("appearance/")?p:"appearance/"+p,depth+1);}
    if(!path.ends_with(".msh")) throw std::runtime_error("Unsupported building appearance: "+path);
    for(auto& m:assets::StaticMesh::parse(store.get(path))) scene.draws.push_back(meshDraw(store,path,m.shaderFilename,m.mesh));
}
Scene buildingScene(Store& store) {
    Scene scene{"Association hall","appearance/ply_all_assoc_hall_civ_s01.pob","Full visual building: exterior and interior cells. Orbit outside; zoom through walls to inspect rooms."};
    auto building=assets::BuildingLayout::parse(store.get(scene.source));
    for(auto& cell:building.cells) staticAppearance(store,scene,cell.modelFilename);
    bounds(scene); return scene;
}
Scene characterScene(Store& store) {
    const std::string object="object/creature/player/shared_human_male.iff";
    auto tmpl=assets::SharedObjectTemplate::parse(store.get(object));
    Scene scene{"Human male",tmpl.appearanceFilename,"Original character meshes in bind pose. Diffuse textures only; no animation, dye palettes, or player customization."};
    auto sat=assets::SkeletalAppearance::parse(store.get(scene.source));
    auto skeleton=assets::Skeleton::parse(store.get(sat.skeletonFilename));
    if(skeleton.bones.empty()) throw std::runtime_error("Character skeleton is empty");
    for(auto& filename:sat.meshFilenames) {
        auto lod=assets::SkeletalMeshLod::parse(store.get(filename));
        auto mgn=assets::SkeletalMesh::parse(store.get(lod.highestDetailMeshFilename));
        for(auto& m:mgn.submeshes) scene.draws.push_back(meshDraw(store,lod.highestDetailMeshFilename,m.shaderFilename,m));
    }
    bounds(scene); return scene;
}
template<class T> void write(std::ofstream& file,const T& value) { file.write(reinterpret_cast<const char*>(&value),sizeof(T)); }
void writeString(std::ofstream& file,const std::string& s) { write(file,uint32_t(s.size())); file.write(s.data(),s.size()); }
void save(const std::vector<Scene>& scenes,Store& store,const fs::path& out) {
    fs::create_directories(out); auto temporary=out/"scene.bin.tmp";
    std::ofstream file(temporary,std::ios::binary); file.exceptions(std::ios::failbit|std::ios::badbit);
    file.write("SWGVIEW1",8); write(file,uint32_t(scenes.size()));
    std::ostringstream manifest; manifest<<"{\n  \"format\":1,\n  \"renderer\":\"Native ARM64 Metal\",\n  \"scenes\":[\n";
    for(size_t s=0;s<scenes.size();++s) {
        auto& scene=scenes[s]; writeString(file,scene.name); writeString(file,scene.source); writeString(file,scene.note);
        write(file,scene.center); write(file,scene.radius); write(file,uint32_t(scene.draws.size()));
        size_t verts=0,triangles=0,textured=0;
        for(auto& d:scene.draws) {
            writeString(file,d.label); write(file,uint32_t(d.vertices.size())); write(file,uint32_t(d.indices.size()));
            write(file,d.width); write(file,d.height); write(file,uint32_t(d.rgba.size()));
            file.write(reinterpret_cast<const char*>(d.vertices.data()),d.vertices.size()*sizeof(Vertex));
            file.write(reinterpret_cast<const char*>(d.indices.data()),d.indices.size()*4);
            file.write(reinterpret_cast<const char*>(d.rgba.data()),d.rgba.size());
            verts+=d.vertices.size(); triangles+=d.indices.size()/3; textured+=!d.texturePath.empty();
        }
        manifest<<"    {\"name\":"<<quote(scene.name)<<",\"source\":"<<quote(scene.source)<<",\"note\":"<<quote(scene.note)<<",\"draws\":"<<scene.draws.size()<<",\"vertices\":"<<verts<<",\"triangles\":"<<triangles<<",\"textured_draws\":"<<textured<<",\"radius\":"<<scene.radius<<"}"<<(s+1<scenes.size()?",":"")<<"\n";
        std::cout<<scene.name<<": "<<triangles<<" triangles, "<<textured<<" textured draws\n";
    }
    file.close(); fs::rename(temporary,out/"scene.bin");
    manifest<<"  ],\n  \"assets\":[\n"; size_t n=0;
    for(auto& [path,index]:store.used) manifest<<"    {\"path\":"<<quote(path)<<",\"archive\":"<<quote(store.archives[index].name)<<"}"<<(++n<store.used.size()?",":"")<<"\n";
    manifest<<"  ]\n}\n"; std::ofstream(out/"manifest.json")<<manifest.str();
}
void decoderTest() {
    assets::DdsImageData image; image.width=image.height=4; image.format=assets::DdsBlockFormat::Bc1;
    image.blockData={0,248,0,0,0,0,0,0}; auto red=decode(image);
    for(size_t i=0;i<16;++i) if(red[4*i]!=255||red[4*i+1]||red[4*i+2]||red[4*i+3]!=255) throw std::runtime_error("BC1 red fixture failed");
    image.blockData={0,0,0,248,255,255,255,255};
    auto transparent=decode(image); for(size_t i=0;i<16;++i) if(transparent[4*i+3]!=0) throw std::runtime_error("BC1 transparent fixture failed");
    image.format=assets::DdsBlockFormat::Bc2;
    image.blockData={0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0,248,0,0,0,0,0,0};
    auto explicitAlpha=decode(image); for(size_t i=0;i<16;++i) if(explicitAlpha[4*i+3]!=(i%2?255:0)) throw std::runtime_error("BC2 alpha fixture failed");
    image.format=assets::DdsBlockFormat::Bc3; image.blockData={0,255,0,0,0,0,0,0,0,248,0,0,0,0,0,0};
    auto alpha=decode(image); for(size_t i=0;i<16;++i) if(alpha[4*i+3]!=0) throw std::runtime_error("BC3 alpha fixture failed");
    // Pixel zero uses alpha palette entry 2, interpolated from endpoints 255/0.
    image.blockData[0]=255; image.blockData[1]=0; image.blockData[2]=2;
    auto interpolated=decode(image); if(interpolated[3]!=218||interpolated[7]!=255) throw std::runtime_error("BC3 interpolation fixture failed");
    image.width=3; image.height=2;
    if(decode(image).size()!=24) throw std::runtime_error("Partial DDS block fixture failed");
    image.blockData.pop_back(); bool rejected=false; try {decode(image);} catch(...) {rejected=true;} if(!rejected) throw std::runtime_error("Truncated DDS accepted");
    std::cout<<"DDS decoding fixtures passed\n";
}
void terrainVersionTest() {
    const auto form=[](uint32_t tag,std::vector<assets::IffChunk> children) {
        assets::IffChunk f; f.id=assets::kFormTag; f.formType=tag; f.children=std::move(children); return f;
    };
    assets::IffChunk headerData; headerData.id=0x44415441;
    headerData.data.writeUint32(1); headerData.data.writeByte(0);
    auto header=form(0x49484452,{form(0x30303031,{headerData})});
    assets::IffChunk payload; payload.id=0x44415441;
    // A real rectangle prefix from patch_01.tre's terrain/tatooine.trn.
    const std::vector<uint8_t> bytes={0x00,0x00,0x05,0xc6,0x00,0x00,0xe4,0xc5,0x00,0x00,0x91,0xc5,0x00,0x00,0xf0,0xc4,
        3,0,0,0,0x1b,0x2f,0x5d,0x3e,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x40,0,0};
    payload.data.writeBytes(bytes);
    auto rectangle=form(0x42524543,{form(0x30303033,{header,payload})});
    auto parsed=terrain::parseBoundary(rectangle);
    auto shape=std::get<terrain::BoundaryRectangle>(parsed->shape);
    if(shape.x0!=-8512||shape.y0!=-7296||shape.x1!=-4640||shape.y1!=-1920||shape.featherFunction!=3
       || std::abs(shape.featherDistance-0.216f)>0.001f) throw std::runtime_error("Patched rectangle fixture failed");
    rectangle.children[0].formType=0x30303032;
    if(!terrain::parseBoundary(rectangle)) throw std::runtime_error("Legacy rectangle fixture failed");
    rectangle.children[0].formType=0x30303034;
    bool rejected=false; try {terrain::parseBoundary(rectangle);} catch(...) {rejected=true;}
    if(!rejected) throw std::runtime_error("Unknown rectangle version accepted");
    rectangle.children[0].formType=0x30303033;
    rectangle.children[0].children[1].data=soe::PacketBuffer(bytes.data(),24);
    rejected=false; try {terrain::parseBoundary(rectangle);} catch(...) {rejected=true;}
    if(!rejected) throw std::runtime_error("Truncated patched rectangle accepted");
    std::cout<<"Patched terrain version fixtures passed\n";
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--self-test") {decoderTest(); terrainVersionTest(); return 0;}
        if(argc==5&&std::string(argv[2])=="--extract") {
            Store store(argv[1]); auto bytes=store.get(argv[3]);
            std::ofstream file(argv[4],std::ios::binary); file.exceptions(std::ios::failbit|std::ios::badbit);
            file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
            std::cout<<argv[3]<<" from "<<store.archives[store.index.at(argv[3])].name<<", "<<bytes.size()<<" bytes\n"; return 0;
        }
        if(argc!=3) throw std::runtime_error("Usage: asset-exporter CLIENT_DIRECTORY OUTPUT_DIRECTORY");
        Store store(argv[1]); std::vector<Scene> scenes;
        scenes.push_back(terrainScene(store)); scenes.push_back(buildingScene(store)); scenes.push_back(characterScene(store));
        save(scenes,store,argv[2]); return 0;
    } catch(const std::exception& e) {std::cerr<<"Asset export failed: "<<e.what()<<'\n'; return 1;}
}
