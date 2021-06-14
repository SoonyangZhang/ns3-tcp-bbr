#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h> // stat
#include <memory.h>
#include <string.h>
#include <map>
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

class InfoPriv:public Object{
public:
    static Ptr<InfoPriv> Get(void);
    InfoPriv();
    ~InfoPriv();
    void SetExperimentInfo(uint32_t flow_num,uint32_t bottleneck_bw);
    void SetLossRateFlag(bool flag);
    bool IsEnableBandwidthUtility();
    bool IsEnableLossRate() {return m_lossFlag;}
    void RegisterBulkBytes(const TcpSessionKey &key,uint32_t bytes);
    int64_t GetBulkBytes (const TcpSessionKey &key);
    void OnLossInfo(uint32_t uuid,float loss_rate);
    void OnSessionStop(uint32_t bytes,Time stop);
private:
    virtual void DoDispose (void);
    static Ptr<InfoPriv> *DoGet (void);
    static void Delete (void);
    inline void OpenLossFile();
    inline void OpenUtilFile();
    uint32_t m_flows {0};
    uint32_t m_bandwidth {0};
    uint32_t m_flowCount{0};
    Time m_stopStamp {Time(0)};
    uint64_t m_bytes=0;
    bool m_lossFlag {false};
    std::map<TcpSessionKey,uint32_t> m_sessionBytes;
    std::fstream m_loss;
    std::fstream m_util;
};
Ptr<InfoPriv> InfoPriv::Get(void){
    return *DoGet();
}
void InfoPriv::SetExperimentInfo(uint32_t flow_num,uint32_t bottleneck_bw){
    m_flows=flow_num;
    m_bandwidth=bottleneck_bw;
}
void InfoPriv::SetLossRateFlag(bool flag){
    m_lossFlag=flag;
}
bool InfoPriv::IsEnableBandwidthUtility(){
    return m_flows>0;
}
void InfoPriv::RegisterBulkBytes(const TcpSessionKey &key,uint32_t bytes){
    if(m_flows>0){
        m_sessionBytes.insert(std::make_pair(key,bytes));
    }
}
int64_t InfoPriv::GetBulkBytes (const TcpSessionKey &key){
    int64_t bytes=-1;
    if(m_flows>0){
        auto it=m_sessionBytes.find(key);
        if(it!=m_sessionBytes.end()){
            bytes=it->second;
        }
    }
    return bytes;
}
void InfoPriv::OnLossInfo(uint32_t uuid,float loss_rate){
    if(!m_loss.is_open()){
        OpenLossFile();
    }
    m_loss<<uuid<<"\t"<<loss_rate<<std::endl;
}
void InfoPriv::OnSessionStop(uint32_t bytes,Time stop){
    if(0==m_flows){
        return ;
    }
    if(!m_util.is_open()){
        OpenUtilFile();
    }
    m_bytes+=bytes;
    m_flowCount++;
    if(Time(0)==m_stopStamp||stop>m_stopStamp){
        m_stopStamp=stop;
    }
    if(m_flowCount==m_flows){
        double util=0.0;
        if(m_stopStamp!=Time(0)){
            double average_bps=1.0*m_bytes*8000/m_stopStamp.GetMilliSeconds();
            util=average_bps*100/m_bandwidth;
            m_util<<m_stopStamp.GetSeconds()<<"\t"<<m_bytes<<"\t"<<util<<std::endl;
        }
    }
}
InfoPriv::InfoPriv(){}
InfoPriv::~InfoPriv(){
    if(m_loss.is_open()){
        m_loss.close();
    }
    if(m_util.is_open()){
        m_util.close();
    }
}
void InfoPriv::DoDispose(){
    Object::DoDispose();
}
Ptr<InfoPriv> *InfoPriv::DoGet (void){
    static Ptr<InfoPriv> ptr = 0;
    if(0==ptr){
        ptr = CreateObject<InfoPriv>();
        Simulator::ScheduleDestroy (&InfoPriv::Delete);
    }
    return &ptr;
}
void InfoPriv::Delete (void){
    (*DoGet ()) = 0;
}
void InfoPriv::OpenLossFile(){
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
void InfoPriv::OpenUtilFile(){
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
    path=path+"utilinfo.txt";
    m_util.open(path.c_str(), std::fstream::out);
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
    int len=strlen(path);
    if(len>0){
        memcpy(RootDir,path,len);
    }
}
void TcpTracer::ClearTraceFolder(){
    memset(RootDir,0,FILENAME_MAX);
}
void TcpTracer::SetExperimentInfo(uint32_t flow_num,uint32_t bottleneck_bw){
    InfoPriv::Get()->SetExperimentInfo(flow_num,bottleneck_bw);
}
void TcpTracer::SetLossRateFlag(bool flag){
    InfoPriv::Get()->SetLossRateFlag(flag);
}
bool TcpTracer::IsEnableLossRate(){
    return InfoPriv::Get()->IsEnableLossRate();
}
bool TcpTracer::IsEnableBandwidthUtility(){
    return InfoPriv::Get()->IsEnableBandwidthUtility();
}
void TcpTracer::RegisterBulkBytes(const TcpSessionKey &key,uint32_t bytes){
    InfoPriv::Get()->RegisterBulkBytes(key,bytes);
}
int64_t TcpTracer::GetBulkBytes (const TcpSessionKey &key){
    return InfoPriv::Get()->GetBulkBytes(key);
}
void TcpTracer::OnLossInfo(uint32_t uuid,float loss_rate){
    InfoPriv::Get()->OnLossInfo(uuid,loss_rate);
}
void TcpTracer::OnSessionStop(uint32_t bytes,Time stop){
    InfoPriv::Get()->OnSessionStop(bytes,stop);
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
}
