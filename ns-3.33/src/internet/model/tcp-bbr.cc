#include <unistd.h>
#include <time.h>
#include <limits>
#include <algorithm>
#include "tcp-bbr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "tcp-bbr-debug.h"
namespace ns3{
NS_LOG_COMPONENT_DEFINE ("TcpBbr");
NS_OBJECT_ENSURE_REGISTERED (TcpBbr);
namespace {
// Constants based on TCP defaults.
// The minimum CWND to ensure delayed acks don't reduce bandwidth measurements.
// Does not inflate the pacing rate.
const uint32_t kMinCWndSegment= 4;

// The gain used for the STARTUP, equal to 2/ln(2).
const double kDefaultHighGain = 2.885f;
// The newly derived gain for STARTUP, equal to 4 * ln(2)
const double kDerivedHighGain = 2.773f;
// The newly derived CWND gain for STARTUP, 2.
const double kDerivedHighCWNDGain = 2.0f;
// The cycle of gains used during the PROBE_BW stage.
const double kPacingGain[] = {1.25, 0.75, 1, 1, 1, 1, 1, 1};
// The length of the gain cycle.
const size_t kGainCycleLength = sizeof(kPacingGain) / sizeof(kPacingGain[0]);
// The size of the bandwidth filter window, in round-trips.
const uint64_t kBandwidthWindowSize = kGainCycleLength + 2;

const double kCWNDGainConstant=2.0;
// The time after which the current min_rtt value expires.
const Time kMinRttExpiry =Seconds(10);
// The minimum time the connection can spend in PROBE_RTT mode.
const Time kProbeRttTime = MilliSeconds(200);

const double kStartupGrowthTarget=1.25;

static const int bbr_pacing_margin_percent = 1;

/* But after 3 rounds w/o significant bw growth, estimate pipe is full: */
static const uint32_t bbr_full_bw_cnt = 3;

/* "long-term" ("LT") bandwidth estimator parameters... */
/* The minimum number of rounds in an LT bw sampling interval: */
static const uint32_t bbr_lt_intvl_min_rtts = 4;
/* If lost/delivered ratio > 20%, interval is "lossy" and we may be policed: */
static const uint32_t bbr_lt_loss_thresh_num=2;
static const uint32_t bbr_lt_loss_thresh_den=10;
/* If 2 intervals have a bw ratio <= 1/8, their bw is "consistent": */
static const double bbr_lt_bw_ratio = 0.125;
/* If 2 intervals have a bw diff <= 4 Kbit/sec their bw is "consistent": */
static const DataRate bbr_lt_bw_diff =DataRate(4000);
/* If we estimate we're policed, use lt_bw for this many round trips: */
const uint32_t bbr_lt_bw_max_rtts =48;

/* Gain factor for adding extra_acked to target cwnd: */
static const double bbr_extra_acked_gain = 1.0;
/* Window length of extra_acked window. */
static const uint32_t bbr_extra_acked_win_rtts = 5;
/* Max allowed val for ack_epoch_acked, after which sampling epoch is reset */
static const uint32_t bbr_ack_epoch_acked_reset_thresh = 1U << 20;
/* Time period for clamping cwnd increment due to ack aggregation */
static const Time bbr_extra_acked_max_time = MilliSeconds(100);
}  // namespace
TypeId TcpBbr::GetTypeId (void){
    static TypeId tid = TypeId ("ns3::TcpBbr")
    .SetParent<TcpCongestionOps> ()
    .AddConstructor<TcpBbr> ()
    .SetGroupName ("Internet")
    .AddAttribute ("HighGain",
                   "Value of high gain",
                   DoubleValue (kDefaultHighGain),
                   MakeDoubleAccessor (&TcpBbr::m_highGain),
                   MakeDoubleChecker<double> ())
  ;
  return tid;
}
TcpBbr::TcpBbr():TcpCongestionOps(),
m_maxBwFilter(kBandwidthWindowSize,DataRate(0),0){
    m_uv = CreateObject<UniformRandomVariable> ();
    m_uv->SetStream(time(NULL));
#if (TCP_BBR_DEGUG)
    m_debug=CreateObject<TcpBbrDebug>(GetName());
#endif
}
TcpBbr::TcpBbr(const TcpBbr &sock):TcpCongestionOps(sock),
m_maxBwFilter(kBandwidthWindowSize,DataRate(0),0),
m_highGain(sock.m_highGain){
    m_uv = CreateObject<UniformRandomVariable> ();
    m_uv->SetStream(time(NULL));
#if (TCP_BBR_DEGUG)
    m_debug=sock.m_debug;
#endif
}
TcpBbr::~TcpBbr(){}
std::string TcpBbr::ModeToString(uint8_t mode){
    switch(mode){
        case STARTUP:
            return "statrtup";
        case DRAIN:
            return "drain";
        case PROBE_BW:
            return "probe_bw";
        case PROBE_RTT:
            return "probe_rtt";
    }
    return "???";
}
std::string TcpBbr::GetName () const{
    return "TcpBbr";
}
void TcpBbr::Init (Ptr<TcpSocketState> tcb){
    NS_ASSERT_MSG(tcb->m_pacing,"Enable pacing for BBR");
    Time now=Simulator::Now();
    m_delivered=0;
    m_deliveredTime=now;
    m_bytesLost=0;
    
    m_minRtt=Time::Max();
    m_minRttStamp =now;
    m_probeRttDoneStamp=Time(0);
    
    m_priorCwnd=0;
    m_roundTripCount=0;
    m_nextRttDelivered=0;
    m_cycleStamp=Time(0);
    
    m_prevCongState=TcpSocketState::CA_OPEN;
    m_packetConservation=0;
    m_roundStart=0;
    m_idleRestart=0;
    m_probeRttRoundDone=0;
    
    m_ltIsSampling=0;
    m_ltRttCount=0;
    m_ltUseBandwidth=0;
    m_ltBandwidth=0;
    m_ltLastDelivered=0;  //when 0, reset lt sampling in CongControl
    m_ltLastStamp=Time(0);
    m_ltLastLost=0;
    
    m_pacingGain=m_highGain;
    m_cWndGain=m_highGain;
    
    m_fullBanwidthReached=0;
    m_fullBandwidthCount=0;
    m_cycleIndex=0;
    m_hasSeenRtt=0;
    
    m_priorCwnd=0;
    m_fullBandwidth=0;
    
    m_ackEpochStamp=Time(0);
    m_extraAckedBytes[0]=0;
    m_extraAckedBytes[1]=0;
    m_ackEpochAckedBytes=0;
    m_extraAckedWinRtts=0;
    m_extraAckedWinIdx=0;
    
    ResetStartUpMode();
    InitPacingRateFromRtt(tcb);
}
uint32_t TcpBbr::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight){
    SaveCongestionWindow(tcb->m_cWnd);
    return tcb->m_ssThresh;
}
void TcpBbr::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked){}
void TcpBbr::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt){}
void TcpBbr::CongestionStateSet (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCongState_t newState){
    if(TcpSocketState::CA_LOSS==newState){
        TcpRateOps::TcpRateSample rs;
        rs.m_bytesLoss=1;
        m_prevCongState=TcpSocketState::CA_LOSS;
        m_fullBandwidth=0;
        m_roundStart=1;
        LongTermBandwidthSampling(tcb,rs);
        bool use_lt=m_ltUseBandwidth;
        #if (TCP_BBR_DEGUG)
        NS_LOG_INFO(m_debug->GetUuid()<<" rx time out "<<use_lt);
        #endif
    }
}
void TcpBbr::CwndEvent (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCAEvent_t event){}
bool TcpBbr::HasCongControl () const{
    return true;
}
void TcpBbr::CongControl (Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs){
    NS_ASSERT(rc.m_delivered>=m_delivered);
    //NS_ASSERT(rc.m_deliveredTime>=m_deliveredTime);
    if(rc.m_delivered>=m_delivered){
        m_delivered=rc.m_delivered;
    }
    if(rc.m_deliveredTime>=m_deliveredTime){
        m_deliveredTime=rc.m_deliveredTime;
    }  
    if(rs.m_bytesLoss>0){
        m_bytesLost+=rs.m_bytesLoss;
    }
    if(0==m_ltLastDelivered){
        ResetLongTermBandwidthSampling();
    }
    if(m_ackEpochStamp.IsZero()){
        m_ackEpochStamp=rc.m_deliveredTime;
    }
    NS_ASSERT(!m_ackEpochStamp.IsZero());
    UpdateModel(tcb,rc,rs);
    DataRate bw=BbrBandwidth();
    SetPacingRate(tcb,bw,m_pacingGain);
    SetCongestionWindow(tcb,rc,rs,bw,m_cWndGain);
    LogDebugInfo(tcb,rc,rs);
}
Ptr<TcpCongestionOps> TcpBbr::Fork (){
    return CopyObject<TcpBbr> (this);
}
void TcpBbr::AssignStreams (int64_t stream){
    if(m_uv){
        m_uv->SetStream(stream);
    }
}
DataRate TcpBbr::BbrMaxBandwidth() const{
    return m_maxBwFilter.GetBest();
}
DataRate TcpBbr::BbrBandwidth() const{
    DataRate bw=m_maxBwFilter.GetBest();
    if(m_ltUseBandwidth){
        bw=m_ltBandwidth;
        #if (TCP_BBR_DEGUG)
        //NS_LOG_INFO(m_debug->GetUuid()<<"lt bw "<<m_ltBandwidth);
        #endif 
    }
    return bw;
}
bool TcpBbr::BbrFullBandwidthReached() const{
    return m_fullBanwidthReached;
}
uint64_t TcpBbr::BbrExtraAcked() const{
    return std::max<uint64_t>(m_extraAckedBytes[0],m_extraAckedBytes[1]);
}
DataRate TcpBbr::BbrRate(DataRate bw,double gain) const{
    double bps=gain*bw.GetBitRate();
    double value=bps*(100-bbr_pacing_margin_percent)/100;
    return DataRate(value);
}
DataRate TcpBbr::BbrBandwidthToPacingRate(Ptr<TcpSocketState> tcb,DataRate bw,double gain) const{
    DataRate rate=BbrRate(bw,gain);
    if(rate>tcb->m_maxPacingRate){
        rate=tcb->m_maxPacingRate;
    }
    return rate;
}
void TcpBbr::InitPacingRateFromRtt(Ptr<TcpSocketState> tcb){
    uint32_t mss=tcb->m_segmentSize;
    uint32_t congestion_window=tcb->m_cWnd;
    if(congestion_window<tcb->m_initialCWnd*mss){
        congestion_window=tcb->m_initialCWnd*mss;
    }
    Time rtt=tcb->m_lastRtt;
    DataRate bw(1000000);
    DataRate pacing_rate=bw;
    if(Time(0)==rtt){
        pacing_rate=BbrBandwidthToPacingRate(tcb,bw,m_highGain);
    }else{
        m_hasSeenRtt=1;
        double bps=1.0*congestion_window*8000/rtt.GetMilliSeconds();
        bw=DataRate(bps);
        pacing_rate=BbrBandwidthToPacingRate(tcb,bw,m_highGain);
    }
#if (TCP_BBR_DEGUG)
    NS_LOG_FUNCTION(m_debug->GetUuid()<<rtt.GetMilliSeconds()<<pacing_rate<<m_highGain);
#endif
    if(pacing_rate>tcb->m_maxPacingRate){
        pacing_rate=tcb->m_maxPacingRate;
    }
    tcb->m_pacingRate=pacing_rate;
}
void TcpBbr::SetPacingRate(Ptr<TcpSocketState> tcb,DataRate bw, double gain){
    DataRate rate=BbrBandwidthToPacingRate(tcb,bw,gain);
    Time last_rtt=tcb->m_lastRtt;
    if(!m_hasSeenRtt&&(!last_rtt.IsZero())){
        InitPacingRateFromRtt(tcb);
    }
    if(BbrFullBandwidthReached()||rate>tcb->m_pacingRate){
        if(BbrFullBandwidthReached()){
            NS_ASSERT_MSG(rate>0,"rate is zero");
        }
        tcb->m_pacingRate=rate;
    }
}
void TcpBbr::ResetLongTermBandwidthSamplingInterval(){
    m_ltLastStamp=m_deliveredTime;
    m_ltLastDelivered=m_delivered;
    m_ltLastLost=m_bytesLost;
    m_ltRttCount=0;
}
void TcpBbr::ResetLongTermBandwidthSampling(){
    m_ltBandwidth=0;
    m_ltUseBandwidth=0;
    m_ltIsSampling=false;
    ResetLongTermBandwidthSamplingInterval();
}
/* Long-term bw sampling interval is done. Estimate whether we're policed. */
void TcpBbr::LongTermBandwidthIntervalDone(Ptr<TcpSocketState> tcb,DataRate bw){
    if(m_ltBandwidth>0){
        DataRate diff=0;
        if(m_ltBandwidth>=bw){
            uint64_t value=m_ltBandwidth.GetBitRate()-bw.GetBitRate();
            diff=DataRate(value);
        }else{
            uint64_t value=bw.GetBitRate()-m_ltBandwidth.GetBitRate();
            diff=DataRate(value);
        }
        if((diff.GetBitRate()<=bbr_lt_bw_ratio*m_ltBandwidth.GetBitRate())||(BbrRate(diff,1.0)<=bbr_lt_bw_diff)){
            /* All criteria are met; estimate we're policed. */
            uint64_t average=(bw.GetBitRate()+m_ltBandwidth.GetBitRate())/2;
            m_ltBandwidth=DataRate(average);
            m_ltUseBandwidth=1;
            m_pacingGain=1.0;
            m_ltRttCount=0;
            return ;
        }
    }
    m_ltBandwidth=bw;
    ResetLongTermBandwidthSamplingInterval();
}
void TcpBbr::LongTermBandwidthSampling(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateSample &rs){
    uint32_t lost,delivered;
    if(m_ltUseBandwidth){
        if(PROBE_BW==m_mode&&m_roundStart&&(++m_ltRttCount)>=bbr_lt_bw_max_rtts){
            ResetLongTermBandwidthSampling();
            ResetProbeBandwidthMode();
        }
        return ;
    }

    /* Wait for the first loss before sampling, to let the policer exhaust
    * its tokens and estimate the steady-state rate allowed by the policer.
    * Starting samples earlier includes bursts that over-estimate the bw.
    */
    if(!m_ltIsSampling){
        if(!rs.m_bytesLoss){
            return ;
        }
        ResetLongTermBandwidthSamplingInterval();
        m_ltIsSampling=true;
    }
    
    /* To avoid underestimates, reset sampling if we run out of data. */
    if(rs.m_isAppLimited){
        ResetLongTermBandwidthSampling();
        return ;
    }
    
    if(m_roundStart){
        m_ltRttCount++; /* count round trips in this interval */
    }
    if(m_ltRttCount<bbr_lt_intvl_min_rtts){
        return ; /* sampling interval needs to be longer */
    }
    if(m_ltRttCount>4 * bbr_lt_intvl_min_rtts){
        ResetLongTermBandwidthSampling(); /* interval is too long */
        return ;
    }
 
    /* End sampling interval when a packet is lost, so we estimate the
    * policer tokens were exhausted. Stopping the sampling before the
    * tokens are exhausted under-estimates the policed rate.
    */
    if(!rs.m_bytesLoss){
        return ;
    }
    lost=m_bytesLost-m_ltLastLost;
    delivered=m_delivered-m_ltLastDelivered;
    /* Is loss rate (lost/delivered) >= lt_loss_thresh? If not, wait. */
    if(!delivered||(lost*bbr_lt_loss_thresh_den<delivered*bbr_lt_loss_thresh_num)){
        return ;
    }
    
    /* Find average delivery rate in this sampling interval. */
    Time t=m_deliveredTime-m_ltLastStamp;
    if(t<MilliSeconds(1)){
        return ;    /* interval is less than one ms, so wait */
    }
    uint32_t value=std::numeric_limits<uint32_t>::max()/1000;
    if(t>=MilliSeconds(value)){
        ResetLongTermBandwidthSampling(); /* interval too long; reset */
        #if (TCP_BBR_DEGUG)
        NS_LOG_FUNCTION(m_debug->GetUuid()<<"interval too long");
        #endif
        return ;
    }
    double bps=1.0*delivered*8000/t.GetMilliSeconds();
    DataRate bw(bps);
    #if (TCP_BBR_DEGUG)
    NS_LOG_FUNCTION(m_debug->GetUuid()<<delivered<<t.GetMilliSeconds()<<bw<<BbrMaxBandwidth());
    #endif
    LongTermBandwidthIntervalDone(tcb,bw);
}
void TcpBbr::UpdateBandwidth(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,const TcpRateOps::TcpRateSample &rs){
    m_roundStart=0;
    if(rs.m_delivered<=0||rs.m_interval.IsZero()||rs.m_priorTime.IsZero()){
        return ;
    }
    
    if(rs.m_priorDelivered>=m_nextRttDelivered){
        m_nextRttDelivered=rc.m_delivered;
        m_roundTripCount++;
        m_roundStart=1;
        m_packetConservation=0;
    }
    LongTermBandwidthSampling(tcb,rs);
    /* Divide delivered by the interval to find a (lower bound) bottleneck
    * bandwidth sample. Delivered is in packets and interval_us in uS and
    * ratio will be <<1 for most connections. So delivered is first scaled.
    */
    DataRate bw=rs.m_deliveryRate;
    /* If this sample is application-limited, it is likely to have a very
    * low delivered count that represents application behavior rather than
    * the available network rate. Such a sample could drag down estimated
    * bw, causing needless slow-down. Thus, to continue to send at the
    * last measured network rate, we filter out app-limited samples unless
    * they describe the path bw at least as well as our bw model.
    *
    * So the goal during app-limited phase is to proceed with the best
    * network rate no matter how long. We automatically leave this
    * phase when app writes faster than the network can deliver :)
    */
    if(!rs.m_isAppLimited||bw>=m_maxBwFilter.GetBest()){
        m_maxBwFilter.Update(bw,m_roundTripCount);
    }
}
/* Estimates the windowed max degree of ack aggregation.
 * This is used to provision extra in-flight data to keep sending during
 * inter-ACK silences.
 *
 * Degree of ack aggregation is estimated as extra data acked beyond expected.
 *
 * max_extra_acked = "maximum recent excess data ACKed beyond max_bw * interval"
 * cwnd += max_extra_acked
 *
 * Max extra_acked is clamped by cwnd and bw * bbr_extra_acked_max_us (100 ms).
 * Max filter is an approximate sliding window of 5-10 (packet timed) round
 * trips.
 */
