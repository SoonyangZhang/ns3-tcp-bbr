#pragma once
#include "ns3/object.h"
#include "ns3/address.h"
#include "ns3/internet-module.h"
#include "ns3/tcp-tracer.h"
namespace ns3{
class TcpSink:public Object{
public:
    TcpSink(Ptr<Socket> socket,Address client_addr,Address server_addr,bool log_rate=false);
    virtual ~TcpSink();
    static void SetRateCountGap(Time gap);
protected:
    virtual void DoDispose (void);
    virtual void DoInitialize (void);
private:
    void HandleRead (Ptr<Socket> socket);
    void HandlePeerClose (Ptr<Socket> socket);
    void HandlePeerError (Ptr<Socket> socket);
    Ptr<Socket> m_socket;
    Ptr<TcpTracer> m_trace;
    Time m_lastCountRateTime=Time(0);
    uint64_t m_lastRxBytes=0;
    uint64_t m_rxBytes=0;
    int64_t  m_totalBytes=0;
};
}
