#include "tcp-server.h"
namespace ns3{
NS_LOG_COMPONENT_DEFINE ("TcpServer");
TcpServer::TcpServer(Address local_addr):m_localAddr(local_addr){}
TcpServer::~TcpServer(){}
Ptr<Socket> TcpServer::GetListeningSocket (void) const{
    return m_socket;
}
void TcpServer::DoDispose (void){
    m_socket=0;
    m_sinks.clear();
    Application::DoDispose ();
}
void TcpServer::StartApplication (void){
    if(0==m_socket){
        m_socket=Socket::CreateSocket(GetNode(),TcpSocketFactory::GetTypeId ());
        if(-1==m_socket->Bind (m_localAddr)){
            NS_FATAL_ERROR("Failed to bind socket");
        }
        m_socket->Listen ();
        m_socket->ShutdownSend ();
    }
    m_socket->SetRecvCallback (MakeCallback (&TcpServer::HandleRead, this));
    m_socket->SetAcceptCallback (MakeNullCallback<bool, Ptr<Socket>, const Address &> (),
                                MakeCallback (&TcpServer::HandleAccept, this));
    m_socket->SetCloseCallbacks (MakeCallback (&TcpServer::HandlePeerClose, this),
                                MakeCallback (&TcpServer::HandlePeerError, this));
}
void TcpServer::StopApplication (void){}
void TcpServer::HandleRead (Ptr<Socket> socket){
    NS_LOG_FUNCTION(this << socket);
}
void TcpServer::HandleAccept (Ptr<Socket> socket, const Address& from){
    Ptr<TcpSink> sink=CreateObject<TcpSink>(socket,from,m_localAddr,true);
    m_sinks.push_back(sink);
}
void TcpServer::HandlePeerClose (Ptr<Socket> socket){}
void TcpServer::HandlePeerError (Ptr<Socket> socket){}
}
