#pragma once
#include "game/transparent_capture.h"

namespace recording_mask_probe {
// One read-only inventory at the first colour sample. These globals are merely
// candidates: neither their names nor their presence prove usable alpha. Do not
// retain transient render-graph textures or feed them into production output.
inline void Inspect() {
  if(!il2cpp_string_new)return;
  try {
    auto shader=mmd_api::Class("UnityEngine","Shader"),texture=mmd_api::Class("UnityEngine","Texture");
    auto get=mmd_api::Method(shader,"GetGlobalTexture","UnityEngine.Texture",{"System.String"},true);
    auto width=mmd_api::Method(texture,"get_width","System.Int32"),height=mmd_api::Method(texture,"get_height","System.Int32");
    auto format=mmd_api::Method(texture,"get_graphicsFormat","UnityEngine.Experimental.Rendering.GraphicsFormat");
    if(!get||!width||!height||!format){Log("[MMD-MASK] global texture inspection API unavailable");return;}
    for(const char *name:{"_MotionVector","_PostProcessMask","_SSRSceneStencil","_CameraDepthTexture","_SceneDepthTexture","_ContactShadowStencilBuffer"}) {
      auto key=il2cpp_string_new(name);void *args[]{key},*value=nullptr;int w=0,h=0,f=0;
      if(!key||!mmd_api::Call(get,nullptr,args,value)||!UnityObjAlive(value)) {
        Log("[MMD-MASK] %s: no live global binding at colour extraction",name);continue;
      }
      poser_capture::Ref retained;if(!retained.capture(value))continue;
      if(!mmd_api::Value(width,value,w)||!mmd_api::Value(height,value,h)||!mmd_api::Value(format,value,f)) {
        Log("[MMD-MASK] %s: texture metadata unavailable",name);continue;
      }
      Log("[MMD-MASK] %s: %dx%d graphicsFormat=%d (candidate only; alpha/alignment not verified)",name,w,h,f);
    }
  } catch(...) {Log("[MMD-MASK] inspection failed; keeping validated multi-pass capture");}
}
}
