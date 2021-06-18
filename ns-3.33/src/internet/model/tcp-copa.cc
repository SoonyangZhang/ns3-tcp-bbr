#include <limits>
#include <stdexcept>
#include <iostream>
#include "tcp-copa.h"
#include "tcp-bbr-debug.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
namespace ns3{
NS_LOG_COMPONENT_DEFINE ("TcpCopa");
NS_OBJECT_ENSURE_REGISTERED (TcpCopa);
namespace{
    uint32_t kMinCwndSegment=4;
    Time kRTTWindowLength=MilliSeconds(10000);
    Time kSrttWindowLength=MilliSeconds(100);
    uint32_t AddAndCheckOverflow(uint32_t value,const uint32_t toAdd,uint32_t label){
        if (std::numeric_limits<uint32_t>::max() - toAdd < value) {
            // TODO: the error code is CWND_OVERFLOW but this function can totally be
            // used for inflight bytes.
            std::cout<<value<<" "<<toAdd<<" "<<label<<std::endl;
            NS_ASSERT_MSG(0,"Overflow bytes in flight");
        }
        value +=(toAdd);
        return value;
    }
    template <class T1>
    void subtractAndCheckUnderflow(T1& value, const T1& toSub) {
        if (value < toSub) {
            // TODO: wrong error code
            throw std::runtime_error("Underflow bytes in flight");
        }
        value -=(toSub);
        return value;
    }
    inline uint64_t DivRoundUp(uint64_t a,uint64_t b){
        uint64_t value=(a+b-1)/b;
        return value;
    }
}
TypeId TcpCopa::GetTypeId (void){
    static TypeId tid = TypeId ("ns3::TcpCopa")
    .SetParent<TcpCongestionOps> ()
    .AddConstructor<TcpCopa> ()
    .SetGroupName ("Internet")
    .AddAttribute ("UseRttStanding",
                   "True to use rtt standing",
                   BooleanValue (false),
                   MakeBooleanAccessor (&TcpCopa::m_useRttStanding),
                   MakeBooleanChecker ())
    .AddAttribute ("Latencyfactor",
                   "Value of latency factor",
                   DoubleValue (0.05),
                   MakeDoubleAccessor (&TcpCopa::m_deltaParam),
                   MakeDoubleChecker<double> ())
    ;
  return tid;
}
TcpCopa::TcpCopa():TcpCongestionOps(),
m_minRttFilter(kRTTWindowLength.GetMicroSeconds(),Time(0),0),
m_standingRttFilter(kSrttWindowLength.GetMicroSeconds(),Time(0),0){
#if (TCP_COPA_DEGUG)
    m_debug=CreateObject<TcpBbrDebug>(GetName());
#endif
}

TcpCopa::TcpCopa (const TcpCopa &sock):TcpCongestionOps (sock),
m_minRttFilter(kRTTWindowLength.GetMicroSeconds(),Time(0),0),
m_standingRttFilter(kSrttWindowLength.GetMicroSeconds(),Time(0),0),
m_useRttStanding(sock.m_useRttStanding),
m_isSlowStart(sock.m_isSlowStart),
m_deltaParam(sock.m_deltaParam){
    m_lastCwndDoubleTime=Time(0);
#if (TCP_COPA_DEGUG)
    m_debug=sock.m_debug;
#endif
}
TcpCopa::~TcpCopa(){}
std::string TcpCopa::GetName () const{
    return "TcpCopa";
}
void TcpCopa::Init (Ptr<TcpSocketState> tcb){
    NS_ASSERT_MSG(tcb->m_pacing,"Enable pacing for Copa");
    InitPacingRateFromRtt(tcb,2.0);
}
uint32_t TcpCopa::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight){
    return tcb->m_cWnd;
}
void TcpCopa::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked){}
void TcpCopa::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt){}
void TcpCopa::CongestionStateSet (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCongState_t newState){
    
}
void TcpCopa::CwndEvent (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCAEvent_t event){}
bool TcpCopa::HasCongControl () const {return true;}
void TcpCopa::CongControl (Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs){
    Time rtt=rs.m_rtt;
    Time srtt=tcb->m_lastRtt;
    Time event_time=Simulator::Now();
    m_minRttFilter.Update(rtt,event_time.GetMicroSeconds());
    auto rtt_min=m_minRttFilter.GetBest();
    std::string rtt_str=std::to_string(rtt.GetMilliSeconds());
    NS_ASSERT(Time(0)!=rtt_min);
    if(m_useRttStanding){
        m_standingRttFilter.SetWindowLength(srtt.GetMicroSeconds());
    }else{
        m_standingRttFilter.SetWindowLength(srtt.GetMicroSeconds()/2);
    }
    m_standingRttFilter.Update(rtt,event_time.GetMicroSeconds());
    Time rttStanding=m_standingRttFilter.GetBest();
    NS_ASSERT(Time(0)!=rttStanding);
    if(rttStanding<rtt_min){
        return ;
    }
    if(rs.m_ackedSacked){
        m_ackBytesRound+=rs.m_ackedSacked;
    }
    uint64_t delay_us;
    uint32_t packet_size=1500;
    uint32_t cwnd_bytes=tcb->m_cWnd;
    if(m_useRttStanding){
        delay_us=rttStanding.GetMicroSeconds()-rtt_min.GetMicroSeconds();
    }else{
        delay_us=rtt.GetMicroSeconds()-rtt_min.GetMicroSeconds();
    }
    if(Time(0)==rttStanding){
        NS_LOG_FUNCTION("zero standing rtt"<<rtt);
        return ;
    }
    bool increase_cwnd=false;
    if(0==delay_us){
        increase_cwnd=true;
    }else{
        double target_rate=1.0*packet_size*1000000/(delay_us*m_deltaParam);
        double current_rate=1.0*cwnd_bytes*1000000/rttStanding.GetMicroSeconds();
        if(target_rate>=current_rate){
            increase_cwnd=true;
        }
    }
    
    if(!(increase_cwnd&&m_isSlowStart)){
        CheckAndUpdateDirection(event_time,srtt,cwnd_bytes);
    }
    if(increase_cwnd){
        if(m_isSlowStart){
        // When a flow starts, Copa performs slow-start where
        // cwnd doubles once per RTT until current rate exceeds target rate".
            if(Time(0)==m_lastCwndDoubleTime){
                m_lastCwndDoubleTime=event_time;
            }else if(event_time-m_lastCwndDoubleTime>srtt){
                uint32_t addition=0;
                if(m_ackBytesRound>0){
                    addition=cwnd_bytes;
                }
                uint32_t new_cwnd=AddAndCheckOverflow(cwnd_bytes,addition,154);
                m_ackBytesRound=0;
                tcb->m_cWnd=new_cwnd;
                m_lastCwndDoubleTime=event_time;
            }
        }else{
            if(m_velocityState.direction!=VelocityState::Direction::Up
                &&m_velocityState.velocity>1){
            // if our current rate is much different than target, we double v every
            // RTT. That could result in a high v at some point in time. If we
            // detect a sudden direction change here, while v is still very high but
            // meant for opposite direction, we should reset it to 1.
                ChangeDirection(event_time,VelocityState::Direction::Up,cwnd_bytes);
            }
            uint32_t mss=tcb->m_segmentSize;
            uint32_t acked_packets=DivRoundUp(rs.m_ackedSacked,mss);
            uint32_t addition=0;
            if(acked_packets){
                addition=(acked_packets*mss*mss*m_velocityState.velocity)/(m_deltaParam*cwnd_bytes);
            }
            uint32_t new_cwnd=AddAndCheckOverflow(cwnd_bytes,addition,174);
            m_ackBytesRound=0;
            tcb->m_cWnd=new_cwnd;
        }
    }else{
        if(m_velocityState.direction!=VelocityState::Direction::Down&&m_velocityState.velocity>1){
            ChangeDirection(event_time,VelocityState::Direction::Down,cwnd_bytes);
        }
        uint32_t mss=tcb->m_segmentSize;
        uint32_t acked_packets=DivRoundUp(rs.m_ackedSacked,mss);
        uint32_t reduction=0;
        if(acked_packets){
            reduction=(acked_packets*mss*mss*m_velocityState.velocity)/(m_deltaParam*cwnd_bytes);
        }
        if(cwnd_bytes<reduction){
            reduction=cwnd_bytes;
        }
        uint32_t new_cwnd=cwnd_bytes-reduction;
        new_cwnd=std::max<uint32_t>(new_cwnd,kMinCwndSegment*mss);
        tcb->m_cWnd=new_cwnd;
        m_isSlowStart=false;
        m_ackBytesRound=0;
    }
    SetPacingRate(tcb,2.0);
#if (TCP_COPA_DEGUG)
     m_debug->GetStram()<<event_time.GetSeconds()<<"\t"<<BandwidthEstimate(tcb).GetBitRate()
                        <<"\t"<<rtt.GetMilliSeconds()<<std::endl;
#endif
}
Ptr<TcpCongestionOps> TcpCopa::Fork (){
    return CopyObject<TcpCopa> (this);
}
void TcpCopa::InitPacingRateFromRtt(Ptr<TcpSocketState> tcb,float gain){
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
void TcpCopa::SetPacingRate(Ptr<TcpSocketState> tcb,float gain){
    InitPacingRateFromRtt(tcb,gain);
}
DataRate TcpCopa::BandwidthEstimate(Ptr<TcpSocketState> tcb){
    Time srtt=tcb->m_lastRtt;
    DataRate rate;
    double bps=0.0;
    if(srtt!=Time(0)){
        bps=1.0*tcb->m_cWnd*8000/srtt.GetMilliSeconds();
    }
    rate=DataRate(bps);
    return rate;
}
/**
 * Once per window, the sender
 * compares the current cwnd to the cwnd value at
 *  the time that the latest acknowledged packet was
 *  sent (i.e., cwnd at the start of the current window).
 *  If the current cwnd is larger, then set direction to
 *  'up'; if it is smaller, then set direction to 'down'.
 *  Now, if direction is the same as in the previous
 *  window, then double v. If not, then reset v to 1.
 *  However, start doubling v only after the direction
 *  has remained the same for three RTTs
 */
void TcpCopa::CheckAndUpdateDirection(Time event_time,Time srtt,uint32_t cwnd_bytes){
    if(Time(0)==m_velocityState.lastCwndRecordTime){
        m_velocityState.lastCwndRecordTime=event_time;
        m_velocityState.lastRecordedCwndBytes=cwnd_bytes;
        return ;
    }
    NS_ASSERT(event_time>=m_velocityState.lastCwndRecordTime);
    auto elapsed_time=event_time-m_velocityState.lastCwndRecordTime;
    
    if(elapsed_time>=srtt){
        VelocityState::Direction new_direction=cwnd_bytes>m_velocityState.lastRecordedCwndBytes
                           ? VelocityState::Direction::Up
                           : VelocityState::Direction::Down;
        if(new_direction!=m_velocityState.direction){
            m_velocityState.velocity=1;
            m_velocityState.numTimesDirectionSame=0;
        }else{
            m_velocityState.numTimesDirectionSame++;
            uint64_t velocityDirectionThreshold = 3;
            if(m_useRttStanding){
                velocityDirectionThreshold = 4;
            }
            if( m_velocityState.numTimesDirectionSame>=velocityDirectionThreshold){
                m_velocityState.velocity=2*m_velocityState.velocity;
            }
        }
        
        m_velocityState.direction=new_direction;
        m_velocityState.lastCwndRecordTime=event_time;
        m_velocityState.lastRecordedCwndBytes=cwnd_bytes;
    }
}
void TcpCopa::ChangeDirection(Time event_time, VelocityState::Direction new_direction,uint32_t cwnd_bytes){
    if(new_direction==m_velocityState.direction){
        return ;
    }
    
    m_velocityState.direction=new_direction;
    m_velocityState.velocity=1;
    m_velocityState.numTimesDirectionSame=0;
    m_velocityState.lastRecordedCwndBytes=cwnd_bytes;
}

}

