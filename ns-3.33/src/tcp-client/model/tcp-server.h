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
#pragma once
#include <vector>
#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/internet-module.h"
#include "ns3/tcp-sink.h"
namespace ns3{
class TcpServer:public Application{
public:
    TcpServer(Address local_addr);
    virtual ~TcpServer();
    Ptr<Socket> GetListeningSocket (void) const;
protected:
  virtual void DoDispose (void);
private:
    virtual void StartApplication (void);
    virtual void StopApplication (void);
    void HandleRead (Ptr<Socket> socket);
    void HandleAccept (Ptr<Socket> socket, const Address& from);
    void HandlePeerClose (Ptr<Socket> socket);
    void HandlePeerError (Ptr<Socket> socket);
    Address m_localAddr;
    Ptr<Socket> m_socket;
    std::vector<Ptr<TcpSink>> m_sinks;
};
}
