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
#include <stdint.h>
#include <type_traits>
namespace ns3{
//SingletonEnv stolen from leveldb
template <typename EnvType>
class SingletonEnv {
 public:
  SingletonEnv() {
    static_assert(sizeof(env_storage_) >= sizeof(EnvType),
                  "env_storage_ will not fit the Env");
    static_assert(alignof(decltype(env_storage_)) >= alignof(EnvType),
                  "env_storage_ does not meet the Env's alignment needs");
    new (&env_storage_) EnvType();
  }
  ~SingletonEnv() = default;

  SingletonEnv(const SingletonEnv&) = delete;
  SingletonEnv& operator=(const SingletonEnv&) = delete;
  EnvType* Content() { return reinterpret_cast<EnvType*>(&env_storage_); }
 private:
  typename std::aligned_storage<sizeof(EnvType), alignof(EnvType)>::type
      env_storage_;
};
class TcpUuidManager{
public:
    static TcpUuidManager* Instance(){
        static SingletonEnv<TcpUuidManager> single;
        return single.Content();
    }
    void set_base(uint16_t base){m_base=base;}
    uint32_t id(){
        uint32_t ret=m_count++;
        ret+=((uint32_t(m_base))<<16);
        return ret;
    }
private:
    friend SingletonEnv<TcpUuidManager>;
    TcpUuidManager(){}
    uint16_t m_base=0;
    uint16_t m_count=0;
};
void rl_server_ip_set(const char*ip);
void rl_server_port_set(uint16_t port);
int rl_new_conn();
void tcp_agent_fun_test();
}