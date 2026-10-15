#include "clientcommon/LoginInput.h"
#include "clientcommon/LauncherProtocol.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <vector>
int checks=0;
void require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
std::string frame(const std::string& user,const std::string& pass){
    std::string out;for(auto n:{user.size(),pass.size()})for(int shift:{24,16,8,0})out+=char((n>>shift)&255);
    return out+user+pass;
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--stdin-fixture") {
        auto credentials=clientcommon::readLoginCredentials(std::cin);
        require(credentials.username=="fixture-user"&&credentials.password=="test-$-\"-\n-secret","Swift/C++ private pipe framing mismatch");
    }else{
        const auto encoded=clientcommon::credentialFrame("synthetic", "a\r\n\x1a" "z");
        std::istringstream encodedInput(std::string(encoded.begin(),encoded.end()));
        require(clientcommon::readLoginCredentials(encodedInput).password=="a\r\n\x1a" "z","Launcher frame altered binary bytes");
        const auto record=clientcommon::characterRecord(17,0x100000001,"Synthetic character");
        auto character=clientcommon::parseCharacterRecord(record);
        require(character&&character->galaxy==17&&character->name=="Synthetic character","Character record roundtrip failed");
        require(!clientcommon::parseCharacterRecord("NATIVE_CHARACTER 1 1 0a"),"Character record accepted a control byte");
        for(const auto& password:std::vector<std::string>{"secret","a\nb\r\n\"$`","unicode-\xc3\xa9"}) {
            std::istringstream input(frame("account",password));auto result=clientcommon::readLoginCredentials(input);
            require(result.username=="account"&&result.password==password,"Credential bytes changed");
        }
        for(const auto& invalid:std::vector<std::string>{"",std::string(7,'\0'),frame("","password"),frame("user",""),frame("u",std::string(4097,'x')),frame(std::string("u\0x",3),"p"),frame("user","password").substr(0,10)}) {
            bool rejected=false;try{std::istringstream input(invalid);(void)clientcommon::readLoginCredentials(input);}catch(const std::exception&){rejected=true;}
            require(rejected,"Malformed login pipe must fail before networking");
        }
        for(const auto& invalid:std::vector<std::string>{"","0","65536","-1","44453x","9999999999999999999999"}) {
            bool rejected=false;try{(void)clientcommon::parseLoginPort(invalid);}catch(const std::exception&){rejected=true;}
            require(rejected,"Invalid port must not truncate into another destination");
        }
        require(clientcommon::parseLoginPort("44453")==44453&&clientcommon::parseLoginPort("65535")==65535,"Valid ports rejected");
    }
    std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"scope\":\"Bounded private credential pipe and destination validation\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
