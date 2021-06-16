/** Network topology
 *       n0            n1
 *        |            | 
 *        | l0         | l2
 *        |            | 
 *        n2---l1------n3
 *        |            | 
 *        |  l3        | l4
 *        |            | 
 *        n4           n5
 */
#include <string>
#include <unistd.h>
#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <utility>
#include <memory>
#include "ns3/core-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/tcp-client-module.h"
using namespace ns3;
using namespace std;
NS_LOG_COMPONENT_DEFINE ("TcpDumbbell");
class TriggerRandomLoss{
public:
    TriggerRandomLoss(){}
    ~TriggerRandomLoss(){
        if(m_timer.IsRunning()){
            m_timer.Cancel();
        }
    }
    void RegisterDevice(Ptr<NetDevice> device){
        m_netDevice=device;
    }
    void Start(){
        Time next=Seconds(2);
        m_timer=Simulator::Schedule(next,&TriggerRandomLoss::ConfigureRandomLoss,this);
    }
    void ConfigureRandomLoss(){
        if(m_timer.IsExpired()){
            std::string errorModelType = "ns3::RateErrorModel";
            ObjectFactory factory;
            factory.SetTypeId (errorModelType);
            Ptr<ErrorModel> em = factory.Create<ErrorModel> ();
            m_netDevice->SetAttribute ("ReceiveErrorModel", PointerValue (em));            
            m_timer.Cancel();
        }
        NS_ASSERT_MSG(m_counter==1,"only run once");
        m_counter++;
    }
private:
    Ptr<NetDevice> m_netDevice;
    EventId m_timer;
    int m_counter{1};
};

struct LinkProperty{
    uint16_t nodes[2];
    uint32_t bandwidth;
    uint32_t propagation_ms;
};
uint32_t CalMaxRttInDumbbell(LinkProperty *topoinfo,int links){
    uint32_t rtt1=2*(topoinfo[0].propagation_ms+topoinfo[1].propagation_ms+topoinfo[2].propagation_ms);
    uint32_t rtt2=2*(topoinfo[1].propagation_ms+topoinfo[3].propagation_ms+topoinfo[4].propagation_ms);
    return std::max<uint32_t>(rtt1,rtt2);
}

