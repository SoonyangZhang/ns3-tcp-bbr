#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
namespace ns3{
struct TcpSessionKey{
    TcpSessionKey():TcpSessionKey(0,0,0,0){}
    TcpSessionKey(uint32_t ip1_arg,uint16_t port1_arg,
                  uint32_t ip2_arg,uint16_t port2_arg):
    ip1(ip1_arg),ip2(ip2_arg),port1(port1_arg),port2(port2_arg){}
    uint32_t ip1;
    uint32_t ip2;
    uint16_t port1;
    uint16_t port2;
    bool operator < (const TcpSessionKey &other) const{
        return ip1<other.ip1||ip2<other.ip2||
                port1<other.port1||port2<other.port2;
    }
};
class TcpTracer:public Object
{
public:
    TcpTracer(){}
    ~TcpTracer();
    static void SetTraceFolder(const char *path);
    static void ClearTraceFolder();
    static void SetExperimentInfo(uint32_t flow_num,uint32_t bottleneck_bw);
    static void SetLossRateFlag(bool flag);
    static bool IsEnableBandwidthUtility();
    static bool IsEnableLossRate();
    static void RegisterBulkBytes(const TcpSessionKey &key,uint32_t bytes);
    static int64_t GetBulkBytes (const TcpSessionKey &key);
    static void OnLossInfo(uint32_t uuid,float loss_rate);
    static void OnSessionStop(uint32_t bytes,Time stop);
    void OpenCwndTraceFile(std::string filename);
    void OpenInflightTraceFile(std::string filename);
    void OpenRttTraceFile(std::string filename);
    void OpenSendRateTraceFile(std::string filename);
    void OpenGoodputTraceFile(std::string filename);
    
    void OnCwnd(Time event_time,uint32_t w);
    void OnInflight(Time event_time,uint32_t packets);
    void OnRtt(Time event_time, Time rtt);
    void OnSendRate(Time event_time,DataRate rate);
    void OnGoodput(Time event_time,DataRate rate);
protected:
    virtual void DoDispose (void);
    virtual void DoInitialize (void);
private:
    std::fstream    m_cwnd;
    std::fstream    m_inflight;
    std::fstream    m_rtt;
    std::fstream    m_sendRate;
    std::fstream    m_goodput;
};
bool MakePath(const std::string& path);
}
