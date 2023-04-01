/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2021 Northeastern University, China
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: SongyangZhang <sonyang.chang@foxmail.com>
 * URL: https://github.com/SoonyangZhang/ns3-tcp-bbr
*/
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
