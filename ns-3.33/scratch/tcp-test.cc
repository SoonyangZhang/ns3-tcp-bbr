#include <string>
#include <unistd.h>
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
// ./waf --run "scratch/tcp-test --cc=bbr2 --folder=bbr2"
int main(int argc, char *argv[])
{
    LogComponentEnable("TcpTest", LOG_LEVEL_ALL);
    LogComponentEnable("TcpClient", LOG_LEVEL_ALL);
    LogComponentEnable("TcpBbr", LOG_LEVEL_ALL);
    std::string cc("bbr2");
    std::string folder_name("default");
    CommandLine cmd;
    cmd.AddValue ("cc", "congestion algorithm1",cc);
    cmd.AddValue ("folder", "folder name to collect data", folder_name);
    cmd.Parse (argc, argv);
    uint32_t kMaxmiumSegmentSize=1400;
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(200*kMaxmiumSegmentSize));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(200*kMaxmiumSegmentSize));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",UintegerValue(kMaxmiumSegmentSize));
    if(0==cc.compare("reno")||0==cc.compare("bic")||0==cc.compare("cubic")||
      0==cc.compare("bbr")||0==cc.compare("bbr2")){}
    else{
        NS_ASSERT_MSG(0,"please input correct cc");
    }
    std::string trace_folder;
    
    {
        char buf[FILENAME_MAX];
        std::string trace_path=std::string (getcwd(buf, FILENAME_MAX))+"/traces/";
        trace_folder=trace_path+folder_name+"/";
        MakePath(trace_folder);
        TcpBbrDebug::SetTraceFolder(trace_folder.c_str());
        TcpTracer::SetTraceFolder(trace_folder.c_str());
    }
    uint32_t link_bw=6000000;
    uint32_t link_owd=50;
    uint32_t q_delay=200;
    NodeContainer topo;
    topo=BuildExampleTopo(link_bw,link_owd,q_delay);
    Ptr<Node> h1=topo.Get(0);
    Ptr<Node> h2=topo.Get(1);

    //for utility
    TcpTracer::SetExperimentInfo(3,link_bw);
    //for loss rate
    TcpTracer::SetLossRateFlag(true);

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

    uint64_t totalTxBytes = 40000*1500;
    {
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT|TcpClient::E_TRACE_RATE);
        h1->AddApplication(client);
        client->ConfigurePeer(serv_addr);
        client->SetCongestionAlgo(cc);
        client->SetStartTime (Seconds (startTime));
        client->SetStopTime (Seconds (simDuration));
    }
    {
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT|TcpClient::E_TRACE_RATE);
        h1->AddApplication(client);
        client->ConfigurePeer(serv_addr);
        client->SetCongestionAlgo(cc);
        client->SetStartTime (Seconds (startTime+20));
        client->SetStopTime (Seconds (simDuration));
    }
    {
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT|TcpClient::E_TRACE_RATE);
        h1->AddApplication(client);
        client->ConfigurePeer(serv_addr);
        client->SetCongestionAlgo(cc);
        client->SetStartTime (Seconds (startTime+50));
        client->SetStopTime (Seconds (simDuration));
    }
    
    Simulator::Stop (Seconds (simDuration+10.0));
    Simulator::Run ();
    Simulator::Destroy ();
    return 0;
    
}
