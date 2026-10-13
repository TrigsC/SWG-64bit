#include "Visualizer.h"
#include "MaterialResolver.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    try {
        setenv("VK_DRIVER_FILES",SWG_MOLTENVK_ICD,0);
        std::string materialAudit,cameraAudit,floorAudit,cellAudit,frameAudit,shipAudit;
        OfflineWorldOptions options;options.clientPath=SWG_CLIENT_ROOT;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--client-path"&&i+1<argc)options.clientPath=argv[++i];
            else if(arg=="--smoke"&&i+1<argc)options.smokeOutput=argv[++i];
            else if(arg=="--material-smoke"&&i+1<argc){options.smokeOutput=argv[++i];options.materialSmoke=true;}
            else if(arg=="--creature-material-audit"&&i+1<argc)materialAudit=argv[++i];
            else if(arg=="--frame-audit"&&i+1<argc)frameAudit=argv[++i];
            else if(arg=="--ship-frame-audit"&&i+1<argc)shipAudit=argv[++i];
            else if(arg=="--frame-smoke"&&i+1<argc){options.smokeOutput=argv[++i];options.cellSmoke=true;options.frameSmoke=true;}
            else if(arg=="--cell-audit"&&i+1<argc)cellAudit=argv[++i];
            else if(arg=="--cell-smoke"&&i+1<argc){options.smokeOutput=argv[++i];options.cellSmoke=true;}
            else if(arg=="--floor-audit"&&i+1<argc)floorAudit=argv[++i];
            else if(arg=="--floor-smoke"&&i+1<argc){options.smokeOutput=argv[++i];options.floorSmoke=true;}
            else if(arg=="--camera-audit"&&i+1<argc)cameraAudit=argv[++i];
            else if(arg=="--camera-smoke"&&i+1<argc){options.smokeOutput=argv[++i];options.cameraSmoke=true;}
            else if(arg=="--action-smoke"&&i+1<argc){options.smokeOutput=argv[++i];options.actionSmoke=true;}
            else throw std::runtime_error("Usage: swg-offline-world [--client-path DIRECTORY] [--smoke DIRECTORY | --action-smoke DIRECTORY | --material-smoke DIRECTORY | --creature-material-audit FILE | --camera-audit FILE | --camera-smoke DIRECTORY | --floor-audit FILE | --floor-smoke DIRECTORY | --cell-audit FILE | --cell-smoke DIRECTORY | --frame-audit FILE | --ship-frame-audit FILE | --frame-smoke DIRECTORY]");
        }
        if(!shipAudit.empty()){writeFrameAssetAudit(options.clientPath,shipAudit,true);return 0;}
        if(!frameAudit.empty()){writeFrameAssetAudit(options.clientPath,frameAudit);return 0;}
        if(!cellAudit.empty()){writeCellAssetAudit(options.clientPath,cellAudit);return 0;}
        if(!floorAudit.empty()){writeFloorAssetAudit(options.clientPath,floorAudit);return 0;}
        if(!cameraAudit.empty()){writeCameraAssetAudit(options.clientPath,cameraAudit);return 0;}
        if (!materialAudit.empty()) {
            auto materials = dummyclient::resolveCreatureMaterials(options.clientPath, {
                "object/creature/player/shared_human_male.iff",
                "object/creature/player/shared_human_female.iff",
                "object/mobile/shared_womp_rat.iff",
                "object/mobile/shared_gaping_spider.iff",
                "object/mobile/shared_bantha.iff",
                "object/mobile/shared_cu_pa.iff"});
            dummyclient::writeCreatureMaterialAudit(materials, materialAudit);
            return 0;
        }
        runOfflineVisualizer(options);return 0;
    }catch(const std::exception& e){std::cerr<<"Offline world: "<<e.what()<<std::endl;return 1;}
}
