#include <unistd.h>
#include <memory.h>
#include "tcp-bbr-debug.h"
namespace ns3{
namespace{
    uint32_t kDebugUniqueIdCount=0;
    char RootDir[FILENAME_MAX]={0};
}
void TcpBbrDebug::SetTraceFolder(const char *path){
    memset(RootDir,0,FILENAME_MAX);
    int sz=std::min((int)(FILENAME_MAX-1),(int)strlen(path));
    memcpy(RootDir,path,sz);
}
TcpBbrDebug::TcpBbrDebug(std::string prefix){
    m_uuid=kDebugUniqueIdCount;
    kDebugUniqueIdCount++;
    OpenFile(prefix);
}
TcpBbrDebug::~TcpBbrDebug(){
    CloseFile();
}
void TcpBbrDebug::OpenFile(std::string prefix){
    char buf[FILENAME_MAX];
    std::string path = std::string (getcwd(buf, FILENAME_MAX))+ "/traces/";
    int len=strlen(RootDir);
    if(len>0){
        std::string parent_dir(RootDir,len);
        path=parent_dir;
        if(RootDir[len-1]!='/'){
           path=parent_dir+"/";
        }
    }
    if(!m_stream.is_open()){
        std::string pathname=path+std::to_string(m_uuid)+"_"+prefix+"_info.txt";
        m_stream.open(pathname.c_str(), std::fstream::out);
    }
}
void TcpBbrDebug::CloseFile(){
    if(m_stream.is_open()){
        m_stream.close();
    }
}
}
