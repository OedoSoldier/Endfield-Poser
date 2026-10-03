#pragma once
#include <cstdint>

// Optional SBM compatibility ABI. All data is copied; no STL or managed objects
// cross the DLL boundary. Acquire/Release bracket a persistent actor lease.
namespace sbm_mmd {
constexpr uint32_t Version=1;
struct Profile {
  uint32_t size=sizeof(Profile),enabled=0,axis=2,allowFallback=0,explicitAxis=0;
  float sign=1,scale=1,frequency=2.5f;
  char right[128]{},left[128]{};
};
struct Api {
  uint32_t size=sizeof(Api),version=Version;
  int (__cdecl *profile)(const char *model,Profile *out)=nullptr;
  int (__cdecl *acquire)(void *animator,uint64_t token)=nullptr;
  void (__cdecl *release)(void *animator,uint64_t token)=nullptr;
};
using GetApi=int (__cdecl *)(uint32_t version,uint32_t size,Api *out);
}
