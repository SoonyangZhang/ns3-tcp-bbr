#pragma once
#include <vector>
#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/internet-module.h"
#include "ns3/tcp-sink.h"
namespace ns3{
class TcpServer:public Application{
public:
    TcpServer(Address local_addr);
    virtual ~TcpServer();
    Ptr<Socket> GetListeningSocket (void) const;
protected:
  virtual void DoDispose (void);
private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);
    void HandleRead (Ptr<Socket> socket);
    void HandleAccept (Ptr<Socket> socket, const Address& from);
    void HandlePeerClose (Ptr<Socket> socket);
    void HandlePeerError (Ptr<Socket> socket);
    Address m_localAddr;
    Ptr<Socket> m_socket;
    std::vector<Ptr<TcpSink>> m_sinks;
};
}
