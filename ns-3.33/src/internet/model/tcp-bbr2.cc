#include <unistd.h>
#include <time.h>
#include <limits>
#include <algorithm>
#include "ns3/log.h"
#include "tcp-tx-item.h"
#include "ns3/simulator.h"
#include "tcp-bbr2.h"
#include "tcp-bbr-debug.h"
//https://github.com/google/bbr/blob/v2alpha/net/ipv4/tcp_bbr2.c
namespace ns3{
NS_LOG_COMPONENT_DEFINE ("TcpBbr2");
NS_OBJECT_ENSURE_REGISTERED (TcpBbr2);
namespace{
inline uint64_t div_round_up_ull(uint64_t a,uint64_t b){
    uint64_t value=(a+b-1)/b;
    return value;
}
//https://blog.csdn.net/u012138730/article/details/79818162
inline unsigned int ilog2(unsigned int v){
    unsigned int r;
    unsigned int shift;
    r = (v > 0xffff) << 4; v >>= r;
    shift = (v > 0xff) << 3; v >>= shift; r |= shift;
    shift = (v > 0xf) << 2; v >>= shift; r |= shift;
    shift = (v > 0x3) << 1; v >>= shift; r |= shift;
    r |= (v >> 1);
    return r;
}
}
/* Parameters used to convert the timespec values: */
#define MSEC_PER_SEC	1000L
#define USEC_PER_MSEC	1000L
#define NSEC_PER_USEC	1000L
#define NSEC_PER_MSEC	1000000L
#define USEC_PER_SEC	1000000L
#define NSEC_PER_SEC	1000000000L
#define FSEC_PER_SEC	1000000000000000LL
/* Scale factor for rate in pkt/uSec unit to avoid truncation in bandwidth
 * estimation. The rate unit ~= (1500 bytes / 1 usec / 2^24) ~= 715 bps.
 * This handles bandwidths from 0.06pps (715bps) to 256Mpps (3Tbps) in a uint32_t.
 * Since the minimum window is >=4 packets, the lower bound isn't
 * an issue. The upper bound isn't an issue with existing technologies.
 */
#define BW_SCALE 24
#define BW_UNIT (1 << BW_SCALE)

#define BBR_SCALE 8	/* scaling factor for fractions in BBR (e.g. gains) */
#define BBR_UNIT (1 << BBR_SCALE)

#define FLAG_DEBUG_VERBOSE	0x1	/* Verbose debugging messages */
#define FLAG_DEBUG_LOOPBACK	0x2	/* Do NOT skip loopback addr */

#define CYCLE_LEN		8	/* number of phases in a pacing gain cycle */
/* BBR has the following modes for deciding how fast to send: */
enum bbr_mode {
    BBR_STARTUP,	/* ramp up sending rate rapidly to fill pipe */
    BBR_DRAIN,	/* drain any queue created during startup */
    BBR_PROBE_BW,	/* discover, share bw: pace around estimated bw */
    BBR_PROBE_RTT,	/* cut inflight to min to probe min_rtt */
};

/* How does the incoming ACK stream relate to our bandwidth probing? */
enum bbr_ack_phase {
    BBR_ACKS_INIT,		  /* not probing; not getting probe feedback */
    BBR_ACKS_REFILLING,	  /* sending at est. bw to fill pipe */
    BBR_ACKS_PROBE_STARTING,  /* inflight rising to probe bw */
    BBR_ACKS_PROBE_FEEDBACK,  /* getting feedback from bw probing */
    BBR_ACKS_PROBE_STOPPING,  /* stopped probing; still getting feedback */
};
/* Window length of min_rtt filter (in sec). Max allowed value is 31 (0x1F) */
static uint32_t bbr_min_rtt_win_sec = 10;
/* Minimum time (in ms) spent at bbr_cwnd_min_target in BBR_PROBE_RTT mode.
 * Max allowed value is 511 (0x1FF).
 */
static uint32_t bbr_probe_rtt_mode_ms = 200;
/* Window length of probe_rtt_min_us filter (in ms), and consequently the
 * typical interval between PROBE_RTT mode entries.
 * Note that bbr_probe_rtt_win_ms must be <= bbr_min_rtt_win_sec * MSEC_PER_SEC
 */
static uint32_t bbr_probe_rtt_win_ms = 5000;
/* Skip TSO below the following bandwidth (bits/sec): */
//static int bbr_min_tso_rate = 1200000;

/* Use min_rtt to help adapt TSO burst size, with smaller min_rtt resulting
 * in bigger TSO bursts. By default we cut the RTT-based allowance in half
 * for every 2^9 usec (aka 512 us) of RTT, so that the RTT-based allowance
 * is below 1500 bytes after 6 * ~500 usec = 3ms.
 */
static uint32_t bbr_tso_rtt_shift = 9;  /* halve allowance per 2^9 usecs, 512us */

/* Select cwnd TSO budget approach:
 *  0: padding
 *  1: flooring
 */
static uint32_t bbr_cwnd_tso_budget = 1;

/* Pace at ~1% below estimated bw, on average, to reduce queue at bottleneck.
 * In order to help drive the network toward lower queues and low latency while
 * maintaining high utilization, the average pacing rate aims to be slightly
 * lower than the estimated bandwidth. This is an important aspect of the
 * design.
 */
static const int bbr_pacing_margin_percent = 1;

/* We use a high_gain value of 2/ln(2) because it's the smallest pacing gain
 * that will allow a smoothly increasing pacing rate that will double each RTT
 * and send the same number of packets per RTT that an un-paced, slow-starting
 * Reno or CUBIC flow would. Max allowed value is 2047 (0x7FF).
 */
static int bbr_high_gain  = BBR_UNIT * 2885 / 1000 + 1;
/* The gain for deriving startup cwnd. Max allowed value is 2047 (0x7FF). */
static int bbr_startup_cwnd_gain  = BBR_UNIT * 2885 / 1000 + 1;
/* The pacing gain of 1/high_gain in BBR_DRAIN is calculated to typically drain
 * the queue created in BBR_STARTUP in a single round. Max allowed value
 * is 1023 (0x3FF).
 */
static int bbr_drain_gain = BBR_UNIT * 1000 / 2885;
/* The gain for deriving steady-state cwnd tolerates delayed/stretched ACKs.
 * Max allowed value is 2047 (0x7FF).
 */
static int bbr_cwnd_gain  = BBR_UNIT * 2;
/* The pacing_gain values for the PROBE_BW gain cycle, to discover/share bw.
 * Max allowed value for each element is 1023 (0x3FF).
 */
enum bbr_pacing_gain_phase {
    BBR_BW_PROBE_UP		= 0,  /* push up inflight to probe for bw/vol */
    BBR_BW_PROBE_DOWN	= 1,  /* drain excess inflight from the queue */
    BBR_BW_PROBE_CRUISE	= 2,  /* use pipe, w/ headroom in queue/pipe */
    BBR_BW_PROBE_REFILL	= 3,  /* v2: refill the pipe again to 100% */
};
static int bbr_pacing_gain[] = {
    BBR_UNIT * 5 / 4,	/* probe for more available bw */
    BBR_UNIT * 3 / 4,	/* drain queue and/or yield bw to other flows */
    BBR_UNIT, BBR_UNIT, BBR_UNIT,	/* cruise at 1.0*bw to utilize pipe, */
    BBR_UNIT, BBR_UNIT, BBR_UNIT	/* without creating excess queue... */
};

/* Try to keep at least this many packets in flight, if things go smoothly. For
 * smooth functioning, a sliding window protocol ACKing every other packet
 * needs at least 4 packets in flight. Max allowed value is 15 (0xF).
 */
static uint32_t bbr_cwnd_min_target = 4;

/* Cwnd to BDP proportion in PROBE_RTT mode scaled by BBR_UNIT. Default: 50%.
 * Use 0 to disable. Max allowed value is 255.
 */
static uint32_t bbr_probe_rtt_cwnd_gain = BBR_UNIT * 1 / 2;

/* To estimate if BBR_STARTUP mode (i.e. high_gain) has filled pipe... */
/* If bw has increased significantly (1.25x), there may be more bw available.
 * Max allowed value is 1023 (0x3FF).
 */
static uint32_t bbr_full_bw_thresh = BBR_UNIT * 5 / 4;
/* But after 3 rounds w/o significant bw growth, estimate pipe is full.
 * Max allowed value is 7 (0x7).
 */
static uint32_t bbr_full_bw_cnt = 3;

/* Experiment: each cycle, try to hold sub-unity gain until inflight <= BDP. */
static bool bbr_drain_to_target = true;		/* default: enabled */

/* Experiment: Flags to control BBR with ECN behavior.
 */
static bool bbr_precise_ece_ack = true;		/* default: enabled */

/* The max rwin scaling shift factor is 14 (RFC 1323), so the max sane rwin is
 * (2^(16+14) B)/(1024 B/packet) = 1M packets.
 */
//static uint32_t bbr_cwnd_warn_val	= 1U << 20;

/* BBR module parameters. These are module parameters only in Google prod.
 * Upstream these are intentionally not module parameters.
 */
static int bbr_pacing_gain_size = CYCLE_LEN;

/* Gain factor for adding extra_acked to target cwnd: */
static int bbr_extra_acked_gain = 256;

/* Window length of extra_acked window. Max allowed val is 31. */
static uint32_t bbr_extra_acked_win_rtts = 5;

/* Max allowed val for ack_epoch_acked, after which sampling epoch is reset */
static uint32_t bbr_ack_epoch_acked_reset_thresh = 1U << 20;

/* Time period for clamping cwnd increment due to ack aggregation */
static uint32_t bbr_extra_acked_max_us = 100 * 1000;

/* Use extra acked in startup ?
 * 0: disabled
 * 1: use latest extra_acked value from 1-2 rtt in startup
 */
static int bbr_extra_acked_in_startup = 1;		/* default: enabled */

/* Experiment: don't grow cwnd beyond twice of what we just probed. */
static bool bbr_usage_based_cwnd=false;		/* default: disabled */


/* Return rate in bytes per second, optionally with a gain.
 * The order here is chosen carefully to avoid overflow of uint64_t. This should
 * work for input rates of up to 2.9Tbit/sec and gain of 2.89x.
 */
FUNC_INLINE uint64_t bbr_rate_bytes_per_sec(uint64_t rate, int gain,int mss,int margin)
{
    rate *= mss;
    rate *= gain;
    rate >>= BBR_SCALE;
    rate *= USEC_PER_SEC/ 100 * (100 - margin);
    rate >>= BW_SCALE;
    rate = std::max<uint64_t>(rate, 1ULL);
    return rate;
}
FUNC_INLINE uint64_t bbr_bw_bytes_per_sec(uint64_t rate,int mss)
{
    return bbr_rate_bytes_per_sec(rate, BBR_UNIT,mss,0);
}
FUNC_INLINE uint64_t bbr_rate_kbps(uint64_t rate,int mss)
{
    rate = bbr_bw_bytes_per_sec(rate,mss);
    rate *= 8;
    rate=rate/1000;
    return rate;
}
/* Convert a BBR bw and gain factor to a pacing rate in bytes per second. */
FUNC_INLINE uint64_t bbr_bw_to_pacing_rate(uint64_t bw, int gain,int mss)
{
    uint64_t rate = bw;
    
    rate = bbr_rate_bytes_per_sec(rate, gain,mss,bbr_pacing_margin_percent);
    return rate;
}
FUNC_INLINE uint64_t bbr_kbps_to_bw(uint64_t kbps,int mss){
    // packets in 1 seconds;
    uint64_t packets=kbps*1000/(8*mss);
    uint64_t bw=packets*BW_UNIT/USEC_PER_SEC;
    return  bw;
}

TypeId TcpBbr2::GetTypeId (void){
    static TypeId tid = TypeId ("ns3::TcpBbr2")
    .SetParent<TcpCongestionOps> ()
    .AddConstructor<TcpBbr2> ()
    .SetGroupName ("Internet")
    .AddAttribute ("EnableEcn",
                   "True to support ECN",
                   BooleanValue (false),
                   MakeBooleanAccessor (&TcpBbr2::m_enableEcn),
                   MakeBooleanChecker ())
  ;
  return tid;
}
TcpBbr2::TcpBbr2():TcpCongestionOps(){
    m_uv=CreateObject<UniformRandomVariable> ();
    m_uv->SetStream(time(NULL));
#if (TCP_BBR2_DEGUG)
    m_debug=CreateObject<TcpBbrDebug>(GetName());
#endif
}
TcpBbr2::TcpBbr2(const TcpBbr2 &sock):
m_uv (sock.m_uv){
#if (TCP_BBR2_DEGUG)
    m_debug=sock.m_debug;
#endif
}
TcpBbr2::~TcpBbr2(){}
std::string TcpBbr2::PhaseToString(uint8_t mode,uint8_t cycle_idx){
    switch(mode){
        case BBR_STARTUP:{
            return "startup";
        }
        case BBR_DRAIN:{
            return "drain";
        }
        case BBR_PROBE_BW:{
            break;
        }
        case BBR_PROBE_RTT:{
            return "probe_rtt";
        }
        default:{
            return "???";
        }
    }
    switch(cycle_idx){
        case BBR_BW_PROBE_UP:{
            return "probe_bw_up";
        }
        case BBR_BW_PROBE_DOWN:{
            return "probe_bw_down";
        }
        case BBR_BW_PROBE_CRUISE:{
            return "probe_bw_cruise";
        }
        case BBR_BW_PROBE_REFILL:{
            return "probe_bw_refill";
        }
        default:{
            return "probe_???";
        }
    }
}
std::string TcpBbr2::GetName () const{
    return "TcpBbr2";
}
void TcpBbr2::Init (Ptr<TcpSocketState> tcb){
    NS_ASSERT_MSG(tcb->m_pacing,"Enable pacing for BBRv2");
    bbr2_init(tcb);
}
uint32_t TcpBbr2::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight){
    struct bbr_t *bbr=&m_bbr;
    
    uint32_t mss=tcb->m_segmentSize;
    uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    bbr_save_cwnd(snd_cwnd);
    /* For undo, save state that adapts based on loss signal. */
    bbr->undo_bw_lo		= bbr->bw_lo;
    bbr->undo_inflight_lo	= bbr->inflight_lo;
    bbr->undo_inflight_hi	= bbr->inflight_hi;
    return tcb->m_ssThresh;
}
void  TcpBbr2::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked){}
void TcpBbr2::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt){
    if (tcb->m_ecnState == TcpSocketState::ECN_ECE_RCVD){
        m_eceFlag=true;
    }
}
void TcpBbr2::CongestionStateSet (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCongState_t newState){
    struct bbr_t *bbr=&m_bbr;
    if(TcpSocketState::CA_LOSS==newState){
        uint32_t mss=tcb->m_segmentSize;
        uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
        bbr->prev_ca_state =TcpSocketState::CA_LOSS;
        bbr->full_bw = 0;
        if (!bbr2_is_probing_bandwidth() && bbr->inflight_lo == ~0U) {
            /* bbr_adapt_lower_bounds() needs cwnd before
            * we suffered an RTO, to update inflight_lo:
            */
            bbr->inflight_lo =std::max<uint32_t>(snd_cwnd, bbr->prior_cwnd);
        }
    }else if (bbr->prev_ca_state ==TcpSocketState::CA_LOSS&&
                newState!=TcpSocketState::CA_LOSS) {
        uint32_t mss=tcb->m_segmentSize;
        uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
        snd_cwnd=std::max<uint32_t>(snd_cwnd,bbr->prior_cwnd);
        tcb->m_cWnd=snd_cwnd*mss;
        bbr->try_fast_path = 0; /* bound cwnd using latest model */
    }
}
void TcpBbr2::CwndEvent (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCAEvent_t event){
    struct bbr_t *bbr=&m_bbr;
    //TODO   bbr_cwnd_event
    if((TcpSocketState::CA_EVENT_ECN_IS_CE==event||TcpSocketState::CA_EVENT_ECN_NO_CE==event)&&
        m_enableEcn&&bbr->params.precise_ece_ack){
        uint32_t state = bbr->ce_state;
        //dctcp_ece_ack_update
        bbr->ce_state = state;
    }
}
bool TcpBbr2::HasCongControl () const{
    return true;
}
void TcpBbr2::CongControl (Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs){
    struct bbr_t *bbr=&m_bbr;
    struct bbr_context ctx = { 0 };
    bool update_model = true;
    uint32_t bw;
    uint32_t mss=tcb->m_segmentSize;
    uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    bbr->debug.event = '.';  /* init to default NOP (no event yet) */
    
    if(bbr->ack_epoch_mstamp.IsZero()){
        bbr->ack_epoch_mstamp=Simulator::Now();
    }
    
    bbr_update_round_start(tcb,rc,rs);
    if (bbr->round_start) {
        bbr->rounds_since_probe =std::min<int>(bbr->rounds_since_probe + 1, 0xFF);
        bbr2_update_ecn_alpha(tcb,rc);
    }
    bbr->ecn_in_round|=m_eceFlag;
    bbr_calculate_bw_sample(tcb,rs,&ctx);
    if (bbr2_fast_path(tcb,rc,rs,&update_model,&ctx))
        goto out;

    if (update_model)
        bbr2_update_model(tcb,rc,rs,&ctx);
    
    bbr_update_gains();
    bw = bbr_bw();
    bbr_set_pacing_rate(tcb,bw,bbr->pacing_gain);
    bbr_set_cwnd(tcb,rc,rs,bw,bbr->cwnd_gain,snd_cwnd,&ctx);
    bbr2_bound_cwnd_for_inflight_model(tcb);
out:
    bbr->prev_ca_state=tcb->m_congState;
    bbr->loss_in_cycle |= rs.m_bytesLostRound> 0;
    bbr->ecn_in_cycle  |= rs.m_ecnBytesRound> 0;
    m_eceFlag=false;
#if (TCP_BBR2_DEGUG)
    uint32_t rtt_ms=rs.m_rtt.GetMilliSeconds();
    bw=ctx.sample_bw;
    uint32_t bw_max=bbr_max_bw();
    uint64_t bps1=bbr_bw_bytes_per_sec(bw,mss)*8;
    uint32_t bps2=bbr_bw_bytes_per_sec(bw_max,mss)*8;
    m_debug->GetStram()<<PhaseToString(bbr->mode,bbr->cycle_idx)<<" "<<Simulator::Now().GetSeconds()
    <<" "<<bps1<<" "<<bps2<<" "
    <<rtt_ms<<" "<<rs.m_bytesLoss<<std::endl;
    
#endif
}
void TcpBbr2::MarkSkbLost(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                         const TcpTxItem *skb){
    struct bbr_t *bbr=&m_bbr;
    TcpRateOps::TcpRateSample rs;

    /* Capture "current" data over the full round trip of loss,
    * to have a better chance to see the full capacity of the path.
    */
    if (!bbr->loss_in_round)  /* first loss in this round trip? */
        bbr->loss_round_delivered = rc.m_delivered;  /* set round trip */
    bbr->loss_in_round = 1;
    bbr->loss_in_cycle = 1;
    if (!bbr->bw_probe_samples)
        return;  /* not an skb sent while probing for bandwidth */
    
    /* We are probing for bandwidth. Construct a rate sample that
    * estimates what happened in the flight leading up to this lost skb,
    * then see if the loss rate went too high, and if so at which packet.
    */
    TcpTxItem *skb_ptr=const_cast<TcpTxItem*>(skb);
    TcpTxItem::RateInformation & skbInfo = skb_ptr->GetRateInformation ();
    rs.m_txInFlight=skbInfo.m_bytesInFlight;
    rs.m_bytesLostRound=rc.m_bytesLostCount-skbInfo.m_bytesLostCount;
    rs.m_isAppLimited=skbInfo.m_isAppLimited;
    if(bbr2_is_inflight_too_high(rs)){
        uint32_t mss=tcb->m_segmentSize;
        uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
        uint32_t tx_in_flight = bbr2_inflight_hi_from_lost_skb(rs,skb->GetSeqSize(),mss);
        bbr2_handle_inflight_too_high(rs.m_isAppLimited,snd_cwnd,tx_in_flight,rc.m_delivered);
    }
}
Ptr<TcpCongestionOps> TcpBbr2::Fork (){
    return CopyObject<TcpBbr2> (this);
}
void TcpBbr2::AssignStreams (int64_t stream){
    if(m_uv){
        m_uv->SetStream(stream);
    }
}

