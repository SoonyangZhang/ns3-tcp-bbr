#include <limits>
#include <stdexcept>
#include "tcp-copa2.h"
#include "tcp-bbr-debug.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
namespace ns3{
NS_LOG_COMPONENT_DEFINE ("TcpCopa2");
NS_OBJECT_ENSURE_REGISTERED (TcpCopa2);
namespace{
    uint32_t kMinCwndSegment=4;
    Time kCopa2MinRttWindowLength=MilliSeconds(10000);
    Time  kCopa2ProbeRttInterval=MilliSeconds(8000);
}
TypeId TcpCopa2::GetTypeId (void){
    static TypeId tid = TypeId ("ns3::TcpCopa2")
    .SetParent<TcpCongestionOps> ()
    .AddConstructor<TcpCopa2> ()
    .SetGroupName ("Internet")
    ;
  return tid;
}
TcpCopa2::TcpCopa2():TcpCongestionOps(),
m_minRttFilter(kCopa2MinRttWindowLength.GetMicroSeconds(),Time(0),0){
#if (TCP_COPA2_DEGUG)
    m_debug=CreateObject<TcpBbrDebug>(GetName());
#endif
}

TcpCopa2::TcpCopa2 (const TcpCopa2 &sock):TcpCongestionOps(sock),
m_minRttFilter(kCopa2MinRttWindowLength.GetMicroSeconds(),Time(0),0){
#if (TCP_COPA2_DEGUG)
    m_debug=sock.m_debug;
#endif
}
TcpCopa2::~TcpCopa2(){}
std::string TcpCopa2::GetName () const{
    return "TcpCopa2";
}
void TcpCopa2::Init (Ptr<TcpSocketState> tcb){
    NS_ASSERT_MSG(tcb->m_pacing,"Enable pacing for Copa2");
    InitPacingRateFromRtt(tcb,2.0);
}
uint32_t TcpCopa2::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight){
    return tcb->m_cWnd;
}
void TcpCopa2::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked){}
void TcpCopa2::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt){}
void TcpCopa2::CongestionStateSet (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCongState_t newState){
    
}
void TcpCopa2::CwndEvent (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCAEvent_t event){}
bool TcpCopa2::HasCongControl () const {return true;}
void TcpCopa2::CongControl (Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs){
    Time event_time=Simulator::Now();
    uint32_t mss=tcb->m_segmentSize;
    if(m_lastProbeRtt.IsZero()){
        m_lastProbeRtt=event_time;
    }
    
    Time rtt=rs.m_rtt;
    m_minRttFilter.Update(rtt,event_time.GetMicroSeconds());
    auto rtt_min=m_minRttFilter.GetBest();
    NS_ASSERT(Time(0)!=rtt_min);
    
    m_bytesAckedInCycle+=rs.m_ackedSacked;
    m_lostBytesCount+=rs.m_bytesLoss;
    UpdateLossMode(tcb,rc,rs);

    auto dParam=rtt_min;

    if(m_lossyMode){
        dParam=MicroSeconds(rtt_min.GetMicroSeconds()*2*m_lossToleranceParam);
    }
    auto cycle_dur = rtt_min+ dParam;
    
    if (m_probeRtt){
        // Do we exit probe RTT now?
        if (m_lastProbeRtt+ dParam <=event_time) {
            // Note, probe rtt should ideally never decrease ack rate, since
            // it just barely empties the queue. Hence all ack rate samples
            // are good independent of whether we probed for rtt
            m_probeRtt= false;
        }
    } else {
        // See if we need to enter probe rtt mode
        auto interval = kCopa2ProbeRttInterval /
            (rtt<rtt_min+ dParam ? 2 : 1);
        if (m_lastProbeRtt+ interval <=event_time) {
            m_probeRtt=true;
            m_lastProbeRtt =event_time;
        }
    }

    if(m_cycleStart.IsZero()){
        m_cycleStart=event_time;
        SetPacingRate(tcb,2.0);
        #if (TCP_COPA2_DEGUG)
    m_debug->GetStram()<<event_time.GetSeconds()<<"\t"<<BandwidthEstimate(tcb).GetBitRate()
                       <<"\t"<<rtt.GetMilliSeconds()<<std::endl;
        #endif
        return ;
    }
    if(m_cycleStart+cycle_dur>event_time){
        SetPacingRate(tcb,2.0);
        #if (TCP_COPA2_DEGUG)
    m_debug->GetStram()<<event_time.GetSeconds()<<"\t"<<BandwidthEstimate(tcb).GetBitRate()
                       <<"\t"<<rtt.GetMilliSeconds()<<std::endl;
        #endif
        return ;
    }
    
    uint32_t target_cwnd=m_bytesAckedInCycle+m_alphaParam*mss;
    uint32_t cwnd=tcb->m_cWnd;
    if(!rs.m_isAppLimited||cwnd<target_cwnd){
        // If CC was app limited, don't decrease cwnd
        cwnd=target_cwnd;
    }
    uint32_t min_cwnd=kMinCwndSegment*mss;
    if(m_probeRtt||cwnd<min_cwnd){
        cwnd=min_cwnd;
    }
    tcb->m_cWnd=cwnd;
    SetPacingRate(tcb,2.0);
    m_cycleStart=event_time;
    m_bytesAckedInCycle=0;
#if (TCP_COPA2_DEGUG)
    m_debug->GetStram()<<event_time.GetSeconds()<<"\t"<<BandwidthEstimate(tcb).GetBitRate()
                       <<"\t"<<rtt.GetMilliSeconds()<<std::endl;
#endif
}
Ptr<TcpCongestionOps> TcpCopa2::Fork (){
    return CopyObject<TcpCopa2> (this);
}
void TcpCopa2::InitPacingRateFromRtt(Ptr<TcpSocketState> tcb,float gain){
    uint32_t cwnd_bytes=tcb->m_cWnd;
    Time rtt=tcb->m_lastRtt;
    double bps=1000000;
    DataRate pacing_rate;
    if(rtt!=Time(0)){
        bps=1.0*cwnd_bytes*8000/rtt.GetMilliSeconds();
    }
    pacing_rate=DataRate(gain*bps);
    if(pacing_rate>tcb->m_maxPacingRate){
        pacing_rate=tcb->m_maxPacingRate;
    }
    tcb->m_pacingRate=pacing_rate;
}
void TcpCopa2::SetPacingRate(Ptr<TcpSocketState> tcb,float gain){
    InitPacingRateFromRtt(tcb,gain);
}
void TcpCopa2::UpdateLossMode(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs){
    if(rs.m_priorDelivered<m_lossRoundDelivered){
        return ;
    }
    uint32_t mss=tcb->m_segmentSize;
    uint64_t last_delivered=m_lossRoundDelivered;
    uint32_t acked_bytes_round=rc.m_delivered-last_delivered;
    uint32_t lost_bytes_round=m_lostBytesCount-m_priorLostBytes;
    uint32_t all_bytes_round=acked_bytes_round+lost_bytes_round;
    if(((1.0*all_bytes_round)<(2.0*mss/m_lossToleranceParam))&&(lost_bytes_round<2*mss)){
        return ;
    }
    /* Now do per-round-trip updates. */
    m_lossRoundDelivered=rc.m_delivered;
    m_priorLostBytes=m_lostBytesCount;
    if(1.0*lost_bytes_round>=all_bytes_round*m_lossToleranceParam){
        m_lossyMode=true;
    }else{
        m_lossyMode=false;
    }
}
DataRate TcpCopa2::BandwidthEstimate(Ptr<TcpSocketState> tcb){
    Time srtt=tcb->m_lastRtt;
    DataRate rate;
    double bps=0.0;
    if(srtt!=Time(0)){
        bps=1.0*tcb->m_cWnd*8000/srtt.GetMilliSeconds();
    }
    rate=DataRate(bps);
    return rate;
}
}

