#ifndef TCPBBR2_H
#define TCPBBR2_H
#include <string>
#include "ns3/tcp-congestion-ops.h"
#include "ns3/traced-value.h"
#include "ns3/data-rate.h"
#include "ns3/random-variable-stream.h"
namespace ns3{
#define TCP_BBR2_DEGUG 1
#define FUNC_INLINE
class TcpBbrDebug;
class TcpBbr2: public TcpCongestionOps{
public:
/* BBR congestion control block */
struct bbr_t{
    uint32_t    min_rtt_us;        /* min RTT in min_rtt_win_sec window */
    Time        min_rtt_stamp {Time(0)};	        /* timestamp of min_rtt_us */
    Time        probe_rtt_done_stamp {Time(0)};   /* end time for BBR_PROBE_RTT mode */
    uint32_t    probe_rtt_min_us;	/* min RTT in bbr_probe_rtt_win_ms window */
    Time        probe_rtt_min_stamp {Time(0)};	/* timestamp of probe_rtt_min_us*/
    uint64_t    next_rtt_delivered {0}; /* scb->tx.delivered at end of round */
    uint32_t    prior_rcv_nxt;	/* tp->rcv_nxt when CE state last changed */
    Time        cycle_mstamp {Time(0)};	     /* time of this cycle phase start */
    uint32_t    mode:3,		     /* current bbr_mode in state machine */
                prev_ca_state:3,     /* CA state on previous ACK */
                packet_conservation:1,  /* use packet conservation? */
                round_start:1,	     /* start of packet-timed tx->ack round? */
                ce_state:1,          /* If most recent data has CE bit set */
                bw_probe_up_rounds:5,   /* cwnd-limited rounds in PROBE_UP */
                try_fast_path:1, 	/* can we take fast path? */
                unused2:11,
                idle_restart:1,	     /* restarting after idle? */
                probe_rtt_round_done:1,  /* a BBR_PROBE_RTT round at 4 pkts? */
                cycle_idx:3,	/* current index in pacing_gain cycle array */
                has_seen_rtt:1;	     /* have we seen an RTT sample yet? */
    uint32_t    pacing_gain:11,	/* current gain for setting pacing rate */
                cwnd_gain:11,	/* current gain for setting cwnd */
                full_bw_reached:1,   /* reached full bw in Startup? */
                full_bw_cnt:2,	/* number of rounds without large bw gains */
                init_cwnd:7;	/* initial cwnd */
    uint32_t    prior_cwnd;	/* prior cwnd upon entering loss recovery */
    uint32_t    full_bw;	/* recent bw, to estimate if pipe is full */
    
    /* For tracking ACK aggregation: */
    Time        ack_epoch_mstamp {Time(0)};	/* start of ACK sampling epoch */
    uint16_t    extra_acked[2];		/* max excess data ACKed in epoch */
    uint32_t    ack_epoch_acked:20,	/* packets (S)ACKed in sampling epoch */
                extra_acked_win_rtts:5,	/* age of extra_acked, in round trips */
                extra_acked_win_idx:1,	/* current index in extra_acked array */
                /* BBR v2 state: */
                unused1:2,
                startup_ecn_rounds:2,	/* consecutive hi ECN STARTUP rounds */
                loss_in_cycle:1,	/* packet loss in this cycle? */
                ecn_in_cycle:1;		/* ECN in this cycle? */
    uint64_t    loss_round_delivered; /* scb->tx.delivered ending loss round */
    uint32_t    undo_bw_lo;	     /* bw_lo before latest losses */
    uint32_t    undo_inflight_lo;    /* inflight_lo before latest losses */
    uint32_t    undo_inflight_hi;    /* inflight_hi before latest losses */
    uint32_t    bw_latest;	 /* max delivered bw in last round trip */
    uint32_t    bw_lo;		 /* lower bound on sending bandwidth */
    uint32_t    bw_hi[2];	 /* upper bound of sending bandwidth range*/
    uint32_t    inflight_latest; /* max delivered data in last round trip */
    uint32_t    inflight_lo;	 /* lower bound of inflight data range */
    uint32_t    inflight_hi;	 /* upper bound of inflight data range */
    uint32_t    bw_probe_up_cnt; /* packets delivered per inflight_hi incr */
    uint32_t    bw_probe_up_acks;  /* packets (S)ACKed since inflight_hi incr */
    uint32_t    probe_wait_us;	 /* PROBE_DOWN until next clock-driven probe */
    uint32_t    ecn_eligible:1,	/* sender can use ECN (RTT, handshake)? */
                ecn_alpha:9,	/* EWMA delivered_ce/delivered; 0..256 */
                bw_probe_samples:1,    /* rate samples reflect bw probing? */
                prev_probe_too_high:1, /* did last PROBE_UP go too high? */
                stopped_risky_probe:1, /* last PROBE_UP stopped due to risk? */
                rounds_since_probe:8,  /* packet-timed rounds since probed bw */
                loss_round_start:1,    /* loss_round_delivered round trip? */
                loss_in_round:1,       /* loss marked in this round trip? */
                ecn_in_round:1,	       /* ECN marked in this round trip? */
                ack_phase:3,	       /* bbr_ack_phase: meaning of ACKs */
                loss_events_in_round:4,/* losses in STARTUP round */
                initialized:1;	       /* has bbr_init() been called? */
    uint64_t    alpha_last_delivered_bytes;	 /* tp->delivered    at alpha update */
    uint64_t    alpha_last_delivered_ce_bytes; /* tp->delivered_ce at alpha update */
    
