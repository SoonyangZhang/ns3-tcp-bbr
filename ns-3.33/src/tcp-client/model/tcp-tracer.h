#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/data-rate.h"
namespace ns3{
class TcpTracer:public Object
{
public:
    TcpTracer(){}
    ~TcpTracer();
    static void SetTraceFolder(const char *path);
    static void OnLossInfo(uint32_t uuid,float loss_rate);
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