void TcpBbr::UpdateAckAggregation(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                                const TcpRateOps::TcpRateSample &rs){
    Time epoch_time(0);
    uint64_t  expected_acked_bytes=0, extra_acked_bytes=0;
    uint64_t reset_thresh_bytes=bbr_ack_epoch_acked_reset_thresh*tcb->m_segmentSize;
    if(0==bbr_extra_acked_gain||rs.m_ackedSacked==0||rs.m_delivered<=0||rs.m_interval.IsZero()){
        return ;
    }
    if(m_roundStart){
        m_extraAckedWinRtts=std::min<uint32_t>(0x1F,m_extraAckedWinRtts+1);
        if(m_extraAckedWinRtts>=bbr_extra_acked_win_rtts){
            m_extraAckedWinRtts=0;
            m_extraAckedWinIdx=m_extraAckedWinIdx?0:1;
            m_extraAckedBytes[m_extraAckedWinIdx]=0;
        }
    }
    NS_ASSERT(rc.m_deliveredTime>=m_ackEpochStamp);
    /* Compute how many packets we expected to be delivered over epoch. */
    epoch_time=rc.m_deliveredTime-m_ackEpochStamp;
    double bytes=BbrBandwidth()*epoch_time/8;
    expected_acked_bytes=bytes;
    /* Reset the aggregation epoch if ACK rate is below expected rate or
    * significantly large no. of ack received since epoch (potentially
    * quite old epoch).
    */
    if(m_ackEpochAckedBytes<=expected_acked_bytes||(m_ackEpochAckedBytes+rs.m_ackedSacked>=reset_thresh_bytes)){
        m_ackEpochAckedBytes=0;
        m_ackEpochStamp=rc.m_deliveredTime;
        expected_acked_bytes=0;
    }
    /* Compute excess data delivered, beyond what was expected. */
    uint32_t mss=tcb->m_segmentSize;
    uint64_t limit=0xFFFFF*mss;
    m_ackEpochAckedBytes=std::min<uint64_t>(limit,m_ackEpochAckedBytes+rs.m_ackedSacked);
    extra_acked_bytes=m_ackEpochAckedBytes-expected_acked_bytes;
    if(extra_acked_bytes>tcb->m_cWnd){
        #if (TCP_BBR_DEGUG)
        //NS_LOG_FUNCTION(m_debug->GetUuid()<<"sub"<<extra_acked_bytes<<tcb->m_cWnd<<ModeToString(m_mode));
        #endif
        extra_acked_bytes=tcb->m_cWnd;
    }
    if(extra_acked_bytes>m_extraAckedBytes[m_extraAckedWinIdx]){
        m_extraAckedBytes[m_extraAckedWinIdx]=extra_acked_bytes;
    }
}
void TcpBbr::CheckFullBandwidthReached(const TcpRateOps::TcpRateSample &rs){
    if(BbrFullBandwidthReached()||!m_roundStart||rs.m_isAppLimited){
        return;
    }
    double value=kStartupGrowthTarget*m_fullBandwidth.GetBitRate();
    DataRate target(value);
    DataRate bw=m_maxBwFilter.GetBest();
    if (bw>=target){
        m_fullBandwidth=bw;
        m_fullBandwidthCount=0;
        return ;
    }
    ++m_fullBandwidthCount;
    m_fullBanwidthReached=m_fullBandwidthCount>=bbr_full_bw_cnt;
    
}
void TcpBbr::CheckDrain(Ptr<TcpSocketState> tcb){
    if(STARTUP==m_mode&&BbrFullBandwidthReached()){
        m_mode=DRAIN;
        tcb->m_ssThresh=BbrInflight(tcb,BbrMaxBandwidth(),1.0);
    }
    if(DRAIN==m_mode&&BbrBytesInNetAtEdt(tcb->m_bytesInFlight)<=BbrInflight(tcb,BbrMaxBandwidth(),1.0)){
        ResetProbeBandwidthMode();  /* we estimate queue is drained */
    }
}
void TcpBbr::CheckProbeRttDone(Ptr<TcpSocketState> tcb){
    Time now=Simulator::Now();
    if(!(!m_probeRttDoneStamp.IsZero()&&now>m_probeRttDoneStamp)){
        return ;
    }
    m_minRttStamp=now; /* wait a while until PROBE_RTT */
    if(tcb->m_cWnd<m_priorCwnd){
        tcb->m_cWnd=m_priorCwnd;
    }
    ResetMode();
}
/* The goal of PROBE_RTT mode is to have BBR flows cooperatively and
 * periodically drain the bottleneck queue, to converge to measure the true
 * min_rtt (unloaded propagation delay). This allows the flows to keep queues
 * small (reducing queuing delay and packet loss) and achieve fairness among
 * BBR flows.
 *
 * The min_rtt filter window is 10 seconds. When the min_rtt estimate expires,
 * we enter PROBE_RTT mode and cap the cwnd at bbr_cwnd_min_target=4 packets.
 * After at least bbr_probe_rtt_mode_ms=200ms and at least one packet-timed
 * round trip elapsed with that flight size <= 4, we leave PROBE_RTT mode and
 * re-enter the previous mode. BBR uses 200ms to approximately bound the
 * performance penalty of PROBE_RTT's cwnd capping to roughly 2% (200ms/10s).
 *
 * Note that flows need only pay 2% if they are busy sending over the last 10
 * seconds. Interactive applications (e.g., Web, RPCs, video chunks) often have
 * natural silences or low-rate periods within 10 seconds where the rate is low
 * enough for long enough to drain its queue in the bottleneck. We pick up
 * these min RTT measurements opportunistically with our min_rtt filter. :-)
 */
