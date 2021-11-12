#pragma once
#include <stdint.h>
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-socket-state.h"
namespace ns3{
class TcpRenoAgent:public TcpCongestionOps{
public:
    /**
    * \brief Get the type ID.
    * \return the object TypeId
    */
    static TypeId GetTypeId (void);
    
    TcpRenoAgent ();
    
    /**
    * \brief Copy constructor.
    * \param sock object to copy.
    */
    TcpRenoAgent (const TcpRenoAgent& sock);
    
    ~TcpRenoAgent ();
    
    std::string GetName () const;
    
    virtual void IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked);
    virtual uint32_t GetSsThresh (Ptr<const TcpSocketState> tcb,uint32_t bytesInFlight);
    virtual Ptr<TcpCongestionOps> Fork ();
private:
    uint32_t ReportState(Ptr<const TcpSocketState> tcb,uint32_t segmentsAcked,uint32_t bytesInFlight,bool increase);
    uint32_t m_uuid=0;
    int m_fd=-1;
};
}