    /* Params configurable using setsockopt. Refer to correspoding
    * module param for detailed description of params.
    */
    struct bbr_params {
        uint32_t    high_gain:11,		/* max allowed value: 2047 */
                    drain_gain:10,		/* max allowed value: 1023 */
                    cwnd_gain:11;		/* max allowed value: 2047 */
        uint32_t    cwnd_min_target:4,	/* max allowed value: 15 */
                    min_rtt_win_sec:5,	/* max allowed value: 31 */
                    probe_rtt_mode_ms:9,	/* max allowed value: 511 */
                    full_bw_cnt:3,		/* max allowed value: 7 */
                    cwnd_tso_budget:1,	/* allowed values: {0, 1} */
                    unused3:6,
                    drain_to_target:1,	/* boolean */
                    precise_ece_ack:1,	/* boolean */
                    extra_acked_in_startup:1, /* allowed values: {0, 1} */
                    fast_path:1;		/* boolean */
        uint32_t    full_bw_thresh:10,	/* max allowed value: 1023 */
                    startup_cwnd_gain:11,	/* max allowed value: 2047 */
                    bw_probe_pif_gain:9,	/* max allowed value: 511 */
                    usage_based_cwnd:1, 	/* boolean */
                    unused2:1;
        uint16_t    probe_rtt_win_ms:14,	/* max allowed value: 16383 */
                    refill_add_inc:2;	/* max allowed value: 3 */
        uint16_t    extra_acked_gain:11,	/* max allowed value: 2047 */
                    extra_acked_win_rtts:5; /* max allowed value: 31*/
        uint16_t    pacing_gain[8]; /* max allowed value: 1023 */
        /* Mostly BBR v2 parameters below here: */
        uint32_t    ecn_alpha_gain:8,	/* max allowed value: 255 */
                    ecn_factor:8,		/* max allowed value: 255 */
                    ecn_thresh:8,		/* max allowed value: 255 */
                    beta:8;			/* max allowed value: 255 */
        uint32_t    ecn_max_rtt_us:19,	/* max allowed value: 524287 */
                    bw_probe_reno_gain:9,	/* max allowed value: 511 */
                    full_loss_cnt:4;	/* max allowed value: 15 */
        uint32_t    probe_rtt_cwnd_gain:8,	/* max allowed value: 255 */
                    inflight_headroom:8,	/* max allowed value: 255 */
                    loss_thresh:8,		/* max allowed value: 255 */
                    bw_probe_max_rounds:8;	/* max allowed value: 255 */
        uint32_t    bw_probe_rand_rounds:4, /* max allowed value: 15 */
                    bw_probe_base_us:26,	/* usecs: 0..2^26-1 (67 secs) */
                    full_ecn_cnt:2;		/* max allowed value: 3 */
        uint32_t    bw_probe_rand_us:26,	/* usecs: 0..2^26-1 (67 secs) */
                    undo:1,			/* boolean */
                    tso_rtt_shift:4,	/* max allowed value: 15 */
                    unused5:1;
        uint32_t    ecn_reprobe_gain:9,	/* max allowed value: 511 */
                    unused1:14,
                    ecn_alpha_init:9;	/* max allowed value: 256 */
    } params;
    struct{
        uint32_t    snd_isn; /* Initial sequence number */
        uint32_t    rs_bw; 	 /* last valid rate sample bw */
        uint32_t    target_cwnd; /* target cwnd, based on BDP */
        uint8_t     undo:1,  /* Undo even happened but not yet logged */
                    unused:7;
        char        event;	 /* single-letter event debug codes */
        uint16_t    unused2;
    }debug;
};
struct bbr_context{
    uint32_t sample_bw;
    uint32_t target_cwnd;
    uint32_t log:1;
};
    /**
    * \brief Get the type ID.
    * \return the object TypeId
    */
    static TypeId GetTypeId (void);
    
