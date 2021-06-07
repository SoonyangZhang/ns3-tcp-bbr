#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h> // stat
#include <memory.h>
#include <string.h>
#include "ns3/simulator.h"
#include "tcp-tracer.h"
namespace ns3{
namespace{
    char RootDir[FILENAME_MAX]={0};
}
//https://stackoverflow.com/questions/675039/how-can-i-create-directory-tree-in-c-linux
static bool IsDirExist(const std::string& path)
{
#if defined(_WIN32)
    struct _stat info;
    if (_stat(path.c_str(), &info) != 0)
    {
        return false;
    }
    return (info.st_mode & _S_IFDIR) != 0;
#else 
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
    {
        return false;
    }
    return (info.st_mode & S_IFDIR) != 0;
#endif
}
bool MakePath(const std::string& path)
{
#if defined(_WIN32)
    int ret = _mkdir(path.c_str());
#else
    mode_t mode = 0755;
    int ret = mkdir(path.c_str(), mode);
#endif
    if (ret == 0)
        return true;

    switch (errno)
    {
    case ENOENT:
        // parent didn't exist, try to create it
        {
            int pos = path.find_last_of('/');
            if (pos == std::string::npos)
#if defined(_WIN32)
                pos = path.find_last_of('\\');
            if (pos == std::string::npos)
#endif
                return false;
            if (!MakePath( path.substr(0, pos) ))
                return false;
        }
        // now, try to create again
#if defined(_WIN32)
        return 0 == _mkdir(path.c_str());
#else 
        return 0 == mkdir(path.c_str(), mode);
#endif
    case EEXIST:
        // done!
        return IsDirExist(path);

    default:
        return false;
    }
}
TcpTracer::~TcpTracer()
{
    if(m_cwnd.is_open()){
        m_cwnd.close();
    }
    if(m_inflight.is_open()){
        m_inflight.close();
    }
    if (m_rtt.is_open()){
        m_rtt.close();
    }
    if (m_sendRate.is_open()){
        m_sendRate.close();
    }
    if(m_goodput.is_open()){
        m_goodput.close();
    }
}
void TcpTracer::SetTraceFolder(const char *path){
    memset(RootDir,0,FILENAME_MAX);
    int sz=std::min((int)(FILENAME_MAX-1),(int)strlen(path));
    memcpy(RootDir,path,sz);
}
void TcpTracer::OpenCwndTraceFile(std::string filename)
{
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
    path=path+filename+"_cwnd.txt";
    m_cwnd.open(path.c_str(), std::fstream::out);
}
void TcpTracer::OpenInflightTraceFile(std::string filename){
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
    path=path+filename+"_inflight.txt";
    m_inflight.open(path.c_str(), std::fstream::out);
}
void TcpTracer::OpenRttTraceFile(std::string filename)
{
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
    path=path+filename+"_rtt.txt";
    m_rtt.open(path.c_str(), std::fstream::out);
}
void TcpTracer::OpenSendRateTraceFile(std::string filename){
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
    path=path+filename+"_sendrate.txt";
    m_sendRate.open(path.c_str(), std::fstream::out);
}
void TcpTracer::OpenGoodputTraceFile(std::string filename){
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
    path=path+filename+"_goodput.txt";
    m_goodput.open(path.c_str(), std::fstream::out);
}
void TcpTracer::OnCwnd(Time event_time,uint32_t w){
    if(m_cwnd.is_open()){
        m_cwnd<<event_time.GetSeconds()<<"\t"<<w<< std::endl;        
    }
}
void TcpTracer::OnInflight(Time event_time,uint32_t packets){
    if(m_inflight.is_open()){
        m_inflight<<event_time.GetSeconds()<<"\t"<<packets<< std::endl;
    }
}
void TcpTracer::OnRtt(Time event_time, Time rtt){
    if(m_rtt.is_open()){
        m_rtt<<event_time.GetSeconds()<<"\t"<<rtt.GetMilliSeconds()<<std::endl;
    }
}
void TcpTracer::OnSendRate(Time event_time,DataRate rate){
    if(m_sendRate.is_open()){
        float kbps=1.0*rate.GetBitRate()/1000;
        m_sendRate<<event_time.GetSeconds()<<"\t"<<kbps<<std::endl;
    }
}
void TcpTracer::OnGoodput(Time event_time,DataRate rate){
    if(m_goodput.is_open()){
        float kbps=1.0*rate.GetBitRate()/1000;
        m_goodput<<event_time.GetSeconds()<<"\t"<<kbps<<std::endl;
    }
}
void TcpTracer::DoDispose(){
    Object::DoDispose();
}
void TcpTracer::DoInitialize(){
    Object::DoDispose();
}
class LossInfoPriv:public Object{
public:
    static Ptr<LossInfoPriv> Get(void);
    LossInfoPriv();
    ~LossInfoPriv();
    void OnLossInfo(uint32_t uuid,float loss_rate);
private:
    virtual void DoDispose (void);
    static Ptr<LossInfoPriv> *DoGet (void);
    static void Delete (void);
    std::fstream m_loss;
};
Ptr<LossInfoPriv> LossInfoPriv::Get(void){
    return *DoGet();
}
void LossInfoPriv::OnLossInfo(uint32_t uuid,float loss_rate){
    if(m_loss.is_open()){
        m_loss<<uuid<<"\t"<<loss_rate<<std::endl;
    }
}
LossInfoPriv::LossInfoPriv(){
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
    path=path+"lossinfo.txt";
    m_loss.open(path.c_str(), std::fstream::out);
}
LossInfoPriv::~LossInfoPriv(){
    if(m_loss.is_open()){
        m_loss.close();
    }
}
void LossInfoPriv::DoDispose(){
    Object::DoDispose();
}
Ptr<LossInfoPriv> *LossInfoPriv::DoGet (void){
    static Ptr<LossInfoPriv> ptr = 0;
    if(0==ptr){
        ptr = CreateObject<LossInfoPriv>();
        Simulator::ScheduleDestroy (&LossInfoPriv::Delete);
    }
    return &ptr;
}
void LossInfoPriv::Delete (void){
    (*DoGet ()) = 0;
}
void TcpTracer::OnLossInfo(uint32_t uuid,float loss_rate){
    LossInfoPriv::Get()->OnLossInfo(uuid,loss_rate);
}
}
