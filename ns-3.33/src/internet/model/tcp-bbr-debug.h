#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <stdint.h>
#include "ns3/object.h"
namespace ns3{
class TcpBbrDebug :public Object{
public:
    TcpBbrDebug(std::string prefix);
    virtual ~TcpBbrDebug();
    static void SetTraceFolder(const char *path);
    std::fstream &GetStram() {return m_stream;}
    uint32_t GetUuid() const {return m_uuid;}
private:
    void OpenFile(std::string prefix);
    void CloseFile();
    std::fstream    m_stream;
    uint32_t m_uuid {0};
};
}