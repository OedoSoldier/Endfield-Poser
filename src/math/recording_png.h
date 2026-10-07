#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <memory>
#include "libdeflate/libdeflate.h"

// Lossless PNG with stored DEFLATE blocks. Trades disk space for predictable
// encoding time. RGBA bytes and straight alpha are preserved exactly.
// PNG/zlib layout: https://www.w3.org/TR/png/#10Compression
namespace recording_png {
inline uint32_t Crc(const unsigned char *data,size_t size) {
  static const auto table=[] {
    std::array<std::array<uint32_t,256>,8> t{};
    for(uint32_t i=0;i<256;++i) {
      uint32_t c=i;for(int b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0);t[0][i]=c;
    }
    for(int n=1;n<8;++n)for(int i=0;i<256;++i)t[n][i]=t[0][t[n-1][i]&255]^(t[n-1][i]>>8);
    return t;
  }();
  uint32_t crc=~0u;
  while(size>=8) {
    crc^=uint32_t(data[0])|(uint32_t(data[1])<<8)|(uint32_t(data[2])<<16)|(uint32_t(data[3])<<24);
    crc=table[7][crc&255]^table[6][(crc>>8)&255]^table[5][(crc>>16)&255]^table[4][crc>>24]^
      table[3][data[4]]^table[2][data[5]]^table[1][data[6]]^table[0][data[7]];
    data+=8;size-=8;
  }
  while(size--)crc=table[0][(crc^*data++)&255]^(crc>>8);
  return ~crc;
}
inline void U32(std::vector<unsigned char> &out,uint32_t v) {
  out.push_back(static_cast<unsigned char>(v>>24));out.push_back(static_cast<unsigned char>(v>>16));
  out.push_back(static_cast<unsigned char>(v>>8));out.push_back(static_cast<unsigned char>(v));
}
inline void Chunk(std::vector<unsigned char> &out,const char *type,const unsigned char *data,size_t size) {
  U32(out,uint32_t(size));const size_t begin=out.size();out.insert(out.end(),type,type+4);
  if(size)out.insert(out.end(),data,data+size);U32(out,Crc(out.data()+begin,size+4));
}
inline std::vector<unsigned char> Encode(const std::vector<unsigned char> &rgba,int width,int height) {
  if(width<=0||height<=0||uint64_t(width)*height>33554432||rgba.size()!=uint64_t(width)*height*4)
    throw std::runtime_error("Invalid recording PNG dimensions");
  const size_t stride=size_t(width)*4+1;
  std::vector<unsigned char> raw(stride*height);
  for(int y=0;y<height;++y)memcpy(raw.data()+size_t(y)*stride+1,rgba.data()+size_t(height-1-y)*(stride-1),stride-1);
  std::vector<unsigned char> z;z.reserve(raw.size()+((raw.size()+65534)/65535)*5+6);
  z.push_back(0x78);z.push_back(0x01);
  for(size_t offset=0;offset<raw.size();) {
    const auto n=uint16_t((std::min)(size_t(65535),raw.size()-offset));
    z.push_back(offset+n==raw.size()?1:0);z.push_back(n&255);z.push_back(n>>8);z.push_back((~n)&255);z.push_back((~n>>8)&255);
    z.insert(z.end(),raw.data()+offset,raw.data()+offset+n);offset+=n;
  }
  uint32_t a=1,b=0;
  for(size_t offset=0;offset<raw.size();) {
    const size_t end=(std::min)(raw.size(),offset+5552);
    while(offset<end){a+=raw[offset++];b+=a;}a%=65521;b%=65521;
  }
  U32(z,(b<<16)|a);
  std::vector<unsigned char> out;out.reserve(z.size()+57);
  out.insert(out.end(),{137,80,78,71,13,10,26,10});
  std::vector<unsigned char> ihdr;U32(ihdr,uint32_t(width));U32(ihdr,uint32_t(height));ihdr.insert(ihdr.end(),{8,6,0,0,0});
  Chunk(out,"IHDR",ihdr.data(),ihdr.size());Chunk(out,"IDAT",z.data(),z.size());Chunk(out,"IEND",nullptr,0);return out;
}
// Fixed Up filtering is cheap and reversible, and compresses transparent
// padding efficiently. A persistent worker reuses its compressor allocation.
inline std::vector<unsigned char> EncodeFast(const std::vector<unsigned char> &rgba,int width,int height,int filter=2,bool opaque=false) {
  if(width<=0||height<=0||uint64_t(width)*height>33554432||rgba.size()!=uint64_t(width)*height*4||filter<0||filter>2)
    throw std::runtime_error("Invalid recording PNG dimensions/filter");
  // Background/full-scene alpha is known to be 255. PNG RGB restores that
  // exact alpha on decode, avoids 25% of IO, and needs no costly LZ search.
  if(opaque)filter=0;
  const size_t sourceRow=size_t(width)*4,row=size_t(width)*(opaque?3:4),stride=row+1;
  std::vector<unsigned char> raw(stride*height);
  for(int y=0;y<height;++y) {
    auto dst=raw.data()+size_t(y)*stride;auto src=rgba.data()+size_t(height-1-y)*sourceRow;
    *dst++=static_cast<unsigned char>(filter);
    if(opaque)for(int x=0;x<width;++x) {dst[3*x]=src[4*x];dst[3*x+1]=src[4*x+1];dst[3*x+2]=src[4*x+2];}
    else if(filter==2&&y)for(size_t x=0;x<row;++x)dst[x]=static_cast<unsigned char>(src[x]-src[row+x]);
    else if(filter==1) {
      memcpy(dst,src,4);for(size_t x=4;x<row;++x)dst[x]=static_cast<unsigned char>(src[x]-src[x-4]);
    } else memcpy(dst,src,row);
  }
  using Compressor=std::unique_ptr<libdeflate_compressor,decltype(&libdeflate_free_compressor)>;
  thread_local Compressor compressed(libdeflate_alloc_compressor(1),libdeflate_free_compressor);
  thread_local Compressor stored(libdeflate_alloc_compressor(0),libdeflate_free_compressor);
  auto &compressor=opaque?stored:compressed;
  if(!compressor)throw std::runtime_error("Cannot allocate PNG compressor");
  std::vector<unsigned char> z(libdeflate_zlib_compress_bound(compressor.get(),raw.size()));
  const auto bytes=libdeflate_zlib_compress(compressor.get(),raw.data(),raw.size(),z.data(),z.size());
  if(!bytes)throw std::runtime_error("PNG compression failed");
  z.resize(bytes);
  std::vector<unsigned char> out;out.reserve(bytes+57);out.insert(out.end(),{137,80,78,71,13,10,26,10});
  std::vector<unsigned char> ihdr;U32(ihdr,uint32_t(width));U32(ihdr,uint32_t(height));ihdr.insert(ihdr.end(),{8,static_cast<unsigned char>(opaque?2:6),0,0,0});
  Chunk(out,"IHDR",ihdr.data(),ihdr.size());Chunk(out,"IDAT",z.data(),z.size());Chunk(out,"IEND",nullptr,0);return out;
}
}
