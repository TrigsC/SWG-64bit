#include <renderer/VulkanRenderer.h>
#include <renderer/Camera.h>
#include <animation/SkeletalPose.h>
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace DirectX;
namespace fs=std::filesystem;
struct Reader {
    std::ifstream file;
    explicit Reader(const fs::path& p):file(p,std::ios::binary) {file.exceptions(std::ios::badbit|std::ios::failbit);}
    void bytes(void* p,size_t n) {file.read(static_cast<char*>(p),n);}
    uint32_t u32() {uint32_t n;bytes(&n,4);return n;}
    float real() {float n;bytes(&n,4);if(!std::isfinite(n)) throw std::runtime_error("Non-finite cache value");return n;}
    std::string string() {auto n=u32();if(n>1000000)throw std::runtime_error("Invalid string");std::string s(n,'\0');bytes(s.data(),n);return s;}
};
struct Draw {assets::MeshData mesh;renderer::MeshHandle gpu;renderer::DynamicMeshHandle dynamic;};
struct Scene {
    std::string name,source,note;
    XMFLOAT3 center;float radius;
    std::vector<Draw> draws;
    terrain::TerrainMeshData terrain;
    renderer::TerrainChunkHandle terrainGPU;
    renderer::TextureHandle label;
    int labelWidth=0,labelHeight=0;
};
std::vector<Scene> loadScenes(const fs::path& path,renderer::VulkanRenderer& gfx) {
    Reader r(path);char magic[8];r.bytes(magic,8);
    if(std::memcmp(magic,"SWGVIEW1",8)||r.u32()!=3)throw std::runtime_error("Expected SWGVIEW1 cache with three scenes");
    std::vector<Scene> scenes;
    for(int s=0;s<3;++s) {
        Scene scene;scene.name=r.string();scene.source=r.string();scene.note=r.string();
        scene.center={r.real(),r.real(),r.real()};scene.radius=r.real();auto nd=r.u32();
        if(scene.radius<=0||nd==0||nd>10000)throw std::runtime_error("Invalid scene bounds");
        for(uint32_t d=0;d<nd;++d) {
            auto name=r.string();auto nv=r.u32(),ni=r.u32(),w=r.u32(),h=r.u32(),nb=r.u32();
            if(!nv||nv>10000000||!ni||ni%3||ni>30000000||!w||!h||w>8192||h>8192||nb!=uint64_t(w)*h*4)throw std::runtime_error("Invalid draw dimensions");
            Draw draw;std::vector<assets::Float3> colors;
            for(uint32_t i=0;i<nv;++i) {
                draw.mesh.positions.push_back({r.real(),r.real(),r.real()});
                draw.mesh.normals.push_back({r.real(),r.real(),r.real()});
                draw.mesh.uv0.push_back({r.real(),r.real()});
                colors.push_back({r.real(),r.real(),r.real()});
            }
            for(uint32_t i=0;i<ni;++i) {auto idx=r.u32();if(idx>=nv)throw std::runtime_error("Index out of bounds");draw.mesh.indices.push_back(idx);}
            std::vector<uint8_t> pixels(nb);r.bytes(pixels.data(),nb);
            if(s==0) {
                for(uint32_t i=0;i<nv;++i) scene.terrain.vertices.push_back({draw.mesh.positions[i],draw.mesh.normals[i],colors[i]});
                scene.terrain.indices=draw.mesh.indices;
                scene.terrainGPU=gfx.loadTerrainChunk(scene.terrain);
            } else {
                auto texture=gfx.loadTextureRGBA8(w,h,pixels);
                if(s==2) {draw.dynamic=gfx.allocateDynamicMesh(nv,ni);draw.dynamic.textureDescriptorSet=texture.descriptorSet;}
                else {draw.gpu=gfx.loadStaticMesh(draw.mesh);draw.gpu.textureDescriptorSet=texture.descriptorSet;}
                scene.draws.push_back(std::move(draw));
            }
        }
        std::wstring title(scene.name.begin(),scene.name.end());
        scene.label=gfx.createTextTexture(title,scene.labelWidth,scene.labelHeight);
        scenes.push_back(std::move(scene));
    }
    if(r.file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Trailing cache bytes");
    return scenes;
}
void be32(std::vector<uint8_t>& out,uint32_t n) {for(int k=3;k>=0;--k)out.push_back(uint8_t(n>>(8*k)));}
void chunk(std::vector<uint8_t>& out,const char* name,const std::vector<uint8_t>& data) {
    be32(out,data.size());size_t start=out.size();out.insert(out.end(),name,name+4);out.insert(out.end(),data.begin(),data.end());
    be32(out,uint32_t(crc32(0,out.data()+start,out.size()-start)));
}
void png(const fs::path& path,const std::vector<uint8_t>& rgba,uint32_t w,uint32_t h) {
    std::vector<uint8_t> scan;
    for(uint32_t y=0;y<h;++y) {scan.push_back(0);scan.insert(scan.end(),rgba.begin()+size_t(y)*w*4,rgba.begin()+size_t(y+1)*w*4);}
    uLongf n=compressBound(scan.size());std::vector<uint8_t> compressed(n);
    if(compress2(compressed.data(),&n,scan.data(),scan.size(),6)!=Z_OK)throw std::runtime_error("PNG compression failed");compressed.resize(n);
    std::vector<uint8_t> out={137,80,78,71,13,10,26,10},header;be32(header,w);be32(header,h);header.insert(header.end(),{8,6,0,0,0});
    chunk(out,"IHDR",header);chunk(out,"IDAT",compressed);chunk(out,"IEND",{});
    std::ofstream f(path,std::ios::binary);f.exceptions(std::ios::badbit|std::ios::failbit);f.write(reinterpret_cast<const char*>(out.data()),out.size());
}
void verifyBCSampling(renderer::VulkanRenderer& gfx,const fs::path& output) {
    std::vector<renderer::MeshHandle> quads;
    const uint16_t colors[]={0xF800,0x07E0,0x001F};
    for(int i=0;i<3;++i) {
        assets::DdsImageData d;d.width=d.height=4;
        d.format=i==0?assets::DdsBlockFormat::Bc1:i==1?assets::DdsBlockFormat::Bc2:assets::DdsBlockFormat::Bc3;
        if(i==1)d.blockData=std::vector<uint8_t>(8,255);
        if(i==2)d.blockData={255,0,0,0,0,0,0,0};
        d.blockData.insert(d.blockData.end(),{uint8_t(colors[i]&255),uint8_t(colors[i]>>8),0,0,0,0,0,0});
        auto texture=gfx.loadTexture(d);
        assets::MeshData m;float x=(i-1)*.6f;
        m.positions={{x-.2f,-.2f,1},{x+.2f,-.2f,1},{x+.2f,.2f,1},{x-.2f,.2f,1}};
        m.normals=std::vector<assets::Float3>(4,{0,0,-1});m.uv0={{0,0},{1,0},{1,1},{0,1}};m.indices={0,1,2,0,2,3};
        auto mesh=gfx.loadStaticMesh(m);mesh.textureDescriptorSet=texture.descriptorSet;quads.push_back(mesh);
    }
    gfx.waitForUploads();
    gfx.beginFrame(.045f,.065f,.085f);gfx.setViewProjection(XMMatrixIdentity(),XMMatrixOrthographicLH(2,1.5f,.01f,10));
    for(auto& q:quads)gfx.drawMesh(q,{0,0,0},0,{1,1,1,1});
    gfx.requestFrameCapture();gfx.endFrame();
    std::vector<uint8_t> pixels;uint32_t w=0,h=0;
    if(!gfx.captureFrameRGBA8(pixels,w,h))throw std::runtime_error("BC sampling capture failed");
    for(int i=0;i<3;++i) {
        size_t p=(size_t(h/2)*w+size_t(w*(.2f+.3f*i)))*4;
        for(int c=0;c<3;++c) if((c==i&&pixels[p+c]<100)||(c!=i&&pixels[p+c]>5))throw std::runtime_error("BC sampling color mismatch");
    }
    png(output/"compressed-textures.png",pixels,w,h);std::cout<<"BC1/BC2/BC3 texture sampling verified on GPU"<<std::endl;
}
void reset(renderer::FollowCamera& camera,const Scene& scene,int selected) {
    camera.yaw=XM_PI+0.6f;camera.pitch=selected==0?-0.58f:selected==1?-0.32f:-0.12f;
    camera.distance=scene.radius*2.9f;camera.minDistance=scene.radius*.08f;camera.maxDistance=scene.radius*15;
    camera.zoomPerWheelNotch=scene.radius*.12f;camera.targetHeightOffset=0;
}
int main(int argc,char** argv) {
    try {
        const char* scenePath=argc>1?argv[1]:SWG_SCENE_CACHE;
        setenv("VK_DRIVER_FILES",SWG_MOLTENVK_ICD,0);
        bool smoke=argc==4&&std::string(argv[2])=="--smoke";
        renderer::Window window(1000,740,L"SWG Vulkan / Metal — Offline | 1 terrain  2 building  3 character | Drag orbit · wheel zoom · R reset · Esc quit");
        renderer::VulkanRenderer gfx(window.handle(),window.width(),window.height());
        std::cout<<gfx.capabilityReport()<<std::endl;
        auto scenes=loadScenes(scenePath,gfx);
        gfx.waitForUploads();
        renderer::FollowCamera camera;int selected=0,frames=0;reset(camera,scenes[0],0);
        auto previous=std::chrono::steady_clock::now();
        fs::path out=smoke?argv[3]:"";if(smoke){fs::create_directories(out);verifyBCSampling(gfx,out);}
        std::ofstream report;if(smoke){report.open(out/"render-report.json");report<<"{\"backend\":\"Vulkan via MoltenVK\",\"architecture\":\"arm64\",\"capabilities\":"<<gfx.capabilityReport()<<",\"bc_sampling_verified\":true,\"captures\":[";}
        int captures=0;
        while(window.pumpMessages()) {
            if(window.takeKeyPress(renderer::Window::Escape))break;
            if(window.width()<=0||window.height()<=0){std::this_thread::sleep_for(std::chrono::milliseconds(15));continue;}
            gfx.resize(window.width(),window.height());
            for(int key=0;key<4;++key)if(window.takeKeyPress(key==3?'R':'1'+key)){if(key<3)selected=key;reset(camera,scenes[selected],selected);}
            auto now=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(now-previous).count();previous=now;
            auto& scene=scenes[selected];
            // This offline inspector also accepts a left drag. Keep the
            // live client's right-button camera/left-button picking API intact.
            {
                int dx=0,dy=0;window.takeMouseDragDelta(0,dx,dy);
                camera.yaw+=dx*camera.mouseSensitivity;
                camera.pitch=std::clamp(camera.pitch-dy*camera.mouseSensitivity,-XM_PIDIV2+.01f,XM_PIDIV2-.01f);
            }
            camera.update(window,scene.center,dt);
            gfx.beginFrame(.045f,.065f,.085f);
            gfx.setViewProjection(camera.viewMatrix(scene.center),XMMatrixPerspectiveFovLH(XM_PI/4,float(gfx.renderWidth())/gfx.renderHeight(),scene.radius*.002f,scene.radius*40));
            if(selected==0)gfx.drawTerrainChunk(scene.terrainGPU);
            else for(auto& draw:scene.draws) {
                if(selected==2){gfx.updateDynamicMesh(draw.dynamic,draw.mesh);gfx.drawDynamicMesh(draw.dynamic,{0,0,0},0,{1,1,1,1});}
                else gfx.drawMesh(draw.gpu,{0,0,0},0,{1,1,1,1});
            }
            gfx.beginLabelPass();
            float scale=scene.radius*1.1f/scene.labelWidth;
            auto direction=camera.lookDirection();auto forward=XMLoadFloat3(&direction);
            auto right=XMVector3Normalize(XMVector3Cross(XMVectorSet(0,1,0,0),forward));
            XMFLOAT3 labelRight,labelUp;XMStoreFloat3(&labelRight,right);XMStoreFloat3(&labelUp,XMVector3Cross(forward,right));
            gfx.drawLabel({scene.center.x,scene.center.y+scene.radius*.9f,scene.center.z},scale*scene.labelWidth,scale*scene.labelHeight,labelRight,labelUp,scene.label);
            bool capture=smoke&&frames==4;
            if(capture)gfx.requestFrameCapture();gfx.endFrame();++frames;
            if(capture) {
                std::vector<uint8_t> pixels;uint32_t w=0,h=0;
                if(!gfx.captureFrameRGBA8(pixels,w,h))throw std::runtime_error("Capture failed");
                size_t foreground=0;for(size_t i=0;i<pixels.size();i+=4)if(pixels[i]!=pixels[0]||pixels[i+1]!=pixels[1]||pixels[i+2]!=pixels[2])++foreground;
                if(foreground<1000)throw std::runtime_error("Empty render: "+scene.name);
                auto name=selected==0?"terrain":selected==1?"building":"character";png(out/(std::string(name)+".png"),pixels,w,h);
                if(captures++)report<<",";report<<"{\"scene\":\""<<name<<"\",\"width\":"<<w<<",\"height\":"<<h<<",\"foreground_pixels\":"<<foreground<<"}";
                std::cout<<"Captured "<<name<<": "<<foreground<<" foreground pixels"<<std::endl;
                if(selected==2)break;++selected;frames=0;reset(camera,scenes[selected],selected);
            }
        }
        if(smoke){report<<"]}";if(captures!=3)throw std::runtime_error("Smoke test interrupted before all scenes rendered");}
        for(auto& scene:scenes)for(auto& draw:scene.draws)if(!draw.dynamic.vertexBuffers.empty())gfx.destroyDynamicMesh(draw.dynamic);
        return 0;
    }catch(const std::exception& e){std::cerr<<"Vulkan viewer: "<<e.what()<<std::endl;return 1;}
}
