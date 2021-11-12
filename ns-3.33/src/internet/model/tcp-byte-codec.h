#pragma once
#include <cstddef>
#include <string>
#include <stdint.h>
#include "net_endian.h"
namespace ns3{
class DataReader{
public:
    // Constructs a reader using NETWORK_BYTE_ORDER endianness.
    // Caller must provide an underlying buffer to work on.
    DataReader(const char* data, const size_t len);
    // Constructs a reader using the specified endianness.
    // Caller must provide an underlying buffer to work on.
    DataReader(const char* data,const size_t len,basic::Endianness endianness);
    DataReader(const DataReader&) = delete;
    DataReader& operator=(const DataReader&) = delete;
    ~DataReader(){}

    bool ReadUInt8(uint8_t* result);
    bool ReadUInt16(uint16_t* result);
    bool ReadUInt32(uint32_t* result);
    bool ReadUInt64(uint64_t* result);
  
    bool ReadBytesToUInt64(size_t num_bytes, uint64_t* result);
    
    bool ReadVarInt62(uint64_t* result);
    bool ReadVarInt(uint64_t *result);
    bool ReadBytes(void*result,uint32_t size);
    
    // Skips over |size| bytes from the buffer and forwards the internal iterator.
    // Returns true if there are at least |size| bytes remaining to read, false
    // otherwise.
    bool Seek(size_t size);
    
    // Returns true if the entirety of the underlying buffer has been read via
    // Read*() calls.
    bool IsDoneReading() const;
    
    // Returns the number of bytes remaining to be read.
    size_t BytesRemaining() const;
    
    // Truncates the reader down by reducing its internal length.
    // If called immediately after calling this, BytesRemaining will
    // return |truncation_length|. If truncation_length is less than the
    // current value of BytesRemaining, this does nothing and returns false.
    bool TruncateRemaining(size_t truncation_length);
    
    // Returns the next byte that to be read. Must not be called when there are no
    // bytes to be read.
    //
    // DOES NOT forward the internal iterator.
    uint8_t PeekByte() const;
protected:
    // Returns true if the underlying buffer has enough room to read the given
    // amount of bytes.
    bool CanRead(size_t bytes) const;
    
    // To be called when a read fails for any reason.
    void OnFailure();
    
    const char* data() const { return data_; }
    
    size_t pos() const { return pos_; }
    void AdvancePos(size_t amount);
    basic::Endianness endianness() const { return endianness_; }
private:
    const char* data_;
    
    // The length of the data buffer that we're reading from.
    size_t len_;
    
    // The location of the next read from our data buffer.
    size_t pos_;
    basic::Endianness endianness_;
};
class DataWriter{
public:
    DataWriter(char* buffer,size_t size);
    DataWriter(char* buffer,  size_t size,basic::Endianness endianness);
    DataWriter(const DataWriter&) = delete;
    DataWriter& operator=(const DataWriter&) = delete;
    ~DataWriter(){}
    static int GetVarInt62Len(uint64_t value);
    static int GetVarIntLen(uint64_t number);
    // Returns the size of the QuicheDataWriter's data.
    size_t length() const { return length_; }
    
    // Retrieves the buffer from the QuicheDataWriter without changing ownership.
    char* data() {return buffer_;}
    
    // Methods for adding to the payload.  These values are appended to the end
    // of the QuicheDataWriter payload.
    
    // Writes 8/16/32/64-bit unsigned integers.
    bool WriteUInt8(uint8_t value);
    bool WriteUInt16(uint16_t value);
    bool WriteUInt32(uint32_t value);
    bool WriteUInt64(uint64_t value);
    
    bool WriteBytesToUInt64(size_t num_bytes, uint64_t value);
    
    bool WriteVarInt62(uint64_t value);
    bool WriteVarInt(uint64_t value);
    bool WriteBytes(const void* data, size_t data_len);
    
    bool Seek(size_t length);
    
    size_t capacity() const { return capacity_; }
    
    size_t remaining() const { return capacity_ - length_; }
protected:
    char* BeginWrite(size_t length);
    basic::Endianness endianness() const { return endianness_; }
    
    char* buffer() const { return buffer_; }
    void IncreaseLength(size_t delta);
private:
    char* buffer_;
    size_t capacity_;  // Allocation size of payload (or -1 if buffer is const).
    size_t length_;    // Current length of the buffer.
    basic::Endianness endianness_;
};
}