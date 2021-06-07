#include <string>
#include<stdio.h>
#include <iostream>
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/tcp-client-module.h"
using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE ("TcpTest");
static const double startTime=0;
static const double simDuration= 300.0;
#define DEFAULT_PACKET_SIZE 1500
static NodeContainer BuildExampleTopo (uint32_t bps,
                                       uint32_t msDelay,
                                       uint32_t msQdelay,
                                       bool enable_random_loss=false)
{
    NodeContainer nodes;
    nodes.Create (2);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue  (DataRate (bps)));
    pointToPoint.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (msDelay)));
    auto bufSize = std::max<uint32_t> (DEFAULT_PACKET_SIZE, bps * msQdelay / 8000);
    int packets=bufSize/DEFAULT_PACKET_SIZE;
    NS_LOG_INFO("buffer packet "<<packets);
    pointToPoint.SetQueue ("ns3::DropTailQueue",
                           "MaxSize", StringValue (std::to_string(1)+"p"));
    NetDeviceContainer devices = pointToPoint.Install (nodes);

    InternetStackHelper stack;
    stack.Install (nodes);

    TrafficControlHelper pfifoHelper;
    uint16_t handle = pfifoHelper.SetRootQueueDisc ("ns3::FifoQueueDisc", "MaxSize", StringValue (std::to_string(packets)+"p"));
    pfifoHelper.AddInternalQueues (handle, 1, "ns3::DropTailQueue", "MaxSize",StringValue (std::to_string(packets)+"p"));
    pfifoHelper.Install(devices);
    Ipv4AddressHelper address;
    std::string nodeip="10.1.1.0";
    address.SetBase (nodeip.c_str(), "255.255.255.0");
    address.Assign (devices);
    if(enable_random_loss){
        std::string errorModelType = "ns3::RateErrorModel";
        ObjectFactory factory;
        factory.SetTypeId (errorModelType);
        Ptr<ErrorModel> em = factory.Create<ErrorModel> ();
        devices.Get (1)->SetAttribute ("ReceiveErrorModel", PointerValue (em));
    }
    return nodes;
}
int main(int argc, char *argv[])
{
    LogComponentEnable("TcpTest", LOG_LEVEL_ALL);
    LogComponentEnable("TcpClient", LOG_LEVEL_ALL);
    LogComponentEnable("TcpBbr", LOG_LEVEL_ALL);
    CommandLine cmd;
    cmd.Parse (argc, argv);
    uint32_t kMaxmiumSegmentSize=1400;
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(200*kMaxmiumSegmentSize));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(200*kMaxmiumSegmentSize));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",UintegerValue(kMaxmiumSegmentSize));
    uint32_t link_bw=6000000;
    uint32_t link_owd=50;
    uint32_t q_delay=200;
    NodeContainer topo;
    topo=BuildExampleTopo(link_bw,link_owd,q_delay);
    Ptr<Node> h1=topo.Get(0);
    Ptr<Node> h2=topo.Get(1);

    uint16_t serv_port = 5000;
    PacketSinkHelper sink ("ns3::TcpSocketFactory",
                        InetSocketAddress (Ipv4Address::GetAny (), serv_port));
    

    ApplicationContainer apps = sink.Install (h2);
    apps.Start (Seconds (0.0));
    apps.Stop (Seconds (simDuration));
    
    Ptr<Ipv4> ipv4 = h2->GetObject<Ipv4> ();
    Ipv4Address serv_ip = ipv4->GetAddress (1, 0).GetLocal();
    InetSocketAddress socket_addr=InetSocketAddress{serv_ip,serv_port};
    Address serv_addr=socket_addr;

    std::string algo("bbr");
    uint64_t totalTxBytes = 30000*1500;
    {
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT);
        h1->AddApplication(client);
        client->ConfigurePeer(serv_addr);
        client->SetCongestionAlgo(algo);
        client->SetStartTime (Seconds (startTime));
        client->SetStopTime (Seconds (simDuration));
    }
    {
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT);
        h1->AddApplication(client);
        client->ConfigurePeer(serv_addr);
        client->SetCongestionAlgo(algo);
        client->SetStartTime (Seconds (startTime+15));
        client->SetStopTime (Seconds (simDuration));
    }
    {
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT);
        h1->AddApplication(client);
        client->ConfigurePeer(serv_addr);
        client->SetCongestionAlgo(algo);
        client->SetStartTime (Seconds (startTime+32));
        client->SetStopTime (Seconds (simDuration));
    }
    
    Simulator::Stop (Seconds (simDuration+10.0));
    Simulator::Run ();
    Simulator::Destroy ();
    return 0;
    
}