/* Do we estimate that STARTUP filled the pipe? */
bool TcpBbr2::bbr_full_bw_reached(){
    struct bbr_t *bbr=&m_bbr;
    return  bbr->full_bw_reached;
}
/* Return the windowed max recent bandwidth sample, in pkts/uS << BW_SCALE. */
uint32_t TcpBbr2::bbr_max_bw(){
    struct bbr_t *bbr=&m_bbr;
    return std::max<uint32_t>(bbr->bw_hi[0], bbr->bw_hi[1]);
}
/* Return the estimated bandwidth of the path, in pkts/uS << BW_SCALE. */
uint32_t TcpBbr2::bbr_bw(){
    struct bbr_t *bbr=&m_bbr;
    return std::min<uint32_t>(bbr_max_bw(), bbr->bw_lo);
}
/* Return maximum extra acked in past k-2k round trips,
 * where k = bbr_extra_acked_win_rtts.
 */
uint16_t TcpBbr2::bbr_extra_acked(){
    struct bbr_t *bbr=&m_bbr;
    return std::max<uint16_t>(bbr->extra_acked[0], bbr->extra_acked[1]);
}
void TcpBbr2::bbr_init_pacing_rate_from_rtt(Ptr<TcpSocketState> tcb){
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    uint64_t bw=bbr_kbps_to_bw(1000,mss);
    DataRate pacing_rate(0);
    Time rtt=tcb->m_lastRtt;
    if(rtt.IsZero()){
        uint64_t bytes_per_sec=bbr_bw_to_pacing_rate(bw,bbr->params.high_gain,mss);
        pacing_rate=DataRate(8*bytes_per_sec);
    }else{
        bbr->has_seen_rtt = 1;
        bw=snd_cwnd*BW_UNIT/rtt.GetMicroSeconds();
        uint64_t bytes_per_sec=bbr_bw_to_pacing_rate(bw,bbr->params.high_gain,mss);
        pacing_rate=DataRate(8*bytes_per_sec);
    }
    #if (TCP_BBR2_DEGUG)
        NS_LOG_FUNCTION(m_debug->GetUuid()<<rtt.GetMilliSeconds()<<pacing_rate<<bbr->params.high_gain);
    #endif
    if(pacing_rate>tcb->m_maxPacingRate){
        pacing_rate=tcb->m_maxPacingRate;
    }
    tcb->m_pacingRate=pacing_rate;
}
void TcpBbr2::bbr_set_pacing_rate(Ptr<TcpSocketState> tcb, uint32_t bw, int gain){
    struct bbr_t *bbr=&m_bbr;

    uint32_t mss=tcb->m_segmentSize;
    uint64_t bytes_per_sec=bbr_bw_to_pacing_rate(bw,gain,mss);
    DataRate pacing_rate(8*bytes_per_sec);
    Time last_rtt=tcb->m_lastRtt;
    if(!bbr->has_seen_rtt&&(!last_rtt.IsZero())){
        bbr_init_pacing_rate_from_rtt(tcb);
    }
    if(pacing_rate>tcb->m_maxPacingRate){
        pacing_rate=tcb->m_maxPacingRate;
    }
    if(bbr_full_bw_reached()||pacing_rate>tcb->m_pacingRate){
        tcb->m_pacingRate=pacing_rate;
    }
}
/* Save "last known good" cwnd so we can restore it after losses or PROBE_RTT */
void TcpBbr2::bbr_save_cwnd(uint32_t send_cwnd){
    struct bbr_t *bbr=&m_bbr;
    if (bbr->prev_ca_state < TcpSocketState::CA_RECOVERY&& bbr->mode != BBR_PROBE_RTT)
        bbr->prior_cwnd =send_cwnd;  /* this cwnd is good enough */
    else  /* loss recovery or BBR_PROBE_RTT have temporarily cut cwnd */
        bbr->prior_cwnd = std::max<uint32_t>(bbr->prior_cwnd,send_cwnd);
}
/* Calculate bdp based on min RTT and the estimated bottleneck bandwidth:
 *
 * bdp = ceil(bw * min_rtt * gain)
 *
 * The key factor, gain, controls the amount of queue. While a small gain
 * builds a smaller queue, it becomes more vulnerable to noise in RTT
 * measurements (e.g., delayed ACKs or other ACK compression effects). This
 * noise may cause BBR to under-estimate the rate.
 */