void TcpBbr::UpdateMinRtt(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                        const TcpRateOps::TcpRateSample &rs){
    Time now=Simulator::Now();
    TcpRateOps::TcpRateConnection *rc_ptr=const_cast<TcpRateOps::TcpRateConnection*>(&rc);
    bool filter_expired=false;
    if(Time::Max()==rs.m_rtt){
        return ;
    }
    if(now>kMinRttExpiry+m_minRttStamp){
        filter_expired=true;
    }
    if(rs.m_rtt<m_minRtt||filter_expired){
        m_minRtt=rs.m_rtt;
        m_minRttStamp=now;
    }
    if(/*(!kProbeRttTime.IsZero())&&*/filter_expired&&!m_idleRestart&&m_mode!=PROBE_RTT){
        m_mode=PROBE_RTT;
        m_probeRttDoneStamp=Time(0);
        SaveCongestionWindow(tcb->m_cWnd);
    }
    if(PROBE_RTT==m_mode){
        rc_ptr->m_appLimited=std::max<uint32_t> (rc.m_delivered +tcb->m_bytesInFlight,1);
        uint32_t min_cwnd_target=kMinCWndSegment*tcb->m_segmentSize;
        if(m_probeRttDoneStamp.IsZero()&&tcb->m_bytesInFlight<=min_cwnd_target){
            m_probeRttDoneStamp=now+kProbeRttTime;
            m_probeRttRoundDone=0;
            m_nextRttDelivered=rc.m_delivered;
        }else if(!m_probeRttDoneStamp.IsZero()){
            if(m_roundStart){
                m_probeRttRoundDone=1;
            }
            if(m_probeRttRoundDone){
                CheckProbeRttDone(tcb);
            }
        }
    }
    if(rs.m_delivered){
        m_idleRestart=0;
    }
}
void TcpBbr::UpdateGains(){
    switch(m_mode){
        case STARTUP:{
            m_pacingGain=m_highGain;
            m_cWndGain=m_highGain;
            break;
        }
        case DRAIN:{
            m_pacingGain=1.0/m_highGain;
            m_cWndGain=m_highGain;
            break;
        }
        case PROBE_BW:{
            m_pacingGain=(m_ltUseBandwidth? 1.0:kPacingGain[m_cycleIndex]);
            m_cWndGain=kCWNDGainConstant;
            break;
        }
        case PROBE_RTT:{
            m_pacingGain=1.0;
            m_cWndGain=m_pacingGain;
            break;
        }
        default:{
            NS_ASSERT_MSG(0,"wrong mode");
            break;
        }
        
    }
}
void TcpBbr::UpdateModel(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs){
    UpdateBandwidth(tcb,rc,rs);
    UpdateAckAggregation(tcb,rc,rs);
    UpdateCyclePhase(tcb,rs);
    CheckFullBandwidthReached(rs);
    CheckDrain(tcb);
    UpdateMinRtt(tcb,rc,rs);
    UpdateGains();
}
void TcpBbr::SaveCongestionWindow(uint32_t congestion_window){
    if(m_prevCongState<TcpSocketState::CA_RECOVERY&&m_mode!=PROBE_RTT){
        m_priorCwnd=congestion_window;
    }else{
        m_priorCwnd=std::max(m_priorCwnd,congestion_window);
    }
}
uint64_t TcpBbr::BbrBdp(Ptr<TcpSocketState> tcb,DataRate bw,double gain){
    uint32_t mss=tcb->m_segmentSize;
    if(Time::Max()==m_minRtt||m_minRtt.IsZero()){
        return tcb->m_initialCWnd*mss;
    }
    double value=bw*m_minRtt*gain/(8.0*mss);
    uint64_t packet=value;
    uint64_t bdp=packet*mss;
    if(bdp<=kMinCWndSegment*mss){
    #if (TCP_BBR_DEGUG)
        //auto now=Simulator::Now().GetSeconds();
        //NS_LOG_FUNCTION(m_debug->GetUuid()<<now<<bdp<<bw<<m_minRtt.GetMilliSeconds()<<gain);
    #endif
        bdp=kMinCWndSegment*mss;
    }
    return bdp;
}
uint64_t TcpBbr::QuantizationBudget(Ptr<TcpSocketState> tcb,uint64_t cwnd){
    //cwnd += 3 * bbr_tso_segs_goal(sk);
    uint32_t mss=tcb->m_segmentSize;
    uint32_t w=cwnd/mss;
    //TODO
    /* Allow enough full-sized skbs in flight to utilize end systems. */
    //cwnd += 3 * bbr_tso_segs_goal(sk);
    
    /* Reduce delayed ACKs by rounding up cwnd to the next even number. */
    w=(w+1)&~1U;
    /* Ensure gain cycling gets inflight above BDP even for small BDPs. */
    if(PROBE_BW==m_mode&&0==m_cycleIndex){
        w+=2;
    }
    return w*mss;
}
uint64_t TcpBbr::BbrInflight(Ptr<TcpSocketState> tcb,DataRate bw,double gain){
    uint64_t inflight=BbrBdp(tcb,bw,gain);
    inflight=QuantizationBudget(tcb,inflight);
    return inflight;
}
//refer to bbr_packets_in_net_at_edt;
uint64_t TcpBbr::BbrBytesInNetAtEdt(uint64_t inflight_now){
    return inflight_now;
}
/* Find the cwnd increment based on estimate of ack aggregation */
uint64_t TcpBbr::AckAggregationCongestionWindow(){
    uint64_t max_aggr_cwnd, aggr_cwnd = 0;
    if(bbr_extra_acked_gain>0&&BbrFullBandwidthReached()){
        double bytes=BbrBandwidth()*bbr_extra_acked_max_time/8.0;
        max_aggr_cwnd=bytes;
        bytes=bbr_extra_acked_gain*BbrExtraAcked();
        aggr_cwnd=bytes;
        aggr_cwnd=std::min<uint64_t>(max_aggr_cwnd,aggr_cwnd);
    }
    return aggr_cwnd;
}
/* An optimization in BBR to reduce losses: On the first round of recovery, we
 * follow the packet conservation principle: send P packets per P packets acked.
 * After that, we slow-start and send at most 2*P packets per P packets acked.
 * After recovery finishes, or upon undo, we restore the cwnd we had when
 * recovery started (capped by the target cwnd based on estimated BDP).
 */
