#pragma once
#include <windows.h>
#include "cloth_input_boundary.h"
#include "cloth_input_state.h"
namespace poser_cloth_boundary {
inline bool MappedCodeRange(void *module,const unsigned char *base,CodeRange check) noexcept {
  if(!module||!base||!check.bytes||check.offset>UINTPTR_MAX-uintptr_t(base))return false;
  auto code=reinterpret_cast<const unsigned char*>(uintptr_t(base)+check.offset);
  MEMORY_BASIC_INFORMATION region{};
  if(!VirtualQuery(code,&region,sizeof(region))||region.AllocationBase!=module||region.State!=MEM_COMMIT||
      (region.Protect&(PAGE_NOACCESS|PAGE_GUARD))||
      !(region.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||
      uintptr_t(code)<uintptr_t(region.BaseAddress))return false;
  const size_t offset=uintptr_t(code)-uintptr_t(region.BaseAddress);
  if(offset>region.RegionSize||check.bytes>region.RegionSize-offset)return false;
  __try {return eiem_cloth_input::Fingerprint(code,check.bytes)==check.hash;}
  __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