    /**
    * \brief Constructor
    */
    TcpBbr2 ();
    /**
    * Copy constructor.
    * \param sock The socket to copy from.
    */
    TcpBbr2 (const TcpBbr2 &sock);
    ~TcpBbr2();
    static std::string PhaseToString(uint8_t mode,uint8_t cycle_idx);
    
    virtual std::string GetName () const;
    virtual void Init (Ptr<TcpSocketState> tcb);
    virtual uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight);
    virtual void IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);
    virtual void PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time& rtt);
    virtual void CongestionStateSet (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCongState_t newState);
    virtual void CwndEvent (Ptr<TcpSocketState> tcb,const TcpSocketState::TcpCAEvent_t event);
    virtual bool HasCongControl () const;
    virtual void CongControl (Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs);
    virtual void MarkSkbLost(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                         const TcpTxItem *skb);
    virtual Ptr<TcpCongestionOps> Fork ();
    /**
    * Assign a fixed random variable stream number to the random variables
    * used by this model.  Return the number of streams (possibly zero) that
    * have been assigned.
    *
    * \param stream first stream index to use
    * \return the number of stream indices assigned by this model
    */
    virtual void AssignStreams (int64_t stream);
protected:
    FUNC_INLINE bool bbr_full_bw_reached();
    FUNC_INLINE uint32_t bbr_max_bw();
    FUNC_INLINE uint32_t bbr_bw();
    FUNC_INLINE uint16_t bbr_extra_acked();
    void bbr_init_pacing_rate_from_rtt(Ptr<TcpSocketState> tcb);
    FUNC_INLINE void bbr_set_pacing_rate(Ptr<TcpSocketState> tcb, uint32_t bw, int gain);
    /**
    * \param send_cwnd the size of congestion window in packets;
    */
    FUNC_INLINE void bbr_save_cwnd(uint32_t send_cwnd);
    FUNC_INLINE uint32_t bbr_bdp(uint32_t bw, int gain);
    FUNC_INLINE uint32_t bbr_quantization_budget(uint32_t cwnd);
    FUNC_INLINE uint32_t bbr_inflight(uint32_t bw, int gain);
    FUNC_INLINE uint32_t bbr_packets_in_net_at_edt(uint32_t inflight_now);
    FUNC_INLINE uint32_t bbr_ack_aggregation_cwnd();
    FUNC_INLINE uint32_t bbr_probe_rtt_cwnd();
    void bbr_set_cwnd(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                const TcpRateOps::TcpRateSample &rs,uint32_t bw, int gain, 
                uint32_t cwnd,struct bbr_context *ctx);
    void bbr_update_round_start(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,const TcpRateOps::TcpRateSample &rs);
    void bbr_calculate_bw_sample(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateSample &rs,struct bbr_context *ctx);
    void bbr_update_ack_aggregation(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                const TcpRateOps::TcpRateSample &rs);
    FUNC_INLINE void bbr_check_full_bw_reached(const TcpRateOps::TcpRateSample &rs);
    FUNC_INLINE bool bbr_check_drain(Ptr<TcpSocketState> tcb);
    FUNC_INLINE void bbr_check_probe_rtt_done(Ptr<TcpSocketState> tcb,uint64_t delivered_bytes);
    void bbr_update_min_rtt(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                        const TcpRateOps::TcpRateSample &rs);
    void bbr_update_gains();
    void bbr_init(Ptr<TcpSocketState> tcb);
    FUNC_INLINE void bbr2_take_bw_hi_sample(uint32_t bw);
    FUNC_INLINE void bbr2_advance_bw_hi_filter();
    FUNC_INLINE uint32_t bbr2_target_inflight(uint32_t snd_cwnd);
    FUNC_INLINE bool bbr2_is_probing_bandwidth();
    FUNC_INLINE bool bbr2_has_elapsed_in_phase(uint32_t interval_us);
    FUNC_INLINE void bbr2_handle_queue_too_high_in_startup();
    FUNC_INLINE void bbr2_check_ecn_too_high_in_startup(uint32_t ce_ratio);
    void bbr2_update_ecn_alpha(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc);
    void bbr2_raise_inflight_hi_slope(uint32_t snd_cwnd);
    void bbr2_probe_inflight_hi_upward(uint32_t snd_cwnd,uint32_t acked_sacked);
    bool bbr2_is_inflight_too_high(const TcpRateOps::TcpRateSample &rs);
    uint32_t bbr2_inflight_hi_from_lost_skb(const TcpRateOps::TcpRateSample &rs,uint32_t packet_bytes,uint32_t mss);
    FUNC_INLINE uint32_t bbr2_inflight_with_headroom();
    void bbr2_bound_cwnd_for_inflight_model(Ptr<TcpSocketState> tcb);
    void bbr2_adapt_lower_bounds(uint32_t snd_cwnd);
    FUNC_INLINE void bbr2_reset_lower_bounds();
    FUNC_INLINE void bbr2_reset_congestion_signals();
    void bbr2_update_congestion_signals(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                                    const TcpRateOps::TcpRateSample &rs,struct bbr_context *ctx);
    FUNC_INLINE bool bbr2_is_reno_coexistence_probe_time(uint32_t snd_cwnd);
    FUNC_INLINE void bbr2_pick_probe_wait();
    FUNC_INLINE void bbr2_set_cycle_idx(int cycle_idx);
    FUNC_INLINE void bbr2_start_bw_probe_refill(uint32_t bw_probe_up_rounds,uint64_t delivered_bytes);
    FUNC_INLINE void bbr2_start_bw_probe_up(uint64_t delivered_bytes,uint32_t snd_cwnd);
    FUNC_INLINE void bbr2_start_bw_probe_down(uint64_t delivered_bytes);
    FUNC_INLINE void bbr2_start_bw_probe_cruise();
    void bbr2_handle_inflight_too_high(bool is_app_limited,uint32_t snd_cwnd,
                                    uint32_t tx_in_flight,uint64_t delivered_bytes);
    bool bbr2_adapt_upper_bounds(const TcpRateOps::TcpRateSample &rs,uint32_t snd_cwnd,
                            uint32_t tx_in_flight,uint32_t acked_sacked,uint64_t delivered_bytes);
    bool bbr2_check_time_to_probe_bw(Ptr<TcpSocketState> tcb,uint32_t snd_cwnd,uint64_t delivered_bytes);
    FUNC_INLINE bool bbr2_check_time_to_cruise(uint32_t inflight, uint32_t bw);
    void bbr2_update_cycle_phase(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                                    const TcpRateOps::TcpRateSample &rs);
    void bbr2_exit_probe_rtt(uint64_t delivered_bytes);
    void bbr2_check_loss_too_high_in_startup(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateSample &rs);
    void bbr2_check_drain(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc);
    void bbr2_update_model(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                        const TcpRateOps::TcpRateSample &rs,struct bbr_context *ctx);
    bool bbr2_fast_path(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
       const TcpRateOps::TcpRateSample &rs,bool *update_model,struct bbr_context *ctx);
    void bbr2_init(Ptr<TcpSocketState> tcb);
private:
    FUNC_INLINE uint32_t MockRandomU32Max(uint32_t ep_ro); //pseudo-random number in interval [0, ep_ro)
    bool m_enableEcn {false};
    bool m_eceFlag{false};
    struct bbr_t m_bbr;
    Ptr<UniformRandomVariable> m_uv{nullptr};
    #if TCP_BBR2_DEGUG
    Ptr <TcpBbrDebug> m_debug;
    #endif
};
}

#endif
