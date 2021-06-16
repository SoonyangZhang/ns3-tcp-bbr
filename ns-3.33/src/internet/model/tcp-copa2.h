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
#define TCP_COPA2_DEGUG 1
class TcpBbrDebug;
class TcpCopa2: public TcpCongestionOps{
public:
    /**
    * \brief Get the type ID.
    * \return the object TypeId
    */
    static TypeId GetTypeId (void);
    
    /**
    * \brief Constructor
    */
    TcpCopa2 ();
    /**
    * Copy constructor.
    * \param sock The socket to copy from.
    */
    TcpCopa2 (const TcpCopa2 &sock);
    ~TcpCopa2();
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
    
    typedef WindowedFilter<Time,MinFilter<Time>,uint64_t,uint64_t> RTTFilter;
    void InitPacingRateFromRtt(Ptr<TcpSocketState> tcb,float gain);
    void SetPacingRate(Ptr<TcpSocketState> tcb,float gain);
    void UpdateLossMode(Ptr<TcpSocketState> tcb,const TcpRateOps::TcpRateConnection &rc,
                            const TcpRateOps::TcpRateSample &rs);
    inline DataRate BandwidthEstimate(Ptr<TcpSocketState> tcb);
    RTTFilter m_minRttFilter;
    Time m_cycleStart {Time(0)};
    Time m_lastProbeRtt{Time(0)};
    uint32_t m_bytesAckedInCycle {0};
    uint32_t  m_alphaParam {10};
  // Loss rate we are willing to tolerate. Actual loss rate will be 2 *
  // lossTolaranceParam_ + alpha/BDP
    double m_lossToleranceParam {0.05};
    uint64_t m_lossRoundDelivered {0};
    uint32_t m_lostBytesCount {0} ; //!< Count all the lost bytes in connection;
    uint32_t m_priorLostBytes {0} ;//!< Record the lost bytes in last round;
    bool m_lossyMode {false};
    bool m_probeRtt {false};
#if (TCP_COPA2_DEGUG)
    Ptr <TcpBbrDebug> m_debug;
#endif
};
}