uint32_t TcpBbr2::bbr_bdp(uint32_t bw, int gain){
    uint32_t bdp;
    uint64_t w;
    struct bbr_t *bbr=&m_bbr;
    /* If we've never had a valid RTT sample, cap cwnd at the initial
    * default. This should only happen when the connection is not using TCP
    * timestamps and has retransmitted all of the SYN/SYNACK/data packets
    * ACKed so far. In this case, an RTO can cut cwnd to 1, in which
    * case we need to slow-start up toward something safe: initial cwnd.
    */
    if(0==bbr->min_rtt_us||(~0U==bbr->min_rtt_us)){
        return bbr->init_cwnd;
    }
    w=(uint64_t)bw*bbr->min_rtt_us;
    /* Apply a gain to the given value, remove the BW_SCALE shift, and
    * round the value up to avoid a negative feedback loop.
    */
    bdp = (((w * gain) >> BBR_SCALE) + BW_UNIT - 1) / BW_UNIT;
    
    return bdp;
}
/* To achieve full performance in high-speed paths, we budget enough cwnd to
 * fit full-sized skbs in-flight on both end hosts to fully utilize the path:
 *   - one skb in sending host Qdisc,
 *   - one skb in sending host TSO/GSO engine
 *   - one skb being received by receiver host LRO/GRO/delayed-ACK engine
 * Don't worry, at low rates (bbr_min_tso_rate) this won't bloat cwnd because
 * in such cases tso_segs_goal is 1. The minimum cwnd is 4 packets,
 * which allows 2 outstanding 2-packet sequences, to try to keep pipe
 * full even with ACK-every-other-packet delayed ACKs.
 */
uint32_t TcpBbr2::bbr_quantization_budget(uint32_t cwnd){
    struct bbr_t *bbr=&m_bbr;
    /* Allow enough full-sized skbs in flight to utilize end systems. */
    //cwnd += 3 * bbr_tso_segs_goal(sk);
    
    /* Reduce delayed ACKs by rounding up cwnd to the next even number. */
    cwnd = (cwnd + 1) & ~1U;
    
    /* Ensure gain cycling gets inflight above BDP even for small BDPs. */
    if (bbr->mode == BBR_PROBE_BW && bbr->cycle_idx == 0)
        cwnd += 2;
    
    return cwnd;
}
/* Find inflight based on min RTT and the estimated bottleneck bandwidth. */
uint32_t TcpBbr2::bbr_inflight(uint32_t bw, int gain)
{
    uint32_t inflight;
    
    inflight = bbr_bdp(bw, gain);
    inflight = bbr_quantization_budget(inflight);
    
    return inflight;
}
/* With pacing at lower layers, there's often less data "in the network" than
 * "in flight". With TSQ and departure time pacing at lower layers (e.g. fq),
 * we often have several skbs queued in the pacing layer with a pre-scheduled
 * earliest departure time (EDT). BBR adapts its pacing rate based on the
 * inflight level that it estimates has already been "baked in" by previous
 * departure time decisions. We calculate a rough estimate of the number of our
 * packets that might be in the network at the earliest departure time for the
 * next skb scheduled:
 *   in_network_at_edt = inflight_at_edt - (EDT - now) * bw
 * If we're increasing inflight, then we want to know if the transmit of the
 * EDT skb will push inflight above the target, so inflight_at_edt includes
 * bbr_tso_segs_goal() from the skb departing at EDT. If decreasing inflight,
 * then estimate if inflight will sink too low just before the EDT transmit.
 */
uint32_t TcpBbr2::bbr_packets_in_net_at_edt(uint32_t inflight_now){
    return inflight_now;
}
uint32_t TcpBbr2::bbr_ack_aggregation_cwnd(){
    struct bbr_t *bbr=&m_bbr;
    uint32_t max_aggr_cwnd, aggr_cwnd = 0;
    
    if (bbr->params.extra_acked_gain &&
        (bbr_full_bw_reached() || bbr->params.extra_acked_in_startup)) {
        max_aggr_cwnd = ((uint64_t)bbr_bw() * bbr_extra_acked_max_us)
                / BW_UNIT;
        aggr_cwnd = (bbr->params.extra_acked_gain * bbr_extra_acked())
                >> BBR_SCALE;
        aggr_cwnd = std::min<uint32_t>(aggr_cwnd, max_aggr_cwnd);
    }
    
    return aggr_cwnd;
}
/* Returns the cwnd for PROBE_RTT mode. */
uint32_t TcpBbr2::bbr_probe_rtt_cwnd()
{
    struct bbr_t *bbr=&m_bbr;
    
    if (bbr->params.probe_rtt_cwnd_gain == 0)
        return bbr->params.cwnd_min_target;
    return std::max<uint32_t>(bbr->params.cwnd_min_target,
            bbr_bdp(bbr_bw(), bbr->params.probe_rtt_cwnd_gain));
}
/* Slow-start up toward target cwnd (if bw estimate is growing, or packet loss
 * has drawn us down below target), or snap down to target if we're above it.
 */
