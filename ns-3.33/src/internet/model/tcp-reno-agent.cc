#include <algorithm>
#include <memory.h>
#include <unistd.h>
#include "tcp-reno-agent.h"
#include "tcp-cc-util.h"
#include "tcp-byte-codec.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
namespace ns3{
NS_LOG_COMPONENT_DEFINE ("TcpRenoAgent");
NS_OBJECT_ENSURE_REGISTERED (TcpRenoAgent);
namespace{
    const int kBufferSize=1500;
}
TypeId TcpRenoAgent::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpRenoAgent")
    .SetParent<TcpCongestionOps> ()
    .SetGroupName ("Internet")
    .AddConstructor<TcpRenoAgent> ()
  ;
  return tid;
}

TcpRenoAgent::TcpRenoAgent (void) : TcpCongestionOps ()
{
    NS_LOG_FUNCTION (this);
    TcpUuidManager *manager=TcpUuidManager::Instance();
    m_uuid=manager->id();
    m_fd=rl_new_conn();
    NS_ASSERT_MSG(m_fd>=0,m_uuid<<" can not conn to external server");
}

TcpRenoAgent::TcpRenoAgent (const TcpRenoAgent& sock):TcpCongestionOps (sock)
{
    NS_LOG_FUNCTION (this);
    TcpUuidManager *manager=TcpUuidManager::Instance();
    m_uuid=manager->id();
    m_fd=rl_new_conn();
    NS_ASSERT_MSG(m_fd>=0,m_uuid<<" can not conn to external server");
}

TcpRenoAgent::~TcpRenoAgent (void)
{
    if(m_fd>=0){
        close(m_fd);
        m_fd=-1;
    }
}
std::string TcpRenoAgent::GetName () const
{
    return "TcpRenoAgent";
}
void TcpRenoAgent::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked){
    uint32_t new_cwnd=ReportState(tcb,segmentsAcked,tcb->m_bytesInFlight.Get(),true);
    tcb->m_cWnd=new_cwnd;
}
uint32_t TcpRenoAgent::GetSsThresh (Ptr<const TcpSocketState> tcb,uint32_t bytesInFlight){
    uint32_t new_cwnd=ReportState(tcb,0,bytesInFlight,false);
    return new_cwnd;
}
Ptr<TcpCongestionOps> TcpRenoAgent::Fork (){
     return CopyObject<TcpRenoAgent> (this);
}
/*****
request format:
varint uint8_t    varint       varint  varint        varint           varint 
len     AIMD     ssThresh       cwnd   segmentsAcked bytesInFlight    segmentSize
response format:
varint varint
len  new_cwnd
*****/
uint32_t TcpRenoAgent::ReportState(Ptr<const TcpSocketState> tcb,uint32_t segmentsAcked,
                        uint32_t bytesInFlight, bool increase){
    uint64_t new_cwnd=0,sum=0;
    int n=0,bytes=0,offset=0;;
    bool success=false;
    char buffer[kBufferSize]={0};
    //request
    uint8_t aimd=increase;
    uint32_t ssThresh=tcb->m_ssThresh.Get();
    uint32_t cwnd=tcb->m_cWnd.Get();
    uint32_t segmentSize=tcb->m_segmentSize;
    DataWriter w(buffer,kBufferSize);
    sum=sizeof(aimd)+DataWriter::GetVarIntLen(ssThresh)+
                DataWriter::GetVarIntLen(cwnd)+DataWriter::GetVarIntLen(segmentsAcked)+
                DataWriter::GetVarIntLen(bytesInFlight)+DataWriter::GetVarIntLen(segmentSize);
    success=w.WriteVarInt(sum)&&w.WriteUInt8(aimd)&&
            w.WriteVarInt(ssThresh)&&w.WriteVarInt(cwnd)&&
            w.WriteVarInt(segmentsAcked)&&w.WriteVarInt(bytesInFlight)&&
            w.WriteVarInt(segmentSize);
    bytes=w.length();
    NS_ASSERT_MSG(m_fd>=0,m_uuid<<"bad fd");
    n=write(m_fd,buffer,bytes);
    NS_ASSERT_MSG(n==bytes,m_uuid<<"write error");
    //response
    memset(buffer,0,kBufferSize);
    sum=0;
    success=false;
    while(true){
        n=read(m_fd,buffer+offset,kBufferSize-offset);
        if(n>0){
            offset+=n;
            DataReader reader(buffer,offset);
            success=reader.ReadVarInt(&sum)&&reader.ReadVarInt(&new_cwnd);
            if(success){
                break;
            }
        }else{
            break;
        }
    }
    return new_cwnd;
}
}
