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
#include <string>
#include <stdint.h>
#include "ns3/inet-socket-address.h"
namespace ns3{
class TcpUtils{
public:
    static std::string ConvertIpString(uint32_t ip);
};
}