#define DEFAULT_PACKET_SIZE 1500
int ip=1;
static NodeContainer BuildDumbbellTopo(LinkProperty *topoinfo,int links,int bottleneck_i,
                                    uint32_t buffer_ms,TriggerRandomLoss *trigger=nullptr)
{
    int hosts=links+1;
    NodeContainer topo;
    topo.Create (hosts);
    InternetStackHelper stack;
    stack.Install (topo);
    for (int i=0;i<links;i++){
        uint16_t src=topoinfo[i].nodes[0];
        uint16_t dst=topoinfo[i].nodes[1];
        uint32_t bps=topoinfo[i].bandwidth;
        uint32_t owd=topoinfo[i].propagation_ms;
        NodeContainer nodes=NodeContainer (topo.Get (src), topo.Get (dst));
        auto bufSize = std::max<uint32_t> (DEFAULT_PACKET_SIZE, bps * buffer_ms / 8000);
        int packets=bufSize/DEFAULT_PACKET_SIZE;
        std::cout<<bps<<std::endl;
        PointToPointHelper pointToPoint;
        pointToPoint.SetDeviceAttribute ("DataRate", DataRateValue  (DataRate (bps)));
        pointToPoint.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (owd)));
        if(bottleneck_i==i){
            pointToPoint.SetQueue ("ns3::DropTailQueue","MaxSize", StringValue (std::to_string(20)+"p"));
        }else{
            pointToPoint.SetQueue ("ns3::DropTailQueue","MaxSize", StringValue (std::to_string(packets)+"p"));   
        }
        NetDeviceContainer devices = pointToPoint.Install (nodes);
        if(bottleneck_i==i){
            TrafficControlHelper pfifoHelper;
            uint16_t handle = pfifoHelper.SetRootQueueDisc ("ns3::FifoQueueDisc", "MaxSize", StringValue (std::to_string(packets)+"p"));
            pfifoHelper.AddInternalQueues (handle, 1, "ns3::DropTailQueue", "MaxSize",StringValue (std::to_string(packets)+"p"));
            pfifoHelper.Install(devices);  
        }
        Ipv4AddressHelper address;
        std::string nodeip="10.1."+std::to_string(ip)+".0";
        ip++;
        address.SetBase (nodeip.c_str(), "255.255.255.0");
        address.Assign (devices);
        if(bottleneck_i==i&&trigger){
            trigger->RegisterDevice(devices.Get(1));
        }
    }
    Ipv4GlobalRoutingHelper::PopulateRoutingTables ();
    return topo;
}
static const double startTime=0;
static const double simDuration=400.0;
//./waf --run "scratch/tcp-dumbbell --it=3 --cc1=bbr --cc2=bbr --folder=bbr1 "
int main(int argc, char *argv[])
{
    LogComponentEnable("TcpDumbbell", LOG_LEVEL_ALL);
    LogComponentEnable("TcpClient", LOG_LEVEL_ALL);
    LogComponentEnable("TcpSink", LOG_LEVEL_ALL);
    std::string instance=std::string("1");
    std::string cc1("bbr2");
    std::string cc2("bbr2");
    std::string folder_name("no-one");
    std::string loss_str("0");
    CommandLine cmd;
    cmd.AddValue ("it", "instacne", instance);
    cmd.AddValue ("cc1", "congestion algorithm1", cc1);
    cmd.AddValue ("cc2", "congestion algorithm2", cc2);
    cmd.AddValue ("folder", "folder name to collect data", folder_name);
    cmd.AddValue ("lo", "loss",loss_str);
    cmd.Parse (argc, argv);
    uint32_t kMaxmiumSegmentSize=1400;
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(200*kMaxmiumSegmentSize));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(200*kMaxmiumSegmentSize));
    Config::SetDefault("ns3::TcpSocket::SegmentSize",UintegerValue(kMaxmiumSegmentSize));
    int loss_integer=std::stoi(loss_str);
    double random_loss=loss_integer*1.0/1000;
    std::unique_ptr<TriggerRandomLoss> triggerloss=nullptr;
    if(loss_integer>0){
        Config::SetDefault ("ns3::RateErrorModel::ErrorRate", DoubleValue (random_loss));
        Config::SetDefault ("ns3::RateErrorModel::ErrorUnit", StringValue ("ERROR_UNIT_PACKET"));
        Config::SetDefault ("ns3::BurstErrorModel::ErrorRate", DoubleValue (random_loss));
        Config::SetDefault ("ns3::BurstErrorModel::BurstSize", StringValue ("ns3::UniformRandomVariable[Min=1|Max=3]"));
        triggerloss.reset(new TriggerRandomLoss());
        triggerloss->Start();
    }
    
    if(0==cc1.compare("reno")||0==cc1.compare("bic")||0==cc1.compare("cubic")||
      0==cc1.compare("bbr")||0==cc1.compare("bbr2")){}
    else{
        NS_ASSERT_MSG(0,"please input correct cc1");
    }
    if(0==cc2.compare("reno")||0==cc2.compare("bic")||0==cc2.compare("cubic")||
      0==cc2.compare("bbr")||0==cc2.compare("bbr2")){}
    else{
        NS_ASSERT_MSG(0,"please input correct cc2");
    }
    std::string trace_folder;
    
    {
        char buf[FILENAME_MAX];
        std::string trace_path=std::string (getcwd(buf, FILENAME_MAX))+"/traces/";
        trace_folder=trace_path+folder_name+"/"+instance+"/";
        MakePath(trace_folder);
        TcpBbrDebug::SetTraceFolder(trace_folder.c_str());
        TcpTracer::SetTraceFolder(trace_folder.c_str());
    }
    uint32_t bw_unit=1000000;//1Mbps;
    uint32_t non_bottleneck_bw=100*bw_unit;
    uint32_t links=5;
    int bottleneck_i=1;
    LinkProperty topoinfo1[]={
        [0]={0,2,0,10},
        [1]={2,3,0,10},
        [2]={3,1,0,10},
        [3]={2,4,0,10},
        [4]={3,5,0,10},
    };
    {
        uint32_t bottleneck_bw=12*bw_unit;
        LinkProperty *info_ptr=topoinfo1;
        for(int i=0;i<links;i++){
            if(bottleneck_i==i){
                info_ptr[i].bandwidth=bottleneck_bw;
            }else{
                info_ptr[i].bandwidth=non_bottleneck_bw;
            }
        }
    }
    LinkProperty topoinfo2[]={
        [0]={0,2,0,15},
        [1]={2,3,0,10},
        [2]={3,1,0,15},
        [3]={2,4,0,10},
        [4]={3,5,0,10},
    };
    {
        uint32_t bottleneck_bw=12*bw_unit;
        LinkProperty *info_ptr=topoinfo2;
        for(int i=0;i<links;i++){
            if(bottleneck_i==i){
                info_ptr[i].bandwidth=bottleneck_bw;
            }else{
                info_ptr[i].bandwidth=non_bottleneck_bw;
            }
        }
    }
    LinkProperty *topoinfo_ptr=nullptr;
    uint32_t buffer_ms=0;
    
    if(0==instance.compare("1")){
        topoinfo_ptr=topoinfo1;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=rtt;
    }else if(0==instance.compare("2")){
        topoinfo_ptr=topoinfo1;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=3*rtt/2;
    }else if(0==instance.compare("3")){
        topoinfo_ptr=topoinfo1;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=4*rtt/2;
    }else if(0==instance.compare("4")){
        topoinfo_ptr=topoinfo1;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=6*rtt/2;
    }else if(0==instance.compare("5")){
        topoinfo_ptr=topoinfo2;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=rtt;
    }else if(0==instance.compare("6")){
        topoinfo_ptr=topoinfo2;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=3*rtt/2;
    }else if(0==instance.compare("7")){
        topoinfo_ptr=topoinfo2;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=4*rtt/2;
    }else if(0==instance.compare("8")){
        topoinfo_ptr=topoinfo2;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=6*rtt/2;
    }else{
        topoinfo_ptr=topoinfo1;
        uint32_t rtt=CalMaxRttInDumbbell(topoinfo_ptr,links);
        buffer_ms=4*rtt/2;
    }
    //for utility
    TcpTracer::SetExperimentInfo(4,topoinfo_ptr[bottleneck_i].bandwidth);
    //for loss rate
    TcpTracer::SetLossRateFlag(true);
    NodeContainer topo=BuildDumbbellTopo(topoinfo_ptr,links,bottleneck_i,buffer_ms,triggerloss.get());
    uint16_t serv_port = 5000;

    //install server on h1
    Address tcp_sink_addr1;
    {
        Ptr<Node> host=topo.Get(1);
        Ptr<Ipv4> ipv4 = host->GetObject<Ipv4> ();
        Ipv4Address serv_ip= ipv4->GetAddress (1, 0).GetLocal();
        InetSocketAddress socket_addr=InetSocketAddress{serv_ip,serv_port};
        tcp_sink_addr1=socket_addr;
        Ptr<TcpServer> server=CreateObject<TcpServer>(tcp_sink_addr1);
        host->AddApplication(server);
        server->SetStartTime (Seconds (0.0));
    }
    //install server on h5
    Address tcp_sink_addr2;
    {
        Ptr<Node> host=topo.Get(5);
        Ptr<Ipv4> ipv4 = host->GetObject<Ipv4> ();
        Ipv4Address serv_ip= ipv4->GetAddress (1, 0).GetLocal();
        InetSocketAddress socket_addr=InetSocketAddress{serv_ip,serv_port};
        tcp_sink_addr2=socket_addr;
        Ptr<TcpServer> server=CreateObject<TcpServer>(tcp_sink_addr2);
        host->AddApplication(server);
        server->SetStartTime (Seconds (0.0));
    }
    
    uint64_t totalTxBytes = 100000*1500;
    // tcp client1 on h0
    {
        Ptr<Node> host=topo.Get(0);
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT|TcpClient::E_TRACE_RATE);
        host->AddApplication(client);
        client->ConfigurePeer(tcp_sink_addr1);
        client->SetCongestionAlgo(cc1);
        client->SetStartTime (Seconds (startTime));
        client->SetStopTime (Seconds (simDuration));
    }