bool TcpBbr::SetCongestionWindowRecoveryOrRestore(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                                                    const TcpRateOps::TcpRateSample &rs,uint32_t *new_cwnd){
    uint8_t prev_state=m_prevCongState,state=tcb->m_congState;
    uint32_t congestion_window=tcb->m_cWnd;
    /* An ACK for P pkts should release at most 2*P packets. We do this
    * in two steps. First, here we deduct the number of lost packets.
    * Then, in bbr_set_cwnd() we slow start up toward the target cwnd.
    */
    uint32_t min_congestion_window=kMinCWndSegment*tcb->m_segmentSize;
    if(rs.m_bytesLoss>0){
        int64_t value=(int64_t)congestion_window-(int64_t)rs.m_bytesLoss;
        if(value<0){
            value=tcb->m_segmentSize;
            //NS_ASSERT_MSG(0,congestion_window<<" "<<value<<" "<<rs.m_bytesLoss);
        }
        congestion_window=value;
        if(congestion_window<min_congestion_window){
            congestion_window=min_congestion_window;
        }
    }
    
    if(TcpSocketState::CA_RECOVERY==state&&prev_state!=TcpSocketState::CA_RECOVERY){
        /* Starting 1st round of Recovery, so do packet conservation. */
        m_packetConservation=1;
        m_nextRttDelivered=rc.m_delivered; /* start round now */
        /* Cut unused cwnd from app behavior, TSQ, or TSO deferral: */
        congestion_window=tcb->m_bytesInFlight+rs.m_ackedSacked;
    }else if(prev_state>=TcpSocketState::CA_RECOVERY&&state<TcpSocketState::CA_RECOVERY){
        /* Exiting loss recovery; restore cwnd saved before recovery. */
        congestion_window=std::max<uint32_t>(congestion_window,m_priorCwnd);
        m_packetConservation=0;
    }
    m_prevCongState=state;
    if(1==m_packetConservation){
        *new_cwnd=std::max<uint32_t>(congestion_window,tcb->m_bytesInFlight+rs.m_ackedSacked);
        return true;
    }
    *new_cwnd=congestion_window;
    return false;
}
/* Slow-start up toward target cwnd (if bw estimate is growing, or packet loss
 * has drawn us down below target), or snap down to target if we're above it.
 */