void TcpBbr2::bbr_set_cwnd(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                const TcpRateOps::TcpRateSample &rs,uint32_t bw, int gain, 
                uint32_t cwnd,struct bbr_context *ctx){
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    uint32_t target_cwnd = 0, prev_cwnd, max_probe;
    uint32_t acked=0;
    uint32_t send_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    prev_cwnd=send_cwnd;
    uint32_t new_cwnd=0;
    if(rs.m_ackedSacked>0){
        acked=div_round_up_ull(rs.m_ackedSacked,mss);
    }

    if (!acked)
        goto done;  /* no packet fully ACKed; just apply caps */
    
    target_cwnd = bbr_bdp(bw, gain);
    
    /* Increment the cwnd to account for excess ACKed data that seems
    * due to aggregation (of data and/or ACKs) visible in the ACK stream.
    */
    target_cwnd += bbr_ack_aggregation_cwnd();
    target_cwnd = bbr_quantization_budget(target_cwnd);
    
    /* If we're below target cwnd, slow start cwnd toward target cwnd. */
    bbr->debug.target_cwnd = target_cwnd;
    
    /* Update cwnd and enable fast path if cwnd reaches target_cwnd. */
    bbr->try_fast_path = 0;
    if (bbr_full_bw_reached()) { /* only cut cwnd if we filled the pipe */
        cwnd += acked;
        if (cwnd >= target_cwnd) {
            cwnd = target_cwnd;
            bbr->try_fast_path = 1;
        }
    } else if (cwnd < target_cwnd || cwnd  < 2 * bbr->init_cwnd) {
        cwnd += acked;
    } else {
        bbr->try_fast_path = 1;
    }

    /* When growing cwnd, don't grow beyond twice what we just probed. */
    if (bbr->params.usage_based_cwnd) {
        //tp->max_packets_out
        uint32_t max_packets_out=div_round_up_ull(tcb->m_bytesInFlight,mss);
        max_probe = std::max<uint32_t>(2 *max_packets_out,send_cwnd);
        cwnd = std::min<uint32_t>(cwnd, max_probe);
    }

    cwnd =std::max<uint32_t>(cwnd, bbr->params.cwnd_min_target);
done:
    new_cwnd=cwnd;
    if (bbr->mode == BBR_PROBE_RTT)  /* drain queue, refresh min_rtt */{
        new_cwnd= std::min<uint32_t>(new_cwnd,bbr_probe_rtt_cwnd());
        if(new_cwnd<bbr->params.cwnd_min_target){
            new_cwnd=bbr->params.cwnd_min_target;
        }
    }
    tcb->m_cWnd=new_cwnd*mss;
    ctx->target_cwnd = target_cwnd;
    ctx->log = (new_cwnd!= prev_cwnd);
}
void TcpBbr2::bbr_update_round_start(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,const TcpRateOps::TcpRateSample &rs){
    struct bbr_t *bbr=&m_bbr;
    bbr->round_start = 0;
    if(rs.m_delivered<=0||rs.m_interval.IsZero()||rs.m_priorTime.IsZero()){
        return ;
    }
    if(rs.m_priorDelivered>=bbr->next_rtt_delivered){
        bbr->next_rtt_delivered=rc.m_delivered;
        bbr->round_start=1;
    }
}
void TcpBbr2::bbr_calculate_bw_sample(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateSample &rs,struct bbr_context *ctx){
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    uint64_t bw = 0;
    bool valid=true;
    if(rs.m_delivered<=0||rs.m_interval.IsZero()||rs.m_priorTime.IsZero()){
        valid=false;
    }
    if(valid){
        uint32_t packets=div_round_up_ull(rs.m_delivered,mss);
        uint32_t us=rs.m_interval.GetMicroSeconds();
        bw=div_round_up_ull((uint64_t)packets*BW_UNIT,us);
    }
    ctx->sample_bw = bw;
    bbr->debug.rs_bw = bw;
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
 * trips for non-startup phase, and 1-2 round trips for startup.
 */
void TcpBbr2::bbr_update_ack_aggregation(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                const TcpRateOps::TcpRateSample &rs){
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    uint32_t epoch_us=0, expected_acked, extra_acked;
    uint32_t acked_sacked=0;
    uint32_t send_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    if(rs.m_ackedSacked>0){
        acked_sacked=div_round_up_ull(rs.m_ackedSacked,mss);
    }
    uint32_t extra_acked_win_rtts_thresh = bbr->params.extra_acked_win_rtts;
    if (!bbr->params.extra_acked_gain || rs.m_ackedSacked<= 0 ||
        rs.m_delivered <=0||rs.m_interval.IsZero()||rs.m_priorTime.IsZero())
        return;
    
    if (bbr->round_start) {
        bbr->extra_acked_win_rtts = std::min<uint32_t>(0x1F,bbr->extra_acked_win_rtts + 1);
        if (bbr->params.extra_acked_in_startup &&!bbr_full_bw_reached()){
            extra_acked_win_rtts_thresh = 1;
        }
        if (bbr->extra_acked_win_rtts >=extra_acked_win_rtts_thresh){
            bbr->extra_acked_win_rtts = 0;
            bbr->extra_acked_win_idx = bbr->extra_acked_win_idx ?0 : 1;
            bbr->extra_acked[bbr->extra_acked_win_idx] = 0;
        }
    }
    
    /* Compute how many packets we expected to be delivered over epoch. */
    if(rc.m_deliveredTime>bbr->ack_epoch_mstamp){
        epoch_us=(rc.m_deliveredTime-bbr->ack_epoch_mstamp).GetMicroSeconds();
    }
    expected_acked = ((uint64_t)bbr_bw() * epoch_us) / BW_UNIT;
    
    /* Reset the aggregation epoch if ACK rate is below expected rate or
    * significantly large no. of ack received since epoch (potentially
    * quite old epoch).
    */
    if (bbr->ack_epoch_acked <= expected_acked ||
        (bbr->ack_epoch_acked +acked_sacked>=
        bbr_ack_epoch_acked_reset_thresh)) {
        bbr->ack_epoch_acked = 0;
        bbr->ack_epoch_mstamp =rc.m_deliveredTime;
        expected_acked = 0;
    }
    
    /* Compute excess data delivered, beyond what was expected. */
    bbr->ack_epoch_acked =std::min<uint32_t>(0xFFFFF,bbr->ack_epoch_acked +acked_sacked);
    extra_acked = bbr->ack_epoch_acked - expected_acked;
    extra_acked=std::min<uint32_t>(extra_acked,send_cwnd);
    if (extra_acked > bbr->extra_acked[bbr->extra_acked_win_idx])
        bbr->extra_acked[bbr->extra_acked_win_idx] = extra_acked;
}
/* Estimate when the pipe is full, using the change in delivery rate: BBR
 * estimates that STARTUP filled the pipe if the estimated bw hasn't changed by
 * at least bbr_full_bw_thresh (25%) after bbr_full_bw_cnt (3) non-app-limited
 * rounds. Why 3 rounds: 1: rwin autotuning grows the rwin, 2: we fill the
 * higher rwin, 3: we get higher delivery rate samples. Or transient
 * cross-traffic or radio noise can go away. CUBIC Hystart shares a similar
 * design goal, but uses delay and inter-ACK spacing instead of bandwidth.
 */
void TcpBbr2::bbr_check_full_bw_reached(const TcpRateOps::TcpRateSample &rs){
    struct bbr_t *bbr=&m_bbr;
    uint32_t bw_thresh;
    if(bbr_full_bw_reached()||!bbr->round_start||rs.m_isAppLimited){
        return ;
    }
    bw_thresh = (uint64_t)bbr->full_bw * bbr->params.full_bw_thresh >> BBR_SCALE;
    if (bbr_max_bw() >= bw_thresh) {
        bbr->full_bw = bbr_max_bw();
        bbr->full_bw_cnt = 0;
        return;
    }
    ++bbr->full_bw_cnt;
    bbr->full_bw_reached = bbr->full_bw_cnt >= bbr->params.full_bw_cnt;
}
bool TcpBbr2::bbr_check_drain(Ptr<TcpSocketState> tcb){
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    uint32_t inflight_packets=div_round_up_ull(tcb->m_bytesInFlight,mss);
    if (bbr->mode == BBR_STARTUP && bbr_full_bw_reached()) {
        bbr->mode = BBR_DRAIN;  /* drain queue we created */
        uint32_t snd_ssthresh=bbr_inflight(bbr_max_bw(), BBR_UNIT);
        tcb->m_ssThresh=snd_ssthresh*mss;
        bbr2_reset_congestion_signals();
    }	/* fall through to check if in-flight is already small: */
    if (bbr->mode == BBR_DRAIN &&
        bbr_packets_in_net_at_edt(inflight_packets) <=
        bbr_inflight(bbr_max_bw(), BBR_UNIT))
        return true;  /* exiting DRAIN now */
    return false;
}
void TcpBbr2::bbr_check_probe_rtt_done(Ptr<TcpSocketState> tcb,uint64_t delivered_bytes){
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    Time now=Simulator::Now();
    
    if(!(!bbr->probe_rtt_done_stamp.IsZero()&&now>bbr->probe_rtt_done_stamp)){
        return ;
    }
    bbr->probe_rtt_min_stamp =now;
    uint32_t prior_cwnd_bytes=bbr->prior_cwnd*mss;
    if(tcb->m_cWnd<prior_cwnd_bytes){
        tcb->m_cWnd=prior_cwnd_bytes;
    }
    bbr2_exit_probe_rtt(delivered_bytes);
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

void TcpBbr2::bbr_update_min_rtt(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                        const TcpRateOps::TcpRateSample &rs){
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    Time now=Simulator::Now();
    TcpRateOps::TcpRateConnection *rc_ptr=const_cast<TcpRateOps::TcpRateConnection*>(&rc);
    uint32_t inflight_packets=div_round_up_ull(tcb->m_bytesInFlight,mss);
    bool probe_rtt_expired=false, min_rtt_expired=false;
    Time expired=bbr->probe_rtt_min_stamp+MilliSeconds(bbr->params.probe_rtt_win_ms);
    if(now>expired){
        probe_rtt_expired=true;
    }
    uint32_t rtt_us=rs.m_rtt.GetMicroSeconds();
    if(rtt_us>=0&&(rtt_us<=bbr->probe_rtt_min_us||probe_rtt_expired)){
        bbr->probe_rtt_min_us=rtt_us;
        bbr->probe_rtt_min_stamp=now;
    }
    /* Track min RTT seen in the min_rtt_win_sec filter window: */
    expired= bbr->min_rtt_stamp+Seconds(bbr->params.min_rtt_win_sec);
    if(now>expired){
        min_rtt_expired=true;
    }
    if(bbr->probe_rtt_min_us <= bbr->min_rtt_us||min_rtt_expired){
        bbr->min_rtt_us = bbr->probe_rtt_min_us;
        bbr->min_rtt_stamp = bbr->probe_rtt_min_stamp;
    }
    
    if (bbr->params.probe_rtt_mode_ms > 0 && probe_rtt_expired &&
        !bbr->idle_restart && bbr->mode != BBR_PROBE_RTT) {
        bbr->mode = BBR_PROBE_RTT;  /* dip, drain queue */
        uint32_t cwnd=div_round_up_ull(tcb->m_cWnd,mss);
        bbr_save_cwnd(cwnd);  /* note cwnd so we can restore it */
        bbr->probe_rtt_done_stamp =Time(0);
        bbr->ack_phase = BBR_ACKS_PROBE_STOPPING;
        bbr->next_rtt_delivered = rc.m_delivered;
    }

    if (bbr->mode == BBR_PROBE_RTT) {
        /* Ignore low rate samples during this mode. */
        rc_ptr->m_appLimited=std::max<uint32_t> (rc.m_delivered +tcb->m_bytesInFlight,1);
        /* Maintain min packets in flight for max(200 ms, 1 round). */
        if (bbr->probe_rtt_done_stamp.IsZero()&&inflight_packets<= bbr_probe_rtt_cwnd()) {
            bbr->probe_rtt_done_stamp = now +MilliSeconds(bbr->params.probe_rtt_mode_ms);
            bbr->probe_rtt_round_done = 0;
            bbr->next_rtt_delivered =rc.m_delivered;
        } else if (!bbr->probe_rtt_done_stamp.IsZero()) {
            if (bbr->round_start)
                bbr->probe_rtt_round_done = 1;
            if (bbr->probe_rtt_round_done)
                bbr_check_probe_rtt_done(tcb,rc.m_delivered);
        }
    }
    /* Restart after idle ends only once we process a new S/ACK for data */
    if (rs.m_delivered > 0)
        bbr->idle_restart = 0;
}

void TcpBbr2::bbr_update_gains(){
    struct bbr_t *bbr=&m_bbr;

    switch (bbr->mode) {
    case BBR_STARTUP:
        bbr->pacing_gain = bbr->params.high_gain;
        bbr->cwnd_gain	 = bbr->params.startup_cwnd_gain;
        break;
    case BBR_DRAIN:
        bbr->pacing_gain = bbr->params.drain_gain;  /* slow, to drain */
        bbr->cwnd_gain = bbr->params.startup_cwnd_gain;  /* keep cwnd */
        break;
    case BBR_PROBE_BW:
        bbr->pacing_gain = bbr->params.pacing_gain[bbr->cycle_idx];
        bbr->cwnd_gain = bbr->params.cwnd_gain;
        break;
    case BBR_PROBE_RTT:
        bbr->pacing_gain = BBR_UNIT;
        bbr->cwnd_gain = BBR_UNIT;
        break;
    default:
        NS_ASSERT_MSG(0,"wrong mode");
        break;
    }
}
void TcpBbr2::bbr_init(Ptr<TcpSocketState> tcb){
    Time now=Simulator::Now();
    struct bbr_t *bbr=&m_bbr;
    bbr->initialized = 1;
    bbr->params.high_gain = std::min<uint32_t>(0x7FF, bbr_high_gain);
    bbr->params.drain_gain =std::min<uint32_t>(0x3FF, bbr_drain_gain);
    bbr->params.startup_cwnd_gain =std::min<uint32_t>(0x7FF, bbr_startup_cwnd_gain);
    bbr->params.cwnd_gain =std::min<uint32_t>(0x7FF, bbr_cwnd_gain);
    bbr->params.cwnd_tso_budget =std::min<uint32_t>(0x1U, bbr_cwnd_tso_budget);
    bbr->params.cwnd_min_target = std::min<uint32_t>(0xFU, bbr_cwnd_min_target);
    bbr->params.min_rtt_win_sec =std::min<uint32_t>(0x1FU, bbr_min_rtt_win_sec);
    bbr->params.probe_rtt_mode_ms = std::min<uint32_t>(0x1FFU, bbr_probe_rtt_mode_ms);
    bbr->params.full_bw_cnt =std::min<uint32_t>(0x7U, bbr_full_bw_cnt);
    bbr->params.full_bw_thresh =std::min<uint32_t>(0x3FFU, bbr_full_bw_thresh);
    bbr->params.extra_acked_gain =std::min<uint32_t>(0x7FF, bbr_extra_acked_gain);
    bbr->params.extra_acked_win_rtts =std::min<uint32_t>(0x1FU, bbr_extra_acked_win_rtts);
    bbr->params.drain_to_target = bbr_drain_to_target ? 1 : 0;
    bbr->params.precise_ece_ack = bbr_precise_ece_ack ? 1 : 0;
    bbr->params.extra_acked_in_startup = bbr_extra_acked_in_startup ? 1 : 0;
    bbr->params.probe_rtt_cwnd_gain =std::min<uint32_t>(0xFFU, bbr_probe_rtt_cwnd_gain);
    bbr->params.probe_rtt_win_ms =std::min<uint32_t>(0x3FFFU,
            std::min<uint32_t>(bbr_probe_rtt_win_ms,bbr->params.min_rtt_win_sec * MSEC_PER_SEC));
    for (int i = 0; i <bbr_pacing_gain_size; i++)
        bbr->params.pacing_gain[i] =std::min<uint32_t>(0x3FF, bbr_pacing_gain[i]);
    bbr->params.usage_based_cwnd = bbr_usage_based_cwnd ? 1 : 0;
    bbr->params.tso_rtt_shift =std::min<uint32_t>(0xFU, bbr_tso_rtt_shift);
    
    //bbr->debug.snd_isn = tp->snd_una;
    //bbr->debug.target_cwnd = 0;
    //bbr->debug.undo = 0;
    
    bbr->init_cwnd = std::min<uint32_t>(0x7FU,tcb->m_initialCWnd);
    bbr->prior_cwnd=0;
    bbr->next_rtt_delivered = 0;
    bbr->prev_ca_state =TcpSocketState::CA_OPEN;
    bbr->packet_conservation = 0;
    
    bbr->probe_rtt_done_stamp=Time(0);
    bbr->probe_rtt_round_done = 0;
    bbr->probe_rtt_min_us=std::numeric_limits<uint32_t>::max();
    bbr->probe_rtt_min_stamp=now;
    bbr->min_rtt_us=std::numeric_limits<uint32_t>::max();
    bbr->min_rtt_stamp=now;
    
    bbr->has_seen_rtt = 0;
    bbr_init_pacing_rate_from_rtt(tcb);
    
    bbr->round_start = 0;
    bbr->idle_restart = 0;
    bbr->full_bw_reached = 0;
    bbr->full_bw = 0;
    bbr->full_bw_cnt = 0;
    bbr->cycle_mstamp =Time(0);
    bbr->cycle_idx = 0;
    bbr->mode = BBR_STARTUP;
    //bbr->debug.rs_bw = 0;

    bbr->ack_epoch_mstamp =Time(0);
    bbr->ack_epoch_acked = 0;
    bbr->extra_acked_win_rtts = 0;
    bbr->extra_acked_win_idx = 0;
    bbr->extra_acked[0] = 0;
    bbr->extra_acked[1] = 0;
    
    bbr->ce_state = 0;
    bbr->prior_rcv_nxt =tcb->m_rxBuffer->NextRxSequence ().GetValue();
    bbr->try_fast_path = 0;
    
#if (TCP_BBR2_DEGUG)
    uint32_t init_cwnd=bbr->init_cwnd;
    NS_LOG_FUNCTION(m_debug->GetUuid()<<init_cwnd);
#endif
}
/* __________________________________________________________________________
 *
 * Functions new to BBR v2 ("bbr") congestion control are below here.
 * __________________________________________________________________________
 */

/* Incorporate a new bw sample into the current window of our max filter. */
void TcpBbr2::bbr2_take_bw_hi_sample(uint32_t bw){
    struct bbr_t *bbr=&m_bbr;
    bbr->bw_hi[1] =std::max<uint32_t>(bw, bbr->bw_hi[1]);
}
/* Keep max of last 1-2 cycles. Each PROBE_BW cycle, flip filter window. */
void TcpBbr2::bbr2_advance_bw_hi_filter()
{
    struct bbr_t *bbr=&m_bbr;
    
    if (!bbr->bw_hi[1])
        return;  /* no samples in this window; remember old window */
    bbr->bw_hi[0] = bbr->bw_hi[1];
    bbr->bw_hi[1] = 0;
}
/* How much do we want in flight? Our BDP, unless congestion cut cwnd. */
uint32_t TcpBbr2::bbr2_target_inflight(uint32_t snd_cwnd)
{
    uint32_t bdp = bbr_inflight(bbr_bw(), BBR_UNIT);
    return std::min<uint32_t>(bdp,snd_cwnd);
}
bool TcpBbr2::bbr2_is_probing_bandwidth()
{
    struct bbr_t *bbr=&m_bbr;
    
    return (bbr->mode == BBR_STARTUP) ||
        (bbr->mode == BBR_PROBE_BW &&
        (bbr->cycle_idx == BBR_BW_PROBE_REFILL ||
        bbr->cycle_idx == BBR_BW_PROBE_UP));
}
/* Has the given amount of time elapsed since we marked the phase start? */
bool TcpBbr2::bbr2_has_elapsed_in_phase(uint32_t interval_us)
{
    struct bbr_t *bbr=&m_bbr;
    Time now=Simulator::Now();
    return now>(bbr->cycle_mstamp +MicroSeconds(interval_us));
}
void TcpBbr2::bbr2_handle_queue_too_high_in_startup()
{
    struct bbr_t *bbr=&m_bbr;
    
    bbr->full_bw_reached = 1;
    bbr->inflight_hi = bbr_inflight(bbr_max_bw(), BBR_UNIT);
}
/* Exit STARTUP upon N consecutive rounds with ECN mark rate > ecn_thresh. */
void TcpBbr2::bbr2_check_ecn_too_high_in_startup(uint32_t ce_ratio)
{
    struct bbr_t *bbr=&m_bbr;
    
    if (bbr_full_bw_reached() || !bbr->ecn_eligible ||
        !bbr->params.full_ecn_cnt || !bbr->params.ecn_thresh)
        return;
    
    if (ce_ratio >= bbr->params.ecn_thresh)
        bbr->startup_ecn_rounds++;
    else
        bbr->startup_ecn_rounds = 0;
    
    if (bbr->startup_ecn_rounds >= bbr->params.full_ecn_cnt) {
        bbr->debug.event = 'E';  /* ECN caused STARTUP exit */
        bbr2_handle_queue_too_high_in_startup();
        return;
    }
}
void TcpBbr2::bbr2_update_ecn_alpha(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc){
    struct bbr_t *bbr=&m_bbr;
    
    int64_t delivered_bytes, delivered_ce_bytes;
    uint64_t alpha, ce_ratio;
    uint32_t gain;
    
    if (bbr->params.ecn_factor == 0)
        return;

    delivered_bytes=rc.m_delivered-bbr->alpha_last_delivered_bytes;
    delivered_ce_bytes=rc.m_deliveredEcnBytes-bbr->alpha_last_delivered_ce_bytes;
    if(0==delivered_bytes||delivered_bytes<0||delivered_ce_bytes<0){
        return ;
    }

    /* See if we should use ECN sender logic for this connection. */
    if (!bbr->ecn_eligible &&m_enableEcn&&
        (bbr->min_rtt_us <= bbr->params.ecn_max_rtt_us ||
        !bbr->params.ecn_max_rtt_us)){
            bbr->ecn_eligible = 1;
        }
    ce_ratio = (uint64_t)delivered_ce_bytes << BBR_SCALE;
    ce_ratio=ce_ratio/delivered_bytes;
    gain = bbr->params.ecn_alpha_gain;
    alpha = ((BBR_UNIT - gain) * bbr->ecn_alpha) >> BBR_SCALE;
    alpha += (gain * ce_ratio) >> BBR_SCALE;
    bbr->ecn_alpha = std::min<uint32_t>(alpha, BBR_UNIT);
    
    bbr->alpha_last_delivered_bytes=rc.m_delivered;
    bbr->alpha_last_delivered_ce_bytes=rc.m_deliveredEcnBytes;
    bbr2_check_ecn_too_high_in_startup(ce_ratio);
}

/* Each round trip of BBR_BW_PROBE_UP, double volume of probing data. */
void TcpBbr2::bbr2_raise_inflight_hi_slope(uint32_t snd_cwnd)
{
    struct bbr_t *bbr=&m_bbr;

    uint32_t growth_this_round, cnt;
    
    /* Calculate "slope": packets S/Acked per inflight_hi increment. */
    growth_this_round = 1 << bbr->bw_probe_up_rounds;
    bbr->bw_probe_up_rounds = std::min<uint32_t>(bbr->bw_probe_up_rounds + 1, 30);
    cnt =snd_cwnd/ growth_this_round;
    cnt = std::max<uint32_t>(cnt, 1U);
    bbr->bw_probe_up_cnt = cnt;
    bbr->debug.event = 'G';  /* Grow inflight_hi slope */
}
/* In BBR_BW_PROBE_UP, not seeing high loss/ECN/queue, so raise inflight_hi. */
void TcpBbr2::bbr2_probe_inflight_hi_upward(uint32_t snd_cwnd,uint32_t acked_sacked)
{
    struct bbr_t *bbr=&m_bbr;
    
    uint32_t delta;
    if (/*(!tp->is_cwnd_limited ||*/snd_cwnd< bbr->inflight_hi) {
        bbr->bw_probe_up_acks = 0;  /* don't accmulate unused credits */
        return;  /* not fully using inflight_hi, so don't grow it */
    }
    
    /* For each bw_probe_up_cnt packets ACKed, increase inflight_hi by 1. */
    bbr->bw_probe_up_acks +=acked_sacked;
    if (bbr->bw_probe_up_acks >=  bbr->bw_probe_up_cnt) {
        delta = bbr->bw_probe_up_acks / bbr->bw_probe_up_cnt;
        bbr->bw_probe_up_acks -= delta * bbr->bw_probe_up_cnt;
        bbr->inflight_hi += delta;
        bbr->debug.event = 'I';  /* Increment inflight_hi */
    }
    
    if (bbr->round_start)
        bbr2_raise_inflight_hi_slope(snd_cwnd);
}
/* Does loss/ECN rate for this sample say inflight is "too high"?
 * This is used by both the bbr_check_loss_too_high_in_startup() function,
 * which can be used in either v1 or v2, and the PROBE_UP phase of v2, which
 * uses it to notice when loss/ECN rates suggest inflight is too high.
 */
bool TcpBbr2::bbr2_is_inflight_too_high(const TcpRateOps::TcpRateSample &rs){
    struct bbr_t *bbr=&m_bbr;

    uint32_t loss_thresh, ecn_thresh;
    if(rs.m_bytesLostRound>0&&rs.m_txInFlight>0){
        loss_thresh = (uint64_t)rs.m_txInFlight * bbr->params.loss_thresh >>
                BBR_SCALE;
        if(rs.m_bytesLostRound>loss_thresh){
            #if (TCP_BBR2_DEGUG)
            NS_LOG_FUNCTION(m_debug->GetUuid()<<"loss too high");
            #endif
            return true;
        }
    }
    
    if(rs.m_ecnBytesRound>0&&rs.m_delivered>0&&
        bbr->ecn_eligible && bbr->params.ecn_thresh){
        ecn_thresh = (uint64_t)rs.m_delivered * bbr->params.ecn_thresh >>
				BBR_SCALE;
        if(rs.m_ecnBytesRound>ecn_thresh){
            #if (TCP_BBR2_DEGUG)
            NS_LOG_FUNCTION(m_debug->GetUuid()<<"ecn too high");
            #endif
            return true;
        }
    }
    
    return false;
}
/* Calculate the tx_in_flight level that corresponded to excessive loss.
 * We find "lost_prefix" segs of the skb where loss rate went too high,
 * by solving for "lost_prefix" in the following equation:
 *   lost                     /  inflight                     >= loss_thresh
 *  (lost_prev + lost_prefix) / (inflight_prev + lost_prefix) >= loss_thresh
 * Then we take that equation, convert it to fixed point, and
 * round up to the nearest packet.
 */
uint32_t TcpBbr2::bbr2_inflight_hi_from_lost_skb(const TcpRateOps::TcpRateSample &rs,uint32_t packet_bytes,uint32_t mss){
    struct bbr_t *bbr=&m_bbr;
   
    uint32_t loss_thresh  = bbr->params.loss_thresh;
    uint32_t pcount, divisor, tx_in_flight,inflight_hi,lost;
    int inflight_prev, lost_prev;
    uint64_t loss_budget, lost_prefix;
    
    pcount =div_round_up_ull(packet_bytes,mss);
    tx_in_flight=0;
    if(rs.m_txInFlight>0){
        tx_in_flight=div_round_up_ull(rs.m_txInFlight,mss);
    }
    lost=div_round_up_ull(rs.m_bytesLostRound,mss);
    /* How much data was in flight before this skb? */
    inflight_prev =tx_in_flight- pcount;
    if(inflight_prev < 0){
        NS_LOG_FUNCTION("negative inflight"<<tx_in_flight<<pcount);
        return ~0U;
    }

    /* How much inflight data was marked lost before this skb? */
    lost_prev =lost- pcount;
    if(lost_prev<0){
        NS_LOG_FUNCTION("negative lost_prev"<<lost<<pcount);
        return ~0U;
    }
    
    /* At what prefix of this lost skb did losss rate exceed loss_thresh? */
    loss_budget = (uint64_t)inflight_prev * loss_thresh + BBR_UNIT - 1;
    loss_budget >>= BBR_SCALE;
    if (lost_prev >= loss_budget) {
        lost_prefix = 0;   /* previous losses crossed loss_thresh */
    } else {
        lost_prefix = loss_budget - lost_prev;
        lost_prefix <<= BBR_SCALE;
        divisor = BBR_UNIT - loss_thresh;
        if (!divisor)  /* loss_thresh is 8 bits */{
            NS_LOG_FUNCTION("zero divisor "<<divisor);
            return ~0U;
        }
        lost_prefix=lost_prefix/divisor;
    }
    
    inflight_hi = inflight_prev + lost_prefix;
    return inflight_hi;
}
/* If loss/ECN rates during probing indicated we may have overfilled a
 * buffer, return an operating point that tries to leave unutilized headroom in
 * the path for other flows, for fairness convergence and lower RTTs and loss.
 */
uint32_t TcpBbr2::bbr2_inflight_with_headroom(){
    struct bbr_t *bbr=&m_bbr;
    
    uint32_t headroom, headroom_fraction;

    if (bbr->inflight_hi == ~0U)
        return ~0U;
    
    headroom_fraction = bbr->params.inflight_headroom;
    headroom = ((uint64_t)bbr->inflight_hi * headroom_fraction) >> BBR_SCALE;
    headroom = std::max<uint32_t>(headroom, 1U);
    return std::max<int>(bbr->inflight_hi - headroom,bbr->params.cwnd_min_target);
}
/* Bound cwnd to a sensible level, based on our current probing state
 * machine phase and model of a good inflight level (inflight_lo, inflight_hi).
 */
void TcpBbr2::bbr2_bound_cwnd_for_inflight_model(Ptr<TcpSocketState> tcb)
{
    struct bbr_t *bbr=&m_bbr;
    uint32_t mss=tcb->m_segmentSize;
    uint32_t cap,snd_cwnd;
    
    /* tcp_rcv_synsent_state_process() currently calls tcp_ack()
    * and thus cong_control() without first initializing us(!).
    */
    if (!bbr->initialized)
        return;
    
    cap = ~0U;
    snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    if (bbr->mode == BBR_PROBE_BW &&
        bbr->cycle_idx != BBR_BW_PROBE_CRUISE) {
        /* Probe to see if more packets fit in the path. */
        cap = bbr->inflight_hi;
    } else {
        if (bbr->mode == BBR_PROBE_RTT ||
            (bbr->mode == BBR_PROBE_BW &&
            bbr->cycle_idx == BBR_BW_PROBE_CRUISE))
            cap = bbr2_inflight_with_headroom();
    }
    /* Adapt to any loss/ECN since our last bw probe. */
    cap = std::min<uint32_t>(cap, bbr->inflight_lo);
    
    cap = std::max<uint32_t>(cap, bbr->params.cwnd_min_target);
    
    snd_cwnd= std::min<uint32_t>(cap,snd_cwnd);
    tcb->m_cWnd=snd_cwnd*mss;
}
/* Estimate a short-term lower bound on the capacity available now, based
 * on measurements of the current delivery process and recent history. When we
 * are seeing loss/ECN at times when we are not probing bw, then conservatively
 * move toward flow balance by multiplicatively cutting our short-term
 * estimated safe rate and volume of data (bw_lo and inflight_lo). We use a
 * multiplicative decrease in order to converge to a lower capacity in time
 * logarithmic in the magnitude of the decrease.
 *
 * However, we do not cut our short-term estimates lower than the current rate
 * and volume of delivered data from this round trip, since from the current
 * delivery process we can estimate the measured capacity available now.
 *
 * Anything faster than that approach would knowingly risk high loss, which can
 * cause low bw for Reno/CUBIC and high loss recovery latency for
 * request/response flows using any congestion control.
 */
void TcpBbr2::bbr2_adapt_lower_bounds(uint32_t snd_cwnd){
    struct bbr_t *bbr=&m_bbr;
    
    uint32_t ecn_cut, ecn_inflight_lo, beta;
    
    /* We only use lower-bound estimates when not probing bw.
    * When probing we need to push inflight higher to probe bw.
    */
    if (bbr2_is_probing_bandwidth())
        return;

    /* ECN response. */
    if (bbr->ecn_in_round && bbr->ecn_eligible && bbr->params.ecn_factor) {
        /* Reduce inflight to (1 - alpha*ecn_factor). */
        ecn_cut = (BBR_UNIT -
            ((bbr->ecn_alpha * bbr->params.ecn_factor) >>
                BBR_SCALE));
        if (bbr->inflight_lo == ~0U)
            bbr->inflight_lo =snd_cwnd;
        ecn_inflight_lo = (uint64_t)bbr->inflight_lo * ecn_cut >> BBR_SCALE;
    } else {
        ecn_inflight_lo = ~0U;
    }
    
    /* Loss response. */
    if (bbr->loss_in_round) {
        /* Reduce bw and inflight to (1 - beta). */
        if (bbr->bw_lo == ~0U)
            bbr->bw_lo = bbr_max_bw();
        if (bbr->inflight_lo == ~0U)
            bbr->inflight_lo =snd_cwnd;
        beta = bbr->params.beta;
        bbr->bw_lo =std::max<uint32_t>(bbr->bw_latest,
                (uint64_t)bbr->bw_lo *(BBR_UNIT - beta) >> BBR_SCALE);
        bbr->inflight_lo =
            std::max<uint32_t>(bbr->inflight_latest,
                (uint64_t)bbr->inflight_lo *(BBR_UNIT - beta) >> BBR_SCALE);
    }

    /* Adjust to the lower of the levels implied by loss or ECN. */
    bbr->inflight_lo =std::min<uint32_t>(bbr->inflight_lo, ecn_inflight_lo);
}

/* Reset any short-term lower-bound adaptation to congestion, so that we can
 * push our inflight up.
 */
void TcpBbr2::bbr2_reset_lower_bounds()
{
    struct bbr_t *bbr=&m_bbr;
    
    bbr->bw_lo = ~0U;
    bbr->inflight_lo = ~0U;
}
/* After bw probing (STARTUP/PROBE_UP), reset signals before entering a state
 * machine phase where we adapt our lower bound based on congestion signals.
 */
void TcpBbr2::bbr2_reset_congestion_signals()
{
    struct bbr_t *bbr=&m_bbr;
    
    bbr->loss_in_round = 0;
    bbr->ecn_in_round = 0;
    bbr->loss_in_cycle = 0;
    bbr->ecn_in_cycle = 0;
    bbr->bw_latest = 0;
    bbr->inflight_latest = 0;
}
/* Update (most of) our congestion signals: track the recent rate and volume of
 * delivered data, presence of loss, and EWMA degree of ECN marking.
 */
void TcpBbr2::bbr2_update_congestion_signals(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                                    const TcpRateOps::TcpRateSample &rs,struct bbr_context *ctx){
    struct bbr_t *bbr=&m_bbr;

    uint32_t mss=tcb->m_segmentSize;
    uint64_t bw;
    uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    uint32_t delivered_packets=div_round_up_ull(rs.m_delivered,mss);
    bbr->loss_round_start = 0;
    if (rs.m_interval.IsZero()||!rs.m_ackedSacked)
        return; /* Not a valid observation */
    bw = ctx->sample_bw;
    
    if (!rs.m_isAppLimited || bw >= bbr_max_bw())
        bbr2_take_bw_hi_sample(bw);
    
    bbr->loss_in_round |= (rs.m_bytesLoss > 0);

    /* Update rate and volume of delivered data from latest round trip: */
    bbr->bw_latest       = std::max<uint32_t>(bbr->bw_latest,ctx->sample_bw);
    bbr->inflight_latest =std::max<uint32_t>(bbr->inflight_latest,delivered_packets);
    
    if (rs.m_priorDelivered<bbr->loss_round_delivered)
        return;		/* skip the per-round-trip updates */
    /* Now do per-round-trip updates. */
    bbr->loss_round_delivered =rc.m_delivered;  /* mark round trip */
    bbr->loss_round_start = 1;
    bbr2_adapt_lower_bounds(snd_cwnd);
    
    /* Update windowed "latest" (single-round-trip) filters. */
    bbr->loss_in_round = 0;
    bbr->ecn_in_round  = 0;
    bbr->bw_latest = ctx->sample_bw;
    bbr->inflight_latest =delivered_packets;
}
/* Bandwidth probing can cause loss. To help coexistence with loss-based
 * congestion control we spread out our probing in a Reno-conscious way. Due to
 * the shape of the Reno sawtooth, the time required between loss epochs for an
 * idealized Reno flow is a number of round trips that is the BDP of that
 * flow. We count packet-timed round trips directly, since measured RTT can
 * vary widely, and Reno is driven by packet-timed round trips.
 */
bool TcpBbr2::bbr2_is_reno_coexistence_probe_time(uint32_t snd_cwnd){
    struct bbr_t *bbr=&m_bbr;
    
    uint32_t inflight, rounds, reno_gain, reno_rounds;
    
    /* Random loss can shave some small percentage off of our inflight
    * in each round. To survive this, flows need robust periodic probes.
    */
    rounds = bbr->params.bw_probe_max_rounds;
    
    reno_gain = bbr->params.bw_probe_reno_gain;
    if (reno_gain) {
        inflight = bbr2_target_inflight(snd_cwnd);
        reno_rounds = ((uint64_t)inflight * reno_gain) >> BBR_SCALE;
        rounds =std::min<uint32_t>(rounds, reno_rounds);
    }
    return bbr->rounds_since_probe >= rounds;
}
/* How long do we want to wait before probing for bandwidth (and risking
 * loss)? We randomize the wait, for better mixing and fairness convergence.
 *
 * We bound the Reno-coexistence inter-bw-probe time to be 62-63 round trips.
 * This is calculated to allow fairness with a 25Mbps, 30ms Reno flow,
 * (eg 4K video to a broadband user):
 *   BDP = 25Mbps * .030sec /(1514bytes) = 61.9 packets
 *
 * We bound the BBR-native inter-bw-probe wall clock time to be:
 *  (a) higher than 2 sec: to try to avoid causing loss for a long enough time
 *      to allow Reno at 30ms to get 4K video bw, the inter-bw-probe time must
 *      be at least: 25Mbps * .030sec / (1514bytes) * 0.030sec = 1.9secs
 *  (b) lower than 3 sec: to ensure flows can start probing in a reasonable
 *      amount of time to discover unutilized bw on human-scale interactive
 *      time-scales (e.g. perhaps traffic from a web page download that we
 *      were competing with is now complete).
 */
void TcpBbr2::bbr2_pick_probe_wait(){
    struct bbr_t *bbr=&m_bbr;
    
    /* Decide the random round-trip bound for wait until probe: */
    bbr->rounds_since_probe =
        MockRandomU32Max(bbr->params.bw_probe_rand_rounds);
    /* Decide the random wall clock bound for wait until probe: */
    bbr->probe_wait_us = bbr->params.bw_probe_base_us +
                MockRandomU32Max(bbr->params.bw_probe_rand_us);
}

void TcpBbr2::bbr2_set_cycle_idx(int cycle_idx)
{
    struct bbr_t *bbr=&m_bbr;
    
    bbr->cycle_idx = cycle_idx;
    /* New phase, so need to update cwnd and pacing rate. */
    bbr->try_fast_path = 0;
}
/* Send at estimated bw to fill the pipe, but not queue. We need this phase
 * before PROBE_UP, because as soon as we send faster than the available bw
 * we will start building a queue, and if the buffer is shallow we can cause
 * loss. If we do not fill the pipe before we cause this loss, our bw_hi and
 * inflight_hi estimates will underestimate.
 */
void TcpBbr2::bbr2_start_bw_probe_refill(uint32_t bw_probe_up_rounds,uint64_t delivered_bytes)
{
    struct bbr_t *bbr=&m_bbr;
    
    bbr2_reset_lower_bounds();
    if (bbr->inflight_hi != ~0U)
        bbr->inflight_hi += bbr->params.refill_add_inc;
    bbr->bw_probe_up_rounds = bw_probe_up_rounds;
    bbr->bw_probe_up_acks = 0;
    bbr->stopped_risky_probe = 0;
    bbr->ack_phase = BBR_ACKS_REFILLING;
    bbr->next_rtt_delivered =delivered_bytes;
    bbr2_set_cycle_idx(BBR_BW_PROBE_REFILL);
}
/* Now probe max deliverable data rate and volume. */
void TcpBbr2::bbr2_start_bw_probe_up(uint64_t delivered_bytes,uint32_t snd_cwnd){
    struct bbr_t *bbr=&m_bbr;

    bbr->ack_phase = BBR_ACKS_PROBE_STARTING;
    bbr->next_rtt_delivered =delivered_bytes;
    bbr->cycle_mstamp =Simulator::Now();
    bbr2_set_cycle_idx(BBR_BW_PROBE_UP);
    bbr2_raise_inflight_hi_slope(snd_cwnd);
}
/* Start a new PROBE_BW probing cycle of some wall clock length. Pick a wall
 * clock time at which to probe beyond an inflight that we think to be
 * safe. This will knowingly risk packet loss, so we want to do this rarely, to
 * keep packet loss rates low. Also start a round-trip counter, to probe faster
 * if we estimate a Reno flow at our BDP would probe faster.
 */
void TcpBbr2::bbr2_start_bw_probe_down(uint64_t delivered_bytes)
{
    struct bbr_t *bbr=&m_bbr;

    bbr2_reset_congestion_signals();
    bbr->bw_probe_up_cnt = ~0U;     /* not growing inflight_hi any more */
    bbr2_pick_probe_wait();
    bbr->cycle_mstamp =Simulator::Now();;		/* start wall clock */
    bbr->ack_phase = BBR_ACKS_PROBE_STOPPING;
    bbr->next_rtt_delivered =delivered_bytes;
    bbr2_set_cycle_idx(BBR_BW_PROBE_DOWN);
}

/* Cruise: maintain what we estimate to be a neutral, conservative
 * operating point, without attempting to probe up for bandwidth or down for
 * RTT, and only reducing inflight in response to loss/ECN signals.
 */
void TcpBbr2::bbr2_start_bw_probe_cruise()
{
    struct bbr_t *bbr=&m_bbr;
    
    if (bbr->inflight_lo != ~0U)
        bbr->inflight_lo =std::min<uint32_t>(bbr->inflight_lo, bbr->inflight_hi);
    
    bbr2_set_cycle_idx(BBR_BW_PROBE_CRUISE);
}

/* Loss and/or ECN rate is too high while probing.
 * Adapt (once per bw probe) by cutting inflight_hi and then restarting cycle.
 */
void TcpBbr2::bbr2_handle_inflight_too_high(bool is_app_limited,uint32_t snd_cwnd,uint32_t tx_in_flight,uint64_t delivered_bytes)
{
    struct bbr_t *bbr=&m_bbr;

    const uint32_t beta = bbr->params.beta;
    
    
    bbr->prev_probe_too_high = 1;
    bbr->bw_probe_samples = 0;  /* only react once per probe */
    bbr->debug.event = 'L';     /* Loss/ECN too high */
    /* If we are app-limited then we are not robustly
    * probing the max volume of inflight data we think
    * might be safe (analogous to how app-limited bw
    * samples are not known to be robustly probing bw).
    */
    if (!is_app_limited)
        bbr->inflight_hi = std::max<uint32_t>(tx_in_flight,
                    (uint64_t)bbr2_target_inflight(snd_cwnd) *
                    (BBR_UNIT - beta) >> BBR_SCALE);
    if (bbr->mode == BBR_PROBE_BW && bbr->cycle_idx == BBR_BW_PROBE_UP)
        bbr2_start_bw_probe_down(delivered_bytes);
}

/* If we're seeing bw and loss samples reflecting our bw probing, adapt
 * using the signals we see. If loss or ECN mark rate gets too high, then adapt
 * inflight_hi downward. If we're able to push inflight higher without such
 * signals, push higher: adapt inflight_hi upward.
 */
bool TcpBbr2::bbr2_adapt_upper_bounds(const TcpRateOps::TcpRateSample &rs,uint32_t snd_cwnd,
                            uint32_t tx_in_flight,uint32_t acked_sacked,uint64_t delivered_bytes)
{
    struct bbr_t *bbr=&m_bbr;
    
    /* Track when we'll see bw/loss samples resulting from our bw probes. */
    if (bbr->ack_phase == BBR_ACKS_PROBE_STARTING && bbr->round_start)
        bbr->ack_phase = BBR_ACKS_PROBE_FEEDBACK;
    if (bbr->ack_phase == BBR_ACKS_PROBE_STOPPING && bbr->round_start) {
        /* End of samples from bw probing phase. */
        bbr->bw_probe_samples = 0;
        bbr->ack_phase = BBR_ACKS_INIT;
        /* At this point in the cycle, our current bw sample is also
        * our best recent chance at finding the highest available bw
        * for this flow. So now is the best time to forget the bw
        * samples from the previous cycle, by advancing the window.
        */
        if (bbr->mode == BBR_PROBE_BW && !rs.m_isAppLimited)
            bbr2_advance_bw_hi_filter();
        /* If we had an inflight_hi, then probed and pushed inflight all
        * the way up to hit that inflight_hi without seeing any
        * high loss/ECN in all the resulting ACKs from that probing,
        * then probe up again, this time letting inflight persist at
        * inflight_hi for a round trip, then accelerating beyond.
        */
        if (bbr->mode == BBR_PROBE_BW &&
            bbr->stopped_risky_probe && !bbr->prev_probe_too_high) {
            bbr->debug.event = 'R';  /* reprobe */
            bbr2_start_bw_probe_refill(0,delivered_bytes);
            return true;  /* yes, decided state transition */
        }
    }

    if (bbr2_is_inflight_too_high(rs)) {
        if (bbr->bw_probe_samples)  /*  sample is from bw probing? */
            bbr2_handle_inflight_too_high(rs.m_isAppLimited,snd_cwnd,tx_in_flight,delivered_bytes);
    } else {
        /* Loss/ECN rate is declared safe. Adjust upper bound upward. */
        if (bbr->inflight_hi == ~0U)  /* no excess queue signals yet? */
            return false;
    
        /* To be resilient to random loss, we must raise inflight_hi
        * if we observe in any phase that a higher level is safe.
        */
        if (tx_in_flight> bbr->inflight_hi) {
            bbr->inflight_hi =tx_in_flight;
            bbr->debug.event = 'U';  /* raise up inflight_hi */
        }
    
        if (bbr->mode == BBR_PROBE_BW &&
            bbr->cycle_idx == BBR_BW_PROBE_UP)
            bbr2_probe_inflight_hi_upward(snd_cwnd,acked_sacked);
    }
    
    return false;
}

/* Check if it's time to probe for bandwidth now, and if so, kick it off. */
bool TcpBbr2::bbr2_check_time_to_probe_bw(Ptr<TcpSocketState> tcb,
                    uint32_t snd_cwnd,uint64_t delivered_bytes)
{
    struct bbr_t *bbr=&m_bbr;
    uint32_t n;

    /* If we seem to be at an operating point where we are not seeing loss
    * but we are seeing ECN marks, then when the ECN marks cease we reprobe
    * quickly (in case a burst of cross-traffic has ceased and freed up bw,
    * or in case we are sharing with multiplicatively probing traffic).
    */
    if (bbr->params.ecn_reprobe_gain && bbr->ecn_eligible &&
        bbr->ecn_in_cycle && !bbr->loss_in_cycle &&
        tcb->m_congState==TcpSocketState::CA_OPEN) {
        bbr->debug.event = 'A';  /* *A*ll clear to probe *A*gain */
        /* Calculate n so that when bbr2_raise_inflight_hi_slope()
        * computes growth_this_round as 2^n it will be roughly the
        * desired volume of data (inflight_hi*ecn_reprobe_gain).
        */
        n = ilog2((((uint64_t)bbr->inflight_hi *
                bbr->params.ecn_reprobe_gain) >> BBR_SCALE));
        bbr2_start_bw_probe_refill(n,delivered_bytes);
        return true;
    }

    if (bbr2_has_elapsed_in_phase(bbr->probe_wait_us) ||
        bbr2_is_reno_coexistence_probe_time(snd_cwnd)) {
        bbr2_start_bw_probe_refill(0,delivered_bytes);
        return true;
    }
    return false;
}

/* Is it time to transition from PROBE_DOWN to PROBE_CRUISE? */
bool TcpBbr2::bbr2_check_time_to_cruise(uint32_t inflight, uint32_t bw)
{
    struct bbr_t *bbr=&m_bbr;
    bool is_under_bdp, is_long_enough;
    
    /* Always need to pull inflight down to leave headroom in queue. */
    if (inflight > bbr2_inflight_with_headroom())
        return false;
    
    is_under_bdp = inflight <= bbr_inflight(bw, BBR_UNIT);
    if (bbr->params.drain_to_target)
        return is_under_bdp;
    
    is_long_enough = bbr2_has_elapsed_in_phase(bbr->min_rtt_us);
    return is_under_bdp || is_long_enough;
}

/* PROBE_BW state machine: cruise, refill, probe for bw, or drain? */
void TcpBbr2::bbr2_update_cycle_phase(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                                    const TcpRateOps::TcpRateSample &rs){
    struct bbr_t *bbr=&m_bbr;
    
    uint32_t mss=tcb->m_segmentSize;
    uint32_t snd_cwnd=div_round_up_ull(tcb->m_cWnd,mss);
    uint32_t tx_in_flight=div_round_up_ull(rs.m_txInFlight,mss);
    uint32_t acked_sacked=div_round_up_ull(rs.m_ackedSacked,mss);;
    uint32_t prior_in_flight_packets=div_round_up_ull(rs.m_priorInFlight,mss);
    bool is_risky = false, is_queuing = false;
    uint32_t inflight, bw;
    
    if (!bbr_full_bw_reached())
        return;
    
    /* In DRAIN, PROBE_BW, or PROBE_RTT, adjust upper bounds. */
    if (bbr2_adapt_upper_bounds(rs,snd_cwnd,tx_in_flight,acked_sacked,rc.m_delivered))
        return;		/* already decided state transition */
    
    if (bbr->mode != BBR_PROBE_BW)
        return;
    
    inflight = bbr_packets_in_net_at_edt(prior_in_flight_packets);
    bw = bbr_max_bw();

    switch (bbr->cycle_idx) {
    /* First we spend most of our time cruising with a pacing_gain of 1.0,
    * which paces at the estimated bw, to try to fully use the pipe
    * without building queue. If we encounter loss/ECN marks, we adapt
    * by slowing down.
    */
    case BBR_BW_PROBE_CRUISE:
        if (bbr2_check_time_to_probe_bw(tcb,snd_cwnd,rc.m_delivered))
            return;		/* already decided state transition */
        break;
    
    /* After cruising, when it's time to probe, we first "refill": we send
    * at the estimated bw to fill the pipe, before probing higher and
    * knowingly risking overflowing the bottleneck buffer (causing loss).
    */
    case BBR_BW_PROBE_REFILL:
        if (bbr->round_start) {
            /* After one full round trip of sending in REFILL, we
            * start to see bw samples reflecting our REFILL, which
            * may be putting too much data in flight.
            */
            bbr->bw_probe_samples = 1;
            bbr2_start_bw_probe_up(rc.m_delivered,snd_cwnd);
        }
        break;
    
    /* After we refill the pipe, we probe by using a pacing_gain > 1.0, to
    * probe for bw. If we have not seen loss/ECN, we try to raise inflight
    * to at least pacing_gain*BDP; note that this may take more than
    * min_rtt if min_rtt is small (e.g. on a LAN).
    *
    * We terminate PROBE_UP bandwidth probing upon any of the following:
    *
    * (1) We've pushed inflight up to hit the inflight_hi target set in the
    *     most recent previous bw probe phase. Thus we want to start
    *     draining the queue immediately because it's very likely the most
    *     recently sent packets will fill the queue and cause drops.
    *     (checked here)
    * (2) We have probed for at least 1*min_rtt_us, and the
    *     estimated queue is high enough (inflight > 1.25 * estimated_bdp).
    *     (checked here)
    * (3) Loss filter says loss rate is "too high".
    *     (checked in bbr_is_inflight_too_high())
    * (4) ECN filter says ECN mark rate is "too high".
    *     (checked in bbr_is_inflight_too_high())
    */
    case BBR_BW_PROBE_UP:
        if (bbr->prev_probe_too_high &&
            inflight >= bbr->inflight_hi) {
            bbr->stopped_risky_probe = 1;
            is_risky = true;
            bbr->debug.event = 'D';   /* D for danger */
        } else if (bbr2_has_elapsed_in_phase(bbr->min_rtt_us) &&
            inflight >=
            bbr_inflight(bw,bbr->params.bw_probe_pif_gain)) {
            is_queuing = true;
            bbr->debug.event = 'Q'; /* building Queue */
        }
        if (is_risky || is_queuing) {
            bbr->prev_probe_too_high = 0;  /* no loss/ECN (yet) */
            bbr2_start_bw_probe_down(rc.m_delivered);  /* restart w/ down */
        }
        break;
    
    /* After probing in PROBE_UP, we have usually accumulated some data in
    * the bottleneck buffer (if bw probing didn't find more bw). We next
    * enter PROBE_DOWN to try to drain any excess data from the queue. To
    * do this, we use a pacing_gain < 1.0. We hold this pacing gain until
    * our inflight is less then that target cruising point, which is the
    * minimum of (a) the amount needed to leave headroom, and (b) the
    * estimated BDP. Once inflight falls to match the target, we estimate
    * the queue is drained; persisting would underutilize the pipe.
    */
    case BBR_BW_PROBE_DOWN:
        if (bbr2_check_time_to_probe_bw(tcb,snd_cwnd,rc.m_delivered))
            return;		/* already decided state transition */
        if (bbr2_check_time_to_cruise(inflight, bw))
            bbr2_start_bw_probe_cruise();
        break;
    
    default:
            NS_ASSERT_MSG(0, "BBR invalid cycle index "<<bbr->cycle_idx);
    }
}

/* Exiting PROBE_RTT, so return to bandwidth probing in STARTUP or PROBE_BW. */
void TcpBbr2::bbr2_exit_probe_rtt(uint64_t delivered_bytes)
{
    struct bbr_t *bbr=&m_bbr;
    
    bbr2_reset_lower_bounds();
    if (bbr_full_bw_reached()) {
        bbr->mode = BBR_PROBE_BW;
        /* Raising inflight after PROBE_RTT may cause loss, so reset
        * the PROBE_BW clock and schedule the next bandwidth probe for
        * a friendly and randomized future point in time.
        */
        bbr2_start_bw_probe_down(delivered_bytes);
        /* Since we are exiting PROBE_RTT, we know inflight is
        * below our estimated BDP, so it is reasonable to cruise.
        */
        bbr2_start_bw_probe_cruise();
    } else {
        bbr->mode = BBR_STARTUP;
    }
}

/* Exit STARTUP based on loss rate > 1% and loss gaps in round >= N. Wait until
 * the end of the round in recovery to get a good estimate of how many packets
 * have been lost, and how many we need to drain with a low pacing rate.
 */
void TcpBbr2::bbr2_check_loss_too_high_in_startup(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateSample &rs)
{
    struct bbr_t *bbr=&m_bbr;
    
    if (bbr_full_bw_reached())
        return;
    
    /* For STARTUP exit, check the loss rate at the end of each round trip
    * of Recovery episodes in STARTUP. We check the loss rate at the end
    * of the round trip to filter out noisy/low loss and have a better
    * sense of inflight (extent of loss), so we can drain more accurately.
    */
    if (rs.m_bytesLoss && bbr->loss_events_in_round < 0xf)
        bbr->loss_events_in_round++;  /* update saturating counter */
    if (bbr->params.full_loss_cnt && bbr->loss_round_start &&
        tcb->m_congState==TcpSocketState::CA_RECOVERY&&
        bbr->loss_events_in_round >= bbr->params.full_loss_cnt &&
        bbr2_is_inflight_too_high(rs)) {
        bbr->debug.event = 'P';  /* Packet loss caused STARTUP exit */
        bbr2_handle_queue_too_high_in_startup();
        return;
    }
    if (bbr->loss_round_start)
        bbr->loss_events_in_round = 0;
}

/* If we are done draining, advance into steady state operation in PROBE_BW. */
void TcpBbr2::bbr2_check_drain(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc)
{
    struct bbr_t *bbr=&m_bbr;
    
    if (bbr_check_drain(tcb)) {
        bbr->mode = BBR_PROBE_BW;
        bbr2_start_bw_probe_down(rc.m_delivered);
    }
}
void TcpBbr2::bbr2_update_model(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                        const TcpRateOps::TcpRateSample &rs,struct bbr_context *ctx){
    bbr2_update_congestion_signals(tcb,rc,rs,ctx);
    bbr_update_ack_aggregation(tcb,rc,rs);
    bbr2_check_loss_too_high_in_startup(tcb, rs);
    bbr_check_full_bw_reached(rs);
    bbr2_check_drain(tcb,rc);
    bbr2_update_cycle_phase(tcb,rc,rs);
    bbr_update_min_rtt(tcb,rc,rs);
}

/* Fast path for app-limited case.
 *
 * On each ack, we execute bbr state machine, which primarily consists of:
 * 1) update model based on new rate sample, and
 * 2) update control based on updated model or state change.
 *
 * There are certain workload/scenarios, e.g. app-limited case, where
 * either we can skip updating model or we can skip update of both model
 * as well as control. This provides signifcant softirq cpu savings for
 * processing incoming acks.
 *
 * In case of app-limited, if there is no congestion (loss/ecn) and
 * if observed bw sample is less than current estimated bw, then we can
 * skip some of the computation in bbr state processing:
 *
 * - if there is no rtt/mode/phase change: In this case, since all the
 *   parameters of the network model are constant, we can skip model
 *   as well control update.
 *
 * - else we can skip rest of the model update. But we still need to
 *   update the control to account for the new rtt/mode/phase.
 *
 * Returns whether we can take fast path or not.
 */
bool TcpBbr2::bbr2_fast_path(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
       const TcpRateOps::TcpRateSample &rs,bool *update_model,struct bbr_context *ctx){
    struct bbr_t *bbr=&m_bbr;
    uint32_t prev_min_rtt_us, prev_mode;
    
    if (bbr->params.fast_path && bbr->try_fast_path &&
        rs.m_isAppLimited && ctx->sample_bw < bbr_max_bw() &&
        !bbr->loss_in_round && !bbr->ecn_in_round) {
        prev_mode = bbr->mode;
        prev_min_rtt_us = bbr->min_rtt_us;
        bbr2_check_drain(tcb,rc);
        bbr2_update_cycle_phase(tcb,rc,rs);
        bbr_update_min_rtt(tcb,rc,rs);
    
        if (bbr->mode == prev_mode &&
            bbr->min_rtt_us == prev_min_rtt_us &&
            bbr->try_fast_path)
            return true;
    
        /* Skip model update, but control still needs to be updated */
        *update_model = false;
    }
    return false;
}
/* Module parameters that are settable by TCP_CONGESTION_PARAMS are declared
 * down here, so that the algorithm functions that use the parameters must use
 * the per-socket parameters; if they accidentally use the global version
 * then there will be a compile error.
 * TODO(ncardwell): move all per-socket parameters down to this section.
 */

/* On losses, scale down inflight and pacing rate by beta scaled by BBR_SCALE.
 * No loss response when 0. Max allwed value is 255.
 */
static uint32_t bbr_beta = BBR_UNIT * 30 / 100;

/* Gain factor for ECN mark ratio samples, scaled by BBR_SCALE.
 * Max allowed value is 255.
 */
static uint32_t bbr_ecn_alpha_gain = BBR_UNIT * 1 / 16;  /* 1/16 = 6.25% */

/* The initial value for the ecn_alpha state variable. Default and max
 * BBR_UNIT (256), representing 1.0. This allows a flow to respond quickly
 * to congestion if the bottleneck is congested when the flow starts up.
 */
static uint32_t bbr_ecn_alpha_init = BBR_UNIT;	/* 1.0, to respond quickly */

/* On ECN, cut inflight_lo to (1 - ecn_factor * ecn_alpha) scaled by BBR_SCALE.
 * No ECN based bounding when 0. Max allwed value is 255.
 */
static uint32_t bbr_ecn_factor = BBR_UNIT * 1 / 3;	    /* 1/3 = 33% */

/* Estimate bw probing has gone too far if CE ratio exceeds this threshold.
 * Scaled by BBR_SCALE. Disabled when 0. Max allowed is 255.
 */
static uint32_t bbr_ecn_thresh = BBR_UNIT * 1 / 2;  /* 1/2 = 50% */

/* Max RTT (in usec) at which to use sender-side ECN logic.
 * Disabled when 0 (ECN allowed at any RTT).
 * Max allowed for the parameter is 524287 (0x7ffff) us, ~524 ms.
 */
static uint32_t bbr_ecn_max_rtt_us = 5000;

/* If non-zero, if in a cycle with no losses but some ECN marks, after ECN
 * clears then use a multiplicative increase to quickly reprobe bw by
 * starting inflight probing at the given multiple of inflight_hi.
 * Default for this experimental knob is 0 (disabled).
 * Planned value for experiments: BBR_UNIT * 1 / 2 = 128, representing 0.5.
 */
static uint32_t bbr_ecn_reprobe_gain=BBR_UNIT * 1 / 2;

/* Estimate bw probing has gone too far if loss rate exceeds this level. */
static uint32_t bbr_loss_thresh = BBR_UNIT * 2 / 100;  /* 2% loss */

/* Exit STARTUP if number of loss marking events in a Recovery round is >= N,
 * and loss rate is higher than bbr_loss_thresh.
 * Disabled if 0. Max allowed value is 15 (0xF).
 */
static uint32_t bbr_full_loss_cnt = 8;

/* Exit STARTUP if number of round trips with ECN mark rate above ecn_thresh
 * meets this count. Max allowed value is 3.
 */
static uint32_t bbr_full_ecn_cnt = 2;

/* Fraction of unutilized headroom to try to leave in path upon high loss. */
static uint32_t bbr_inflight_headroom = BBR_UNIT * 15 / 100;

/* Multiplier to get target inflight (as multiple of BDP) for PROBE_UP phase.
 * Default is 1.25x, as in BBR v1. Max allowed is 511.
 */
static uint32_t bbr_bw_probe_pif_gain = BBR_UNIT * 5 / 4;

/* Multiplier to get Reno-style probe epoch duration as: k * BDP round trips.
 * If zero, disables this BBR v2 Reno-style BDP-scaled coexistence mechanism.
 * Max allowed is 511.
 */
static uint32_t bbr_bw_probe_reno_gain = BBR_UNIT;

/* Max number of packet-timed rounds to wait before probing for bandwidth.  If
 * we want to tolerate 1% random loss per round, and not have this cut our
 * inflight too much, we must probe for bw periodically on roughly this scale.
 * If low, limits Reno/CUBIC coexistence; if high, limits loss tolerance.
 * We aim to be fair with Reno/CUBIC up to a BDP of at least:
 *  BDP = 25Mbps * .030sec /(1514bytes) = 61.9 packets
 */
static uint32_t bbr_bw_probe_max_rounds = 63;

/* Max amount of randomness to inject in round counting for Reno-coexistence.
 * Max value is 15.
 */
static uint32_t bbr_bw_probe_rand_rounds = 2;

/* Use BBR-native probe time scale starting at this many usec.
 * We aim to be fair with Reno/CUBIC up to an inter-loss time epoch of at least:
 *  BDP*RTT = 25Mbps * .030sec /(1514bytes) * 0.030sec = 1.9 secs
 */
static uint32_t bbr_bw_probe_base_us = 2 * USEC_PER_SEC;  /* 2 secs */

/* Use BBR-native probes spread over this many usec: */
static uint32_t bbr_bw_probe_rand_us = 1 * USEC_PER_SEC;  /* 1 secs */

/* Undo the model changes made in loss recovery if recovery was spurious? */
static bool bbr_undo = true;

/* Use fast path if app-limited, no loss/ECN, and target cwnd was reached? */
static bool bbr_fast_path = true;	/* default: enabled */

/* Use fast ack mode ? */
//static int bbr_fast_ack_mode = 1;	/* default: rwnd check off */

/* How much to additively increase inflight_hi when entering REFILL? */
static uint32_t bbr_refill_add_inc=1;		/* default: disabled */

void TcpBbr2::bbr2_init(Ptr<TcpSocketState> tcb){
    struct bbr_t *bbr=&m_bbr;
    memset(bbr,0,sizeof(*bbr));
    bbr_init(tcb);

    /* BBR v2 parameters: */
    bbr->params.beta =std::min<uint32_t>(0xFFU, bbr_beta);
    bbr->params.ecn_alpha_gain =std::min<uint32_t>(0xFFU, bbr_ecn_alpha_gain);
    bbr->params.ecn_alpha_init =std::min<uint32_t>(BBR_UNIT, bbr_ecn_alpha_init);
    bbr->params.ecn_factor =std::min<uint32_t>(0xFFU, bbr_ecn_factor);
    bbr->params.ecn_thresh =std::min<uint32_t>(0xFFU, bbr_ecn_thresh);
    bbr->params.ecn_max_rtt_us =std::min<uint32_t>(0x7ffffU, bbr_ecn_max_rtt_us);
    bbr->params.ecn_reprobe_gain =std::min<uint32_t>(0x1FF, bbr_ecn_reprobe_gain);
    bbr->params.loss_thresh =std::min<uint32_t>(0xFFU, bbr_loss_thresh);
    bbr->params.full_loss_cnt =std::min<uint32_t>(0xFU, bbr_full_loss_cnt);
    bbr->params.full_ecn_cnt =std::min<uint32_t>(0x3U, bbr_full_ecn_cnt);
    bbr->params.inflight_headroom =
        std::min<uint32_t>(0xFFU, bbr_inflight_headroom);
    bbr->params.bw_probe_pif_gain =
        std::min<uint32_t>(0x1FFU, bbr_bw_probe_pif_gain);
    bbr->params.bw_probe_reno_gain =
        std::min<uint32_t>(0x1FFU, bbr_bw_probe_reno_gain);
    bbr->params.bw_probe_max_rounds =
        std::min<uint32_t>(0xFFU, bbr_bw_probe_max_rounds);
    bbr->params.bw_probe_rand_rounds =
        std::min<uint32_t>(0xFU, bbr_bw_probe_rand_rounds);
    bbr->params.bw_probe_base_us =
        std::min<uint32_t>((1 << 26) - 1, bbr_bw_probe_base_us);
    bbr->params.bw_probe_rand_us =
        std::min<uint32_t>((1 << 26) - 1, bbr_bw_probe_rand_us);
    bbr->params.undo = bbr_undo;
    bbr->params.fast_path = bbr_fast_path ? 1 : 0;
    bbr->params.refill_add_inc =std::min<uint32_t>(0x3U, bbr_refill_add_inc);

    /* BBR v2 state: */
    bbr->initialized = 1;
    /* Start sampling ECN mark rate after first full flight is ACKed: */
    bbr->loss_round_delivered =tcb->m_segmentSize;
    bbr->loss_round_start = 0;
    bbr->undo_bw_lo = 0;
    bbr->undo_inflight_lo = 0;
    bbr->undo_inflight_hi = 0;
    bbr->loss_events_in_round = 0;
    bbr->startup_ecn_rounds = 0;
    bbr2_reset_congestion_signals();
    bbr->bw_lo = ~0U;
    bbr->bw_hi[0] = 0;
    bbr->bw_hi[1] = 0;
    bbr->inflight_lo = ~0U;
    bbr->inflight_hi = ~0U;
    bbr->bw_probe_up_cnt = ~0U;
    bbr->bw_probe_up_acks = 0;
    bbr->bw_probe_up_rounds = 0;
    bbr->probe_wait_us = 0;
    bbr->stopped_risky_probe = 0;
    bbr->ack_phase = BBR_ACKS_INIT;
    bbr->rounds_since_probe = 0;
    bbr->bw_probe_samples = 0;
    bbr->prev_probe_too_high = 0;
    bbr->ecn_eligible = 0;
    bbr->ecn_alpha = bbr->params.ecn_alpha_init;
    bbr->alpha_last_delivered_bytes = 0;
    bbr->alpha_last_delivered_ce_bytes = 0;
    
    //TODO
    //tp->fast_ack_mode = min_t(u32, 0x2U, bbr_fast_ack_mode);
}

uint32_t TcpBbr2::MockRandomU32Max(uint32_t ep_ro){
    uint32_t seed=m_uv->GetInteger(0,std::numeric_limits<uint32_t>::max());
    uint64_t v=(((uint64_t) seed* ep_ro) >> 32);
    return v;
}
}
