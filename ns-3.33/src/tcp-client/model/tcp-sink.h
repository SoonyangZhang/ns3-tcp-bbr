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
#include "ns3/object.h"
#include "ns3/address.h"
#include "ns3/internet-module.h"
#include "ns3/tcp-tracer.h"
namespace ns3{
class TcpSink:public Object{
public:
    TcpSink(Ptr<Socket> socket,Address client_addr,Address server_addr,bool log_rate=false);
    virtual ~TcpSink();
    static void SetRateCountGap(Time gap);
protected:
    virtual void DoDispose (void);
    virtual void DoInitialize (void);
private:
    void HandleRead (Ptr<Socket> socket);
    void HandlePeerClose (Ptr<Socket> socket);
    void HandlePeerError (Ptr<Socket> socket);
    Ptr<Socket> m_socket;
    Ptr<TcpTracer> m_trace;
    Time m_lastCountRateTime=Time(0);
    uint64_t m_lastRxBytes=0;
    uint64_t m_rxBytes=0;
    int64_t  m_totalBytes=0;
};
}
