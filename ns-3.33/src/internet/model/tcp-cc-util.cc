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
#include <errno.h>   // for errno and strerror_r
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> //for sockaddr_in
#include <arpa/inet.h>  //inet_addr  
#include <netinet/tcp.h> //TCP_NODELAY
#include "tcp-cc-util.h"
#include "ns3/log.h"
#include "tcp-byte-codec.h"
namespace ns3{
NS_LOG_COMPONENT_DEFINE("tcp-cc-util");
const char *g_rl_server_ip="127.0.0.1";
uint16_t g_rl_server_port=2233;
void rl_server_ip_set(const char*ip){
    g_rl_server_ip=ip;
}
void rl_server_port_set(uint16_t port){
    g_rl_server_port=port;
}
int rl_new_conn(){
    int fd=-1;
    struct sockaddr_in servaddr;
    // assign IP, PORT 
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = inet_addr(g_rl_server_ip); 
    servaddr.sin_port = htons(g_rl_server_port);
    int flag = 1;
    if ((fd= socket(AF_INET, SOCK_STREAM,0)) < 0){
        NS_LOG_ERROR("Could not create socket");
        return fd;
    }
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
    if(connect(fd,(struct sockaddr *)&servaddr,sizeof(servaddr)) != 0){
        NS_LOG_ERROR("connection with the server failed");
        close(fd);
        fd=-1;
        return fd;
    }
    return fd;
}
namespace{
    const int kBufferSize=1500;
}
void tcp_agent_fun_test(){
    int bytes=0;
    uint32_t a=UINT32_MAX;
    char buffer[kBufferSize]={0};
    DataWriter w(buffer,kBufferSize);
    w.WriteVarInt(a);
    bytes=w.length();
    uint64_t result=0;
    DataReader r(buffer,bytes);
    r.ReadVarInt(&result);
    if(result==a){
        NS_LOG_INFO(bytes<<" equal value");
    }
}
}