void TcpBbr::SetCongestionWindow(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs,DataRate bw,double gain){
    uint32_t congestion_window=tcb->m_cWnd, target_cwnd=0;
    uint32_t mss=tcb->m_segmentSize;
    if(0==rs.m_ackedSacked){
        goto done;/* no packet fully ACKed; just apply caps */
    }
    if(SetCongestionWindowRecoveryOrRestore(tcb,rc,rs,&congestion_window)){
        goto done;
    }
    target_cwnd=BbrBdp(tcb,bw,gain);
    /* Increment the cwnd to account for excess ACKed data that seems
    * due to aggregation (of data and/or ACKs) visible in the ACK stream.
    */
    target_cwnd+=AckAggregationCongestionWindow();
    target_cwnd = QuantizationBudget(tcb, target_cwnd);
    
    /* If we're below target cwnd, slow start cwnd toward target cwnd. */
    if(BbrFullBandwidthReached()){
        congestion_window=std::min<uint32_t>(congestion_window+rs.m_ackedSacked,target_cwnd);
    }else if(congestion_window<target_cwnd||(rc.m_delivered<tcb->m_initialCWnd*mss)){
        congestion_window=congestion_window+rs.m_ackedSacked;
    }
    congestion_window=std::max<uint32_t>(congestion_window,kMinCWndSegment*tcb->m_segmentSize);
done:
    tcb->m_cWnd=congestion_window;
    if(m_mode==PROBE_RTT){
        /* drain queue, refresh min_rtt */
        uint32_t value=tcb->m_cWnd;
        tcb->m_cWnd=std::min<uint32_t>(value,kMinCWndSegment*tcb->m_segmentSize);
    }
}
bool TcpBbr::IsNextCyclePhase(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateSample &rs){
    bool is_full_length=false;
    if(m_deliveredTime>m_cycleStamp+m_minRtt){
        is_full_length=true;
    }
    if(1.0==m_pacingGain){
        return is_full_length;
    }
    uint64_t inflight=BbrBytesInNetAtEdt(rs.m_priorDelivered);
    DataRate bw=BbrMaxBandwidth();
    /* A pacing_gain > 1.0 probes for bw by trying to raise inflight to at
    * least pacing_gain*BDP; this may take more than min_rtt if min_rtt is
    * small (e.g. on a LAN). We do not persist if packets are lost, since
    * a path with small buffers may not hold that much.
    */
    if(m_pacingGain>1.0){
        return is_full_length&&(rs.m_bytesLoss||inflight>=BbrInflight(tcb,bw,m_pacingGain));
    }
    return is_full_length||inflight<=BbrInflight(tcb,bw,1.0);
}
void TcpBbr::AdvanceCyclePhase(){
    m_cycleIndex=(m_cycleIndex+1)%kGainCycleLength;
    m_cycleStamp=m_deliveredTime;
}
void TcpBbr::UpdateCyclePhase(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateSample &rs){
    if(m_mode!=PROBE_BW){
        return ;
    }
    if(IsNextCyclePhase(tcb,rs)){
        AdvanceCyclePhase();
    }
}
void TcpBbr::ResetMode(){
    if(!BbrFullBandwidthReached()){
        ResetStartUpMode();
    }else{
        ResetProbeBandwidthMode();
    }
}
void TcpBbr::ResetStartUpMode(){
    m_mode=STARTUP;
}
void TcpBbr::ResetProbeBandwidthMode(){
    m_mode=PROBE_BW;
    uint32_t bbr_rand=kGainCycleLength-1;
    uint32_t v=MockRandomU32Max(bbr_rand);
    NS_ASSERT(v<bbr_rand);
    m_cycleIndex=(kGainCycleLength-1-v)%kGainCycleLength;
    AdvanceCyclePhase();
}
uint32_t TcpBbr::MockRandomU32Max(uint32_t ep_ro){
    uint32_t seed=m_uv->GetInteger(0,std::numeric_limits<uint32_t>::max());
    uint64_t v=(((uint64_t) seed* ep_ro) >> 32);
    return v;
}
#if (TCP_BBR_DEGUG)
void TcpBbr::LogDebugInfo(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs){
    Time now=Simulator::Now();
    DataRate instant_bw=rs.m_deliveryRate;
    if(rs.m_delivered<=0||rs.m_interval.IsZero()||rs.m_priorTime.IsZero()){
        instant_bw=0;
    }
    DataRate long_bw=BbrBandwidth();
    uint32_t rtt_ms=rs.m_rtt.GetMilliSeconds();
    m_debug->GetStram()<<ModeToString(m_mode)<<" "<<now.GetSeconds()<<" "
    <<instant_bw.GetBitRate()<<" "<<long_bw.GetBitRate()<<" "
    <<rtt_ms<<" "<<rs.m_bytesLoss<<std::endl;
}
#endif
}