/*
    // tcp client2 on h0
    {
        Ptr<Node> host=topo.Get(0);
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT|TcpClient::E_TRACE_RATE);
        host->AddApplication(client);
        client->ConfigurePeer(tcp_sink_addr1);
        client->SetCongestionAlgo(cc1);
        client->SetStartTime (Seconds (startTime));
        client->SetStopTime (Seconds (simDuration));
    }
    // tcp client3 on h4
    {
        Ptr<Node> host=topo.Get(4);
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT|TcpClient::E_TRACE_RATE);
        host->AddApplication(client);
        client->ConfigurePeer(tcp_sink_addr2);
        client->SetCongestionAlgo(cc2);
        client->SetStartTime (Seconds (startTime));
        client->SetStopTime (Seconds (simDuration));
    }
    // tcp client4 on h4
    {
        Ptr<Node> host=topo.Get(4);
        Ptr<TcpClient>  client= CreateObject<TcpClient> (totalTxBytes,TcpClient::E_TRACE_RTT|TcpClient::E_TRACE_INFLIGHT|TcpClient::E_TRACE_RATE);
        host->AddApplication(client);
        client->ConfigurePeer(tcp_sink_addr2);
        client->SetCongestionAlgo(cc2);
        client->SetStartTime (Seconds (startTime));
        client->SetStopTime (Seconds (simDuration));
    }
    */
    Simulator::Stop (Seconds (simDuration+10.0));
    Simulator::Run ();
    Simulator::Destroy ();
    return 0;
    
}
