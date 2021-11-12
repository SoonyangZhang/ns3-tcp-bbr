#include <memory.h>
#include <limits>
#include <stdint.h>
#include "tcp-byte-codec.h"
namespace ns3{
namespace {
// We define an unsigned 16-bit floating point value, inspired by IEEE floats
// (http://en.wikipedia.org/wiki/Half_precision_floating-point_format),
// with 5-bit exponent (bias 1), 11-bit mantissa (effective 12 with hidden
// bit) and denormals, but without signs, transfinites or fractions. Wire format
// 16 bits (little-endian byte order) are split into exponent (high 5) and
// mantissa (low 11) and decoded as:
//   uint64_t value;
//   if (exponent == 0) value = mantissa;
//   else value = (mantissa | 1 << 11) << (exponent - 1)
const int kUFloat16ExponentBits = 5;
const int kUFloat16MaxExponent = (1 << kUFloat16ExponentBits) - 2;     // 30
const int kUFloat16MantissaBits = 16 - kUFloat16ExponentBits;          // 11
const int kUFloat16MantissaEffectiveBits = kUFloat16MantissaBits + 1;  // 12
const uint64_t kUFloat16MaxValue =  // 0x3FFC0000000
    ((UINT64_C(1) << kUFloat16MantissaEffectiveBits) - 1)
    << kUFloat16MaxExponent;
const uint64_t kVarInt62ErrorMask = UINT64_C(0xc000000000000000);
const uint64_t kVarInt62Mask8Bytes = UINT64_C(0x3fffffffc0000000);
const uint64_t kVarInt62Mask4Bytes = UINT64_C(0x000000003fffc000);
const uint64_t kVarInt62Mask2Bytes = UINT64_C(0x0000000000003fc0);
}
enum BaseVariableIntegerLength:uint8_t{
    BASE_VARIABLE_LENGTH_0=0,
    BASE_VARIABLE_LENGTH_1=1,
    BASE_VARIABLE_LENGTH_2=2,
    BASE_VARIABLE_LENGTH_4=4,
    BASE_VARIABLE_LENGTH_8=8,
};

DataReader::DataReader(const char* data, const size_t len)
:DataReader(data,len,basic::NETWORK_BYTE_ORDER){}
DataReader::DataReader(const char* data,const size_t len,basic::Endianness endianness)
:data_(data), len_(len), pos_(0), endianness_(endianness){}
bool DataReader::ReadUInt8(uint8_t *result){
    return ReadBytes(result, sizeof(uint8_t));
}
bool DataReader::ReadUInt16(uint16_t *result){
    if(!ReadBytes(result,sizeof(uint16_t))){
        return false;
    }
    if(endianness_ == basic::NETWORK_BYTE_ORDER){
        *result=basic::QuicheEndian::NetToHost16(*result);
    }
    return true;
}
bool DataReader::ReadUInt32(uint32_t *result){
    if(!ReadBytes(result,sizeof(uint32_t))){
        return false;
    }
    if(endianness_ == basic::NETWORK_BYTE_ORDER){
        *result=basic::QuicheEndian::NetToHost32(*result);
    }
    return true;
}
bool DataReader::ReadUInt64(uint64_t *result){
    if(!ReadBytes(result,sizeof(uint64_t))){
        return false;
    }
    if(endianness_ == basic::NETWORK_BYTE_ORDER){
        *result=basic::QuicheEndian::NetToHost64(*result);
    }
    return true;
}
bool DataReader::ReadBytesToUInt64(size_t num_bytes, uint64_t* result){
    *result = 0u;
    if (num_bytes > sizeof(*result)) {
        return false;
    }
    if (endianness_ ==basic::HOST_BYTE_ORDER) {
        return ReadBytes(result, num_bytes);
    }
    
    if (!ReadBytes(reinterpret_cast<char*>(result) + sizeof(*result) - num_bytes,
                num_bytes)) {
        return false;
    }
    *result = basic::QuicheEndian::NetToHost64(*result);
    return true;
}
bool DataReader::ReadVarInt62(uint64_t* result){
    //DCHECK_EQ(endianness(),basic::NETWORK_BYTE_ORDER);
    
    size_t remaining = BytesRemaining();
    const unsigned char* next =
        reinterpret_cast<const unsigned char*>(data() + pos());
    if (remaining != 0) {
    switch (*next & 0xc0) {
        case 0xc0:
        // Leading 0b11...... is 8 byte encoding
        if (remaining >= 8) {
            *result = (static_cast<uint64_t>((*(next)) & 0x3f) << 56) +
                    (static_cast<uint64_t>(*(next + 1)) << 48) +
                    (static_cast<uint64_t>(*(next + 2)) << 40) +
                    (static_cast<uint64_t>(*(next + 3)) << 32) +
                    (static_cast<uint64_t>(*(next + 4)) << 24) +
                    (static_cast<uint64_t>(*(next + 5)) << 16) +
                    (static_cast<uint64_t>(*(next + 6)) << 8) +
                    (static_cast<uint64_t>(*(next + 7)) << 0);
            AdvancePos(8);
            return true;
        }
        return false;
    
        case 0x80:
        // Leading 0b10...... is 4 byte encoding
        if (remaining >= 4) {
            *result = (((*(next)) & 0x3f) << 24) + (((*(next + 1)) << 16)) +
                    (((*(next + 2)) << 8)) + (((*(next + 3)) << 0));
            AdvancePos(4);
            return true;
        }
        return false;
    
        case 0x40:
        // Leading 0b01...... is 2 byte encoding
        if (remaining >= 2) {
            *result = (((*(next)) & 0x3f) << 8) + (*(next + 1));
            AdvancePos(2);
            return true;
        }
        return false;
    
        case 0x00:
        // Leading 0b00...... is 1 byte encoding
        *result = (*next) & 0x3f;
        AdvancePos(1);
        return true;
    }
    }
    return false;    
}
bool DataReader::ReadVarInt(uint64_t *result){
    size_t remaining = BytesRemaining();
    size_t length=0;
    bool decodable=false;
    for(size_t i=0;i<remaining;i++){
        length++;
        if((data_[pos_+i]&128)==0){
            decodable=true;
            break;
        }
        if(length>8){
            break;
        }
    }
    if((length>0&&length<=8)&&decodable){
        uint64_t remain=0;
        uint64_t remain_multi=1;
        for(size_t i=0;i<length;i++){
            remain+=(data_[pos_+i]&127)*remain_multi;
            remain_multi*=128;
        }
        *result=remain;        
        AdvancePos(length);
        return true;
    }
    return false;
}
bool DataReader::ReadBytes(void*result,uint32_t size){
  // Make sure that we have enough data to read.
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }

