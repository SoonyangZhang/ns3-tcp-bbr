#include "tcp-sink.h"
#include "tcp-utils.h"
#include "ns3/log.h"
namespace ns3{
NS_LOG_COMPONENT_DEFINE("TcpSink");
namespace{
    Time kRateCountGap=MilliSeconds(1000);
}
std::string TcpUtils::ConvertIpString(uint32_t ip){
    uint32_t a=(ip>>24)&0x00FF;
    uint32_t b=(ip>>16)&0x00FF;
    uint32_t c=(ip>>8)&0x00FF;
    uint32_t d=(ip)&0x00FF;
    std::string delimiter(".");
    std::string ret=std::to_string(a)+delimiter+std::to_string(b)+delimiter+
                    std::to_string(c)+delimiter+std::to_string(d);
    return ret;
}

TcpSink::TcpSink(Ptr<Socket> socket,Address client_addr,Address server_addr,bool log_rate){
    socket->SetRecvCallback (MakeCallback (&TcpSink::HandleRead, this));
    socket->SetCloseCallbacks(MakeCallback (&TcpSink::HandlePeerClose, this),
                                    MakeCallback (&TcpSink::HandlePeerError, this));
    m_socket=socket;
    InetSocketAddress client_sock_addr=InetSocketAddress::ConvertFrom(client_addr);
    InetSocketAddress server_sock_addr=InetSocketAddress::ConvertFrom(server_addr);
    uint32_t ip1=client_sock_addr.GetIpv4().Get();
    uint16_t port1=client_sock_addr.GetPort();
    uint32_t ip2=server_sock_addr.GetIpv4().Get();
    uint16_t port2=server_sock_addr.GetPort();
    if(log_rate){
        std::string file_name;
        std::string delimiter="_";
        std::string ip1_str=TcpUtils::ConvertIpString(ip1);
        std::string port1_str=std::to_string(port1);
        std::string ip2_str=TcpUtils::ConvertIpString(ip2);
        std::string port2_str=std::to_string(port2);
        file_name=ip1_str+delimiter+port1_str+delimiter+ip2_str+delimiter+port2_str;
        m_trace=CreateObject<TcpTracer>();
        m_trace->OpenGoodputTraceFile(file_name);
    }
    if(TcpTracer::IsEnableBandwidthUtility()){
        TcpSessionKey key(ip1,port1,ip2,port2);
        m_totalBytes=TcpTracer::GetBulkBytes(key);
        NS_LOG_FUNCTION(m_totalBytes);
    }
}
TcpSink::~TcpSink(){}
void TcpSink::SetRateCountGap(Time gap){
    kRateCountGap=gap;
}
void TcpSink::DoDispose (void){
    Object::DoDispose();
}
void TcpSink::DoInitialize (void){
    Object::DoInitialize();
}
void TcpSink::HandleRead (Ptr<Socket> socket){
    NS_ASSERT(socket==m_socket);
    Address from;
    Ptr<Packet> packet;
    while ((packet = socket->RecvFrom (from))){
        if(0==packet->GetSize ()){
            break;
        }
        m_rxBytes+=packet->GetSize ();
    }
    if(m_trace){
        Time now=Simulator::Now();
        if(Time(0)==m_lastCountRateTime){
            m_lastCountRateTime=now;
        }
        if(now>=m_lastCountRateTime+kRateCountGap){
            double bps=1.0*(m_rxBytes-m_lastRxBytes)*8000/(now-m_lastCountRateTime).GetMilliSeconds();
            DataRate rate(bps);
            m_lastRxBytes=m_rxBytes;
            m_lastCountRateTime=now;
            m_trace->OnGoodput(now,rate);
        }
    }
    if(m_totalBytes>0&&m_totalBytes==m_rxBytes){
        auto now=Simulator::Now();
        TcpTracer::OnSessionStop(m_rxBytes,now);
    }
}
void TcpSink::HandlePeerClose (Ptr<Socket> socket){}
void TcpSink::HandlePeerError (Ptr<Socket> socket){}
}
