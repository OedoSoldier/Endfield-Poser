#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <algorithm>

namespace transparent_capture {
struct AlphaStats {size_t clear=0,solid=0,partial=0;};
inline AlphaStats Inspect(const unsigned char *rgba,size_t bytes,int width,int height) {
  if(width<1||height<1||width>8192||height>8192||uint64_t(width)*height>33554432||
     bytes!=uint64_t(width)*height*4||!rgba)throw std::runtime_error("Invalid capture dimensions");
  AlphaStats s;
  for(size_t i=3;i<bytes;i+=4) {
    if(rgba[i]==0)++s.clear;else if(rgba[i]==255)++s.solid;else ++s.partial;
  }
  return s;
}
inline bool Usable(const AlphaStats &s) {
  // Both an actual foreground and a transparent background must exist.
  return s.clear>0&&s.solid+s.partial>0;
}
struct EdgeStats {size_t corrected=0,unresolved=0;};
inline EdgeStats ApplyMatte(std::vector<unsigned char> &scene,const std::vector<unsigned char> &matte,int width,int height) {
  if(scene.size()!=matte.size())throw std::runtime_error("Mismatched colour and matte images");
  Inspect(matte.data(),matte.size(),width,height);
  // Scene RGB already contains background mixed into its AA fringe. Merely
  // replacing alpha leaves a light/coloured halo. Extend foreground RGB into
  // that fringe, while retaining the exact matte and every opaque RGB pixel.
  // Stay inside connected foreground, so colours cannot jump across a gap to
  // another piece of clothing. Bound propagation to eight pixels: this is a
  // local defringe, not an attempt to unmix an entire translucent material.
  int x0=width,y0=height,x1=-1,y1=-1;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(matte[(size_t(y)*width+x)*4+3]) {
    x0=(std::min)(x0,x);y0=(std::min)(y0,y);x1=(std::max)(x1,x);y1=(std::max)(y1,y);
  }
  if(x1<0){std::fill(scene.begin(),scene.end(),0);return {};}
  const int w=x1-x0+1,h=y1-y0+1;
  auto pixel=[&](int i){return (size_t(i/w+y0)*width+i%w+x0)*4;};
  std::vector<int32_t> owner(size_t(w)*h,-1),queue;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
    int i=y*w+x;if(matte[pixel(i)+3]!=255)continue;
    owner[i]=i;bool border=false;
    for(int dy=-1;dy<=1&&!border;++dy)for(int dx=-1;dx<=1;++dx) {
      int nx=x+dx,ny=y+dy;if(nx<0||nx>=w||ny<0||ny>=h)continue;
      auto a=matte[pixel(ny*w+nx)+3];if(a>0&&a<255){border=true;break;}
    }
    if(border)queue.push_back(i);
  }
  size_t begin=0;
  for(int distance=0;distance<8&&begin<queue.size();++distance) {
    const size_t end=queue.size();
    for(;begin<end;++begin) {
      int i=queue[begin],x=i%w,y=i/w;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        int nx=x+dx,ny=y+dy;if(nx<0||nx>=w||ny<0||ny>=h)continue;
        int next=ny*w+nx;
        if(owner[next]>=0||!matte[pixel(next)+3])continue;
        owner[next]=owner[i];queue.push_back(next);
      }
    }
  }
  EdgeStats stats;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
    const size_t p=(size_t(y)*width+x)*4;auto a=matte[p+3];
    scene[p+3]=a;
    if(!a){scene[p]=scene[p+1]=scene[p+2]=0;continue;}
    if(a==255)continue;
    int seed=owner[(y-y0)*w+x-x0];
    if(seed<0){++stats.unresolved;continue;}
    const size_t source=pixel(seed);
    for(int c=0;c<3;++c)scene[p+c]=scene[source+c];
    ++stats.corrected;
  }
  return stats;
}
inline bool SameTransform(const float *a,const float *b,size_t count) {
  for(size_t i=0;i<count;++i)if(!std::isfinite(a[i])||!std::isfinite(b[i])||std::abs(a[i]-b[i])>0.0001f)return false;
  return true;
}
inline int UnusedLayer(uint32_t used) {
  for(int i=31;i>=8;--i)if(!(used&(uint32_t(1)<<i)))return i;
  return -1;
}
}
