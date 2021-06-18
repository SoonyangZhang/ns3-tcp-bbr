#pragma once
#include <string>
#include "ns3/tcp-congestion-ops.h"
#include "ns3/windowed-filter.h"
#include "ns3/data-rate.h"
#include "ns3/tcp-socket-base.h"
/*Copa: Practical Delay-Based Congestion Control for the Internet
the implementation is refered from mvfst, not the same as in the origin paper
*/
namespace ns3{
#define TCP_COPA_DEGUG 1
class TcpBbrDebug;
class TcpCopa: public TcpCongestionOps{
public:
    /**
    * \brief Get the type ID.
    * \return the object TypeId
    */
    static TypeId GetTypeId (void);
    
    /**
    * \brief Constructor
    */
    TcpCopa ();
    /**
    * Copy constructor.
    * \param sock The socket to copy from.
    */
    TcpCopa (const TcpCopa &sock);
    ~TcpCopa();
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
    virtual Ptr<TcpCongestionOps> Fork ();
private:
    struct VelocityState {
        enum Direction {
            None,
            Up, // cwnd is increasing
            Down, // cwnd is decreasing
        };
        uint64_t velocity{1};
        Direction direction{None};
        // number of rtts direction has remained same
        uint64_t numTimesDirectionSame{0};
        // updated every srtt
        uint64_t lastRecordedCwndBytes {0};
        Time lastCwndRecordTime{Time(0)};
    };
    typedef WindowedFilter<Time,MinFilter<Time>,uint64_t,uint64_t> RTTFilter;
    void InitPacingRateFromRtt(Ptr<TcpSocketState> tcb,float gain);
    void SetPacingRate(Ptr<TcpSocketState> tcb,float gain);
    inline DataRate BandwidthEstimate(Ptr<TcpSocketState> tcb);
    void CheckAndUpdateDirection(Time event_time,Time srtt,uint32_t cwnd_bytes);
    void ChangeDirection(Time event_time, VelocityState::Direction new_direction,uint32_t cwnd_bytes);
    RTTFilter m_minRttFilter;
    RTTFilter m_standingRttFilter;
    bool m_useRttStanding {true};
    bool m_isSlowStart {true};
    /**
    * deltaParam determines how latency sensitive the algorithm is. Lower
    * means it will maximime throughput at expense of delay. Higher value means
    * it will minimize delay at expense of throughput.
    */
    double m_deltaParam {0.05};
    // time at which cwnd was last doubled during slow start
    Time m_lastCwndDoubleTime {Time(0)};
    VelocityState m_velocityState;
    uint32_t m_ackBytesRound {0};
#if (TCP_COPA_DEGUG)
    Ptr <TcpBbrDebug> m_debug;
#endif
};
}
