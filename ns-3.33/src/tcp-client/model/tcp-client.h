#pragma once
#include<string>
#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/internet-module.h"
#include "ns3/tcp-tracer.h"
namespace ns3
{
class TcpClient:public Application
{
public:
    enum TraceEnable:uint8_t{
        E_TRACE_CWND=0x01,
        E_TRACE_INFLIGHT=0x02,
        E_TRACE_RTT=0x04,
        E_TRACE_RATE=0x08,
        E_TRACE_ALL=E_TRACE_CWND|E_TRACE_INFLIGHT|E_TRACE_RTT|E_TRACE_RATE,
    };
    TcpClient(uint64_t bytes,uint32_t flag=0);
    ~TcpClient();
    static void SetSegmentSize(uint32_t mss);
    static void SetRateCountGap(Time gap);
    void ConfigurePeer(Address addr);
    void SetCongestionAlgo(std::string &algo);
private:
    Ipv4Address GetIpv4Address();
    virtual void StartApplication (void);
    virtual void StopApplication (void);
    void ConfigureCongstionAlgo();
    void ConnectionSucceeded (Ptr<Socket> socket);
    void ConnectionFailed (Ptr<Socket> socket);
    void HandlePeerClose (Ptr<Socket> socket);
    void HandlePeerError (Ptr<Socket> socket);
    void OnCanWrite(Ptr<Socket>, uint32_t); // for socket's SetSendCallback
    void NotifiSendData();
    void RegisterTraceFunctions();
    void TraceCwndCallback(uint32_t oldval, uint32_t newval);
    void TraceBytesInflightCallback(uint32_t oldval,uint32_t newval);
    void TraceRttCallback(Time oldval, Time newval);
    void TraceTxCallback(Ptr<const Packet> packet, const TcpHeader& header,Ptr<const TcpSocketBase> base);
    uint16_t m_port=0;
    uint64_t m_targetBytes=0;
    uint64_t m_currentTxBytes=0;
    uint32_t m_traceFlag=0;
    uint32_t m_uuid=0;
    bool m_connected=false;
    std::string m_algo{"linux-reno"};
    Address m_serverAddr;
    Ptr<Socket> m_socket;
    Ptr<TcpTracer> m_trace;
    Time m_lastCountRateTime=Time(0);
    uint64_t m_lastTxBytes=0;
    uint64_t m_totalTxBytes=0;
};
}

