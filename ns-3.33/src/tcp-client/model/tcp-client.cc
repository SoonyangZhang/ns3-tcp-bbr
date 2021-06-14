#include <string>
#include <algorithm>
#include "tcp-utils.h"
#include"tcp-client.h"
#include "ns3/log.h"
namespace ns3{
namespace{
    uint32_t kMSS=1400;
    Time kRateCountGap=MilliSeconds(1000);
    uint32_t TcpClientIdCounter=0;
}
NS_LOG_COMPONENT_DEFINE("TcpClient");
TcpClient::TcpClient(uint64_t bytes,uint32_t flag){
    m_targetBytes=bytes;
    m_traceFlag=flag;
    m_uuid=TcpClientIdCounter;
    TcpClientIdCounter++;
}
TcpClient::~TcpClient(){
    if(TcpTracer::IsEnableLossRate()){
        uint64_t max_val=m_totalTxBytes;
        uint64_t min_val=m_targetBytes;
        double loss_rate=0.0;
        if(max_val>min_val){
            loss_rate=1.0*(max_val-min_val)*100/max_val;
        }
        TcpTracer::OnLossInfo(m_uuid,loss_rate);        
    }
}
void TcpClient::SetSegmentSize(uint32_t mss){
    kMSS=mss;
}
void TcpClient::SetRateCountGap(Time gap){
    kRateCountGap=gap;
}
void TcpClient::SetCongestionAlgo(std::string &algo){
    m_algo=algo;
}
void TcpClient::ConfigureCongstionAlgo(){
    TypeId id;
    if(0==m_algo.compare("reno")){
        id=TcpNewReno::GetTypeId ();
    }else if(0==m_algo.compare("linux-reno")){
        id=TcpLinuxReno::GetTypeId();
    }else if(0==m_algo.compare("vegas")){
        id=TcpVegas::GetTypeId ();
    }else if(0==m_algo.compare("bic")){
        id=TcpBic::GetTypeId ();
    }else if(0==m_algo.compare("cubic")){
        id=TcpCubic::GetTypeId();
    }else if (0==m_algo.compare ("westwood")){
        id=TcpWestwood::GetTypeId ();
    }else if (0==m_algo.compare ("bbr")){
        id=TcpBbr::GetTypeId ();
    }else if (0==m_algo.compare ("bbr2")){
        id=TcpBbr2::GetTypeId ();
    }else{
        id=TcpLinuxReno::GetTypeId();
    }
    ObjectFactory congestionAlgorithmFactory;
    congestionAlgorithmFactory.SetTypeId (id);
    Ptr<TcpCongestionOps> algo = congestionAlgorithmFactory.Create<TcpCongestionOps> ();
    TcpSocketBase *base=static_cast<TcpSocketBase*>(PeekPointer(m_socket));
    if(0==m_algo.compare ("bbr")||0==m_algo.compare ("bbr2")){
        base->SetPacingStatus(true);
    }
    base->SetCongestionControlAlgorithm (algo);
}
void TcpClient::ConfigurePeer(Address addr){
    m_serverAddr=addr;
}
Ipv4Address TcpClient::GetIpv4Address(){
    Ptr<Node> node=GetNode();
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
    Ipv4Address ipv4_addr = ipv4->GetAddress (1, 0).GetLocal ();
    return ipv4_addr;
}
void TcpClient::StartApplication (void){
    m_socket=Socket::CreateSocket(GetNode(),TcpSocketFactory::GetTypeId ());
    m_socket->Bind ();
    m_socket->SetAttribute("SegmentSize",UintegerValue(kMSS));
    ConfigureCongstionAlgo();
    RegisterTraceFunctions();
    m_socket->Connect(m_serverAddr);
    m_socket->ShutdownRecv();
    m_socket->SetConnectCallback(MakeCallback (&TcpClient::ConnectionSucceeded, this),
                                    MakeCallback (&TcpClient::ConnectionFailed, this));
    m_socket->SetCloseCallbacks (MakeCallback (&TcpClient::HandlePeerClose, this),
                                    MakeCallback (&TcpClient::HandlePeerError, this));
    m_socket->SetSendCallback(MakeCallback (&TcpClient::OnCanWrite, this));
}
void TcpClient::StopApplication (void){
    if (m_socket){
        m_socket->Close ();
        m_connected = false;
    }
}
void TcpClient::ConnectionSucceeded (Ptr<Socket> socket){
    NS_LOG_INFO("Connection succeeded");
    m_connected=true;
    NotifiSendData ();
}
void TcpClient::ConnectionFailed(Ptr<Socket>Socket){}
void TcpClient::OnCanWrite(Ptr<Socket> socket, uint32_t){
    if(m_connected){
        NotifiSendData();
    }
}
void TcpClient::HandlePeerClose (Ptr<Socket> socket){
    NS_LOG_FUNCTION (this << socket);
}
void TcpClient::HandlePeerError (Ptr<Socket> socket){
    NS_LOG_FUNCTION (this << socket);
}
void TcpClient::NotifiSendData(){
    while (m_currentTxBytes <m_targetBytes&&m_socket->GetTxAvailable () > 0){
        uint64_t left =m_targetBytes-m_currentTxBytes;
        uint32_t to_write=std::min((uint64_t)kMSS, (uint64_t)left);
        to_write =std::min(to_write,m_socket->GetTxAvailable ());
        int ret=0;
        if(to_write>0){
            ret=m_socket->Send (0, to_write, 0);//means,no data;
            if(ret<0){
                // we will be called again when new tx space becomes available.
                return;
            }
        }
        m_currentTxBytes+=ret;
    }
    if(m_currentTxBytes>=m_targetBytes){
        m_socket->Close();
    }
}
void TcpClient::RegisterTraceFunctions(){
    if(nullptr==m_socket||m_trace!=nullptr){
        return ;
    }
    if(TcpTracer::IsEnableLossRate()||m_traceFlag&E_TRACE_RATE){
        m_socket->TraceConnectWithoutContext ("Tx",MakeCallback(&TcpClient::TraceTxCallback,this));
    }
    if(0==m_traceFlag){
        return ;
    }
    m_trace=CreateObject<TcpTracer>();
    std::string file_name;
    std::string delimiter="_";
    Address local_addr;
    m_socket->GetSockName(local_addr);
    InetSocketAddress local_sock_addr=InetSocketAddress::ConvertFrom(local_addr);
    InetSocketAddress remote_sock_addr=InetSocketAddress::ConvertFrom(m_serverAddr);
    uint32_t ip1=GetIpv4Address().Get();
    uint16_t port1=local_sock_addr.GetPort();
    uint32_t ip2=remote_sock_addr.GetIpv4().Get();
    uint16_t port2=remote_sock_addr.GetPort();
    std::string ip1_str=TcpUtils::ConvertIpString(ip1);
    std::string port1_str=std::to_string(port1);
    std::string ip2_str=TcpUtils::ConvertIpString(ip2);
    std::string port2_str=std::to_string(port2);
    file_name=ip1_str+delimiter+port1_str+delimiter+ip2_str+delimiter+port2_str;
    if(m_traceFlag&E_TRACE_CWND){
        m_trace->OpenCwndTraceFile(file_name);
        m_socket->TraceConnectWithoutContext ("CongestionWindow", MakeCallback(&TcpClient::TraceCwndCallback,this));
    }
    if(m_traceFlag&E_TRACE_INFLIGHT){
        m_trace->OpenInflightTraceFile(file_name);
        m_socket->TraceConnectWithoutContext ("BytesInFlight", MakeCallback(&TcpClient::TraceBytesInflightCallback,this));
    }
    if(m_traceFlag&E_TRACE_RTT){
        m_trace->OpenRttTraceFile(file_name);
        m_socket->TraceConnectWithoutContext ("RTT",MakeCallback(&TcpClient::TraceRttCallback,this));
    }
    if(m_traceFlag&E_TRACE_RATE){
        m_trace->OpenSendRateTraceFile(file_name);
    }
    if(TcpTracer::IsEnableBandwidthUtility()){
        TcpSessionKey key(ip1,port1,ip2,port2);
        TcpTracer::RegisterBulkBytes(key,m_targetBytes);
    }
}
void TcpClient::TraceCwndCallback(uint32_t oldval, uint32_t newval){
    if(m_trace){
        Time now=Simulator::Now();
        uint32_t w=newval/kMSS;
        m_trace->OnCwnd(now,w);
    }
}
void TcpClient::TraceBytesInflightCallback(uint32_t oldval,uint32_t newval){
    if(m_trace){
        Time now=Simulator::Now();
        uint32_t packets=newval/kMSS;
        m_trace->OnInflight(now,packets);
    }
}
void TcpClient::TraceRttCallback(Time oldval, Time newval){
    if(m_trace){
        Time now=Simulator::Now();
        m_trace->OnRtt(now,newval);
    }
}
void TcpClient::TraceTxCallback(Ptr<const Packet> packet, const TcpHeader& header,Ptr<const TcpSocketBase> base){
    Time now=Simulator::Now();
    m_totalTxBytes+=packet->GetSize();

    if(m_trace){
        if(m_lastCountRateTime.IsZero()){
            m_lastCountRateTime=now;
        }
        if(now>=m_lastCountRateTime+kRateCountGap){
            double bps=1.0*(m_totalTxBytes-m_lastTxBytes)*8000/(now-m_lastCountRateTime).GetMilliSeconds();
            DataRate rate(bps);
            m_lastTxBytes=m_totalTxBytes;
            m_lastCountRateTime=now;
            m_trace->OnSendRate(now,rate);
        }        
    }
}

}