  // Read into result.
  memcpy(result, data_ + pos_, size);

  // Iterate.
  pos_ += size;

  return true;
}
bool DataReader::Seek(size_t size) {
  if (!CanRead(size)) {
    OnFailure();
    return false;
  }
  pos_ += size;
  return true;
}
bool DataReader::IsDoneReading() const {
  return len_ == pos_;
}

size_t DataReader::BytesRemaining() const {
  return len_ - pos_;
}

bool DataReader::TruncateRemaining(size_t truncation_length) {
  if (truncation_length > BytesRemaining()) {
    return false;
  }
  len_ = pos_ + truncation_length;
  return true;
}

bool DataReader::CanRead(size_t bytes) const {
  return bytes <= (len_ - pos_);
}

void DataReader::OnFailure() {
  // Set our iterator to the end of the buffer so that further reads fail
  // immediately.
  pos_ = len_;
}
uint8_t DataReader::PeekByte() const {
  if (pos_ >= len_) {
    return 0;
  }
  return data_[pos_];
}
void DataReader::AdvancePos(size_t amount) {
    //DCHECK_LE(pos_, std::numeric_limits<size_t>::max() - amount);
    //DCHECK_LE(pos_, len_ - amount);
    pos_ += amount;
}

DataWriter::DataWriter(char* buffer,size_t size):DataWriter(buffer,size,basic::NETWORK_BYTE_ORDER){}
DataWriter::DataWriter(char* buffer,  size_t size,basic::Endianness endianness)
:buffer_(buffer),capacity_(size),length_(0),endianness_(endianness){}
int DataWriter::GetVarInt62Len(uint64_t value) {
  if ((value & kVarInt62ErrorMask) != 0) {
    return BASE_VARIABLE_LENGTH_0;
  }
  if ((value & kVarInt62Mask8Bytes) != 0) {
    return BASE_VARIABLE_LENGTH_8;
  }
  if ((value & kVarInt62Mask4Bytes) != 0) {
    return BASE_VARIABLE_LENGTH_4;
  }
  if ((value & kVarInt62Mask2Bytes) != 0) {
    return BASE_VARIABLE_LENGTH_2;
  }
  return BASE_VARIABLE_LENGTH_1;
}
int DataWriter::GetVarIntLen(uint64_t number){
    int length=0;
    if(number<=UINT64_C(0x7f)){
        length=1;
    }else if(number<=UINT64_C(0x3fff)){
        length=2;
    }else if(number<=UINT64_C(0x1fffff)){
        length=3;
    }else if(number<=UINT64_C(0xfffffff)){
        length=4;
    }else if(number<=UINT64_C(0x7ffffffff)){
        length=5;
    }else if(number<=UINT64_C(0x3ffffffffff)){
        length=6;
    }else if(number<=UINT64_C(0x1ffffffffffff)){
        length=7;
    }else if(number<=UINT64_C(0xffffffffffffff)){
        length=8;
    }
    return length;
}
bool DataWriter::WriteUInt8(uint8_t value){
    return WriteBytes(&value,sizeof(uint8_t));
}
bool DataWriter::WriteUInt16(uint16_t value){
    if(endianness_ == basic::NETWORK_BYTE_ORDER){
        value=basic::QuicheEndian::HostToNet16(value);
    }
    return WriteBytes(&value,sizeof(uint16_t));
}
bool DataWriter::WriteUInt32(uint32_t value){
    if(endianness_ == basic::NETWORK_BYTE_ORDER){
        value=basic::QuicheEndian::HostToNet32(value);
    }
    return WriteBytes(&value,sizeof(uint32_t));
}
bool DataWriter::WriteUInt64(uint64_t value){
    if(endianness_ == basic::NETWORK_BYTE_ORDER){
        value=basic::QuicheEndian::HostToNet64(value);
    }
    return WriteBytes(&value,sizeof(uint64_t));
}
bool DataWriter::WriteBytesToUInt64(size_t num_bytes, uint64_t value){
    if (num_bytes > sizeof(value)) {
        return false;
    }
    if (endianness_ ==basic::HOST_BYTE_ORDER) {
        return WriteBytes(&value, num_bytes);
    }
    
    value =basic::QuicheEndian::HostToNet64(value);
    return WriteBytes(reinterpret_cast<char*>(&value) + sizeof(value) - num_bytes,
                    num_bytes);
}
bool DataWriter::WriteVarInt62(uint64_t value){
  //DCHECK_EQ(endianness(), basic::NETWORK_BYTE_ORDER);

  size_t remaining_bytes = remaining();
  char* next = buffer() + length();

  if ((value & kVarInt62ErrorMask) == 0) {
    // We know the high 2 bits are 0 so |value| is legal.
    // We can do the encoding.
    if ((value & kVarInt62Mask8Bytes) != 0) {
      // Someplace in the high-4 bytes is a 1-bit. Do an 8-byte
      // encoding.
      if (remaining_bytes >= 8) {
        *(next + 0) = ((value >> 56) & 0x3f) + 0xc0;
        *(next + 1) = (value >> 48) & 0xff;
        *(next + 2) = (value >> 40) & 0xff;
        *(next + 3) = (value >> 32) & 0xff;
        *(next + 4) = (value >> 24) & 0xff;
        *(next + 5) = (value >> 16) & 0xff;
        *(next + 6) = (value >> 8) & 0xff;
        *(next + 7) = value & 0xff;
        IncreaseLength(8);
        return true;
      }
      return false;
    }
    // The high-order-4 bytes are all 0, check for a 1, 2, or 4-byte
    // encoding
    if ((value & kVarInt62Mask4Bytes) != 0) {
      // The encoding will not fit into 2 bytes, Do a 4-byte
      // encoding.
      if (remaining_bytes >= 4) {
        *(next + 0) = ((value >> 24) & 0x3f) + 0x80;
        *(next + 1) = (value >> 16) & 0xff;
        *(next + 2) = (value >> 8) & 0xff;
        *(next + 3) = value & 0xff;
        IncreaseLength(4);
        return true;
      }
      return false;
    }
    // The high-order bits are all 0. Check to see if the number
    // can be encoded as one or two bytes. One byte encoding has
    // only 6 significant bits (bits 0xffffffff ffffffc0 are all 0).
    // Two byte encoding has more than 6, but 14 or less significant
    // bits (bits 0xffffffff ffffc000 are 0 and 0x00000000 00003fc0
    // are not 0)
    if ((value & kVarInt62Mask2Bytes) != 0) {
      // Do 2-byte encoding
      if (remaining_bytes >= 2) {
        *(next + 0) = ((value >> 8) & 0x3f) + 0x40;
        *(next + 1) = (value)&0xff;
        IncreaseLength(2);
        return true;
      }
      return false;
    }
    if (remaining_bytes >= 1) {
      // Do 1-byte encoding
      *next = (value & 0x3f);
      IncreaseLength(1);
      return true;
    }
    return false;
  }
  // Can not encode, high 2 bits not 0
  return false;    
}
bool DataWriter::WriteVarInt(uint64_t value){
    size_t remaining_bytes = remaining();
    size_t need=GetVarIntLen(value);
    if(0==need||need>remaining_bytes){
        return false;
    }
    uint8_t first=0;
    uint64_t next=value;
    do{
        first=next%128;
        next=next/128;
        uint8_t byte=first;
        if(next>0){
            byte|=128;
        }
        WriteUInt8(byte);
    }while(next>0);
    return true;
}
bool DataWriter::WriteBytes(const void* data, size_t data_len){
  char* dest = BeginWrite(data_len);
  if (!dest) {
    return false;
  }
  memcpy(dest, data, data_len);
  length_ += data_len;
  return true;
}
bool DataWriter::Seek(size_t length){
  if (!BeginWrite(length)) {
    return false;
  }
  length_ += length;
  return true;    
}
char* DataWriter::BeginWrite(size_t length){
  if (length_ > capacity_) {
    return nullptr;
  }

  if (capacity_ - length_ < length) {
    return nullptr;
  }

#ifdef ARCH_CPU_64_BITS
  //DCHECK_LE(length, std::numeric_limits<uint32_t>::max());
#endif

  return buffer_ + length_;
}
void DataWriter::IncreaseLength(size_t delta) {
    //DCHECK_LE(length_, std::numeric_limits<size_t>::max() - delta);
    //DCHECK_LE(length_, capacity_ - delta);
    length_ += delta;
}
}