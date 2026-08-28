#pragma once
// 独立 UI 方案：插件内置 localhost HTTP 服务器，浏览器访问 http://127.0.0.1:18923
// 获得独立的摆姿控制窗口（画布骨骼小人 + 滑条）。彻底绕开游戏内 HUD 的
// 焦点/输入拦截/合成问题。

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdio>
#include <cstring>
#include <string>

#include "nlohmann/json.hpp"
#include "config.h"
#include "game/skeleton.h"
#include "game/freeze.h"
#include "editor/panel_pose.h"

static int g_webPort = 18923;
static volatile bool g_webRunning = false;

// ---- 简易 HTTP 响应 ----
static void HttpReply(SOCKET c, const char *ctype, const std::string &body) {
  char head[512];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Content-Length: %d\r\nConnection: close\r\n\r\n",
                   ctype, (int)body.size());
  send(c, head, n, 0);
  send(c, body.data(), (int)body.size(), 0);
}

static void HttpJson(SOCKET c, const nlohmann::json &j) {
  HttpReply(c, "application/json", j.dump());
}

// ---- API：骨骼列表 ----
static nlohmann::json ApiBones() {
  nlohmann::json arr = nlohmann::json::array();
  for (int i = 0; i < s_humanBoneCount; i++) {
    nlohmann::json o;
    o["i"] = i;
    o["name"] = s_humanBones[i].name;
    o["locked"] = s_humanBones[i].locked;
    if (s_humanBones[i].transform) {
      Vec3 p = GetBoneWorldPos(s_humanBones[i].transform);
      o["x"] = p.x; o["y"] = p.y; o["z"] = p.z;
      Quat lr = GetBoneLocalRot(s_humanBones[i].transform);
      Vec3 lp = GetBoneLocalPos(s_humanBones[i].transform);
      o["lrx"] = lr.x; o["lry"] = lr.y; o["lrz"] = lr.z; o["lrw"] = lr.w;
      o["lpx"] = lp.x; o["lpy"] = lp.y; o["lpz"] = lp.z;
      int pi = FindTransformIndex(
          g_transform_get_parent
              ? Invoke(g_transform_get_parent, s_humanBones[i].transform)
              : nullptr);
      o["parent"] = pi;
    } else {
      o["parent"] = -1;
    }
    arr.push_back(o);
  }
  return arr;
}

// ---- 处理一个请求 ----
static void HandleRequest(SOCKET c, const std::string &path,
                          const std::string &body) {
  if (path == "/" || path == "/index.html") {
    // 内嵌网页：画布骨骼小人 + 滑条 + 按钮
    extern const char *g_poserHtml;
    HttpReply(c, "text/html; charset=utf-8", g_poserHtml);
    return;
  }
  if (path == "/api/status") {
    HttpJson(c, {{"ok", true}, {"frozen", g_frozen},
                 {"bones", s_humanBoneCount},
                 {"bones_rev", s_bonesRev},
                 {"selected", g_selectedBone}});
    return;
  }
  if (path == "/api/bones") {
    HttpJson(c, {{"ok", true}, {"bones", ApiBones()}});
    return;
  }
  if (path == "/api/allbones") {
    // 全骨骼（含手指/配饰等），供 Blender 桥接构建完整 Armature
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < s_allBones.size(); i++) {
      nlohmann::json o;
      o["i"] = (int)i;
      o["name"] = s_allBones[i].name;
      o["parent"] = s_allBones[i].parentIdx;
      if (s_allBones[i].transform) {
        Vec3 p = GetBoneWorldPos(s_allBones[i].transform);
        o["x"] = p.x; o["y"] = p.y; o["z"] = p.z;
        Quat lr = GetBoneLocalRot(s_allBones[i].transform);
        Vec3 lp = GetBoneLocalPos(s_allBones[i].transform);
        o["lrx"] = lr.x; o["lry"] = lr.y; o["lrz"] = lr.z; o["lrw"] = lr.w;
        o["lpx"] = lp.x; o["lpy"] = lp.y; o["lpz"] = lp.z;
      }
      arr.push_back(o);
    }
    HttpJson(c, {{"ok", true}, {"rev", s_bonesRev}, {"bones", arr}});
    return;
  }
  if (path == "/api/select") {
    auto j = nlohmann::json::parse(body, nullptr, false);
    int i = j.value("i", -1);
    if (i >= 0 && i < s_humanBoneCount)
      g_selectedBone = i;
    HttpJson(c, {{"ok", true}});
    return;
  }
  if (path == "/api/freeze") {
    auto j = nlohmann::json::parse(body, nullptr, false);
    bool on = j.value("on", !g_frozen);
    if (on) {
      if (!g_frozen)
        FreezeCharacter();
    } else {
      if (g_frozen)
        UnfreezeCharacter();
    }
    HttpJson(c, {{"ok", true}, {"frozen", g_frozen}});
    return;
  }
  if (path == "/api/tpose") {
    ApplyTPose();
    HttpJson(c, {{"ok", true}});
    return;
  }
  if (path == "/api/reset") {
    ApplyPoseSnapshot();
    HttpJson(c, {{"ok", true}});
    return;
  }
  if (path == "/api/setrot") {
    // 增量旋转：绕骨局部轴 {axis} 转 d 度
    auto j = nlohmann::json::parse(body, nullptr, false);
    int i = j.value("i", -1);
    int axis = j.value("axis", 0);
    float d = j.value("d", 0.0f);
    if (i >= 0 && i < s_humanBoneCount && !s_humanBones[i].locked) {
      const float kD2R = 3.14159265f / 180.0f;
      Vec3 axs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
      Quat cur = GetBoneLocalRot(s_humanBones[i].transform);
      SetBoneLocalRot(s_humanBones[i].transform,
                      NormQ(Quat::AxisAngle(axs[axis], d * kD2R) * cur));
    }
    HttpJson(c, {{"ok", true}});
    return;
  }
  if (path == "/api/setpos") {
    auto j = nlohmann::json::parse(body, nullptr, false);
    int i = j.value("i", -1);
    if (i >= 0 && i < s_humanBoneCount && !s_humanBones[i].locked) {
      Vec3 p = GetBoneLocalPos(s_humanBones[i].transform);
      if (j.contains("x")) p.x = j["x"];
      if (j.contains("y")) p.y = j["y"];
      if (j.contains("z")) p.z = j["z"];
      SetBoneLocalPos(s_humanBones[i].transform, p);
    }
    HttpJson(c, {{"ok", true}});
    return;
  }
  if (path == "/api/setbonerot") {
    auto j = nlohmann::json::parse(body, nullptr, false);
    int i = j.value("i", -1);
    if (i >= 0 && i < s_humanBoneCount && !s_humanBones[i].locked &&
        j.contains("q")) {
      Quat q{0, 0, 0, 1};
      const auto &qq = j["q"];
      if (qq.is_array() && qq.size() >= 4) {
        q.x = qq[0].get<float>();
        q.y = qq[1].get<float>();
        q.z = qq[2].get<float>();
        q.w = qq[3].get<float>();
      }
      SetBoneLocalRot(s_humanBones[i].transform, NormQ(q));
    }
    HttpJson(c, {{"ok", true}});
    return;
  }
  if (path == "/api/pose") {
    auto j = nlohmann::json::parse(body, nullptr, false);
    if (j.contains("pose")) {
      int applied = 0;
      for (auto &e : j["pose"]) {
        if (!e.is_object())
          continue;
        void *tr = nullptr;
        bool locked = false;
        if (e.contains("n")) {
          // 按名字匹配全骨骼（Blender 桥接主路径，含手指/配饰）
          std::string nm = e["n"].get<std::string>();
          for (size_t k = 0; k < s_allBones.size(); k++) {
            if (strcmp(s_allBones[k].name, nm.c_str()) == 0) {
              tr = s_allBones[k].transform;
              for (int h = 0; h < s_humanBoneCount; h++) {
                if (strcmp(s_humanBones[h].name, nm.c_str()) == 0) {
                  locked = s_humanBones[h].locked;
                  break;
                }
              }
              break;
            }
          }
        } else if (e.contains("i")) {
          int i = e.value("i", -1);
          if (i >= 0 && i < s_humanBoneCount) {
            tr = s_humanBones[i].transform;
            locked = s_humanBones[i].locked;
          }
        }
        if (!tr || locked)
          continue;
        if (e.contains("q")) {
          Quat q{0, 0, 0, 1};
          const auto &qq = e["q"];
          if (qq.is_array() && qq.size() >= 4) {
            q.x = qq[0].get<float>();
            q.y = qq[1].get<float>();
            q.z = qq[2].get<float>();
            q.w = qq[3].get<float>();
          }
          SetBoneLocalRot(tr, NormQ(q));
        }
        if (e.contains("p")) {
          Vec3 p{0, 0, 0};
          const auto &pp = e["p"];
          if (pp.is_array() && pp.size() >= 3) {
            p.x = pp[0].get<float>();
            p.y = pp[1].get<float>();
            p.z = pp[2].get<float>();
          }
          SetBoneLocalPos(tr, p);
        }
        applied++;
      }
      HttpJson(c, {{"ok", true}, {"applied", applied}});
      return;
    }
    // 返回当前姿势（整姿，供 Blender 同步）：全骨骼（含手指/配饰），按名匹配
    nlohmann::json arr = nlohmann::json::array();
    for (size_t i = 0; i < s_allBones.size(); i++) {
      if (!s_allBones[i].transform)
        continue;
      Quat q = GetBoneLocalRot(s_allBones[i].transform);
      Vec3 p = GetBoneLocalPos(s_allBones[i].transform);
      arr.push_back({{"n", s_allBones[i].name},
                     {"q", {q.x, q.y, q.z, q.w}},
                     {"p", {p.x, p.y, p.z}}});
    }
    HttpJson(c, {{"ok", true}, {"pose", arr}});
    return;
  }
  if (path == "/api/drag") {
    // 拖拽关节点：旋转其父骨，让 骨i 在面板平面(世界XY)对准 (wx,wy)
    auto j = nlohmann::json::parse(body, nullptr, false);
    int i = j.value("i", -1);
    float wx = j.value("wx", 0.0f), wy = j.value("wy", 0.0f);
    if (i >= 0 && i < s_humanBoneCount && !s_humanBones[i].locked) {
      void *t = s_humanBones[i].transform;
      void *parent =
          g_transform_get_parent ? Invoke(g_transform_get_parent, t) : nullptr;
      if (parent) {
        Vec3 pj = GetBoneWorldPos(t);
        Vec3 pp = GetBoneWorldPos(parent);
        float curA = atan2f(pj.y - pp.y, pj.x - pp.x);
        float tgtA = atan2f(wy - pp.y, wx - pp.x);
        float da = tgtA - curA;
        if (fabsf(da) > 1e-4f)
          ApplyWorldRotDelta(parent, Quat::AxisAngle(Vec3{0, 0, 1}, da));
      }
    }
    HttpJson(c, {{"ok", true}});
    return;
  }
  HttpJson(c, {{"ok", false}, {"err", "unknown api"}});
}

static void HandleClient(SOCKET c) {
  try {
    char buf[8192];
    int n = recv(c, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
      closesocket(c);
      return;
    }
    buf[n] = 0;
    std::string req(buf);
    std::string method, path, body;
    size_t sp = req.find(' ');
    if (sp != std::string::npos) {
      method = req.substr(0, sp);
      size_t sp2 = req.find(' ', sp + 1);
      if (sp2 != std::string::npos)
        path = req.substr(sp + 1, sp2 - sp - 1);
    }
    size_t hb = req.find("\r\n\r\n");
    if (hb != std::string::npos)
      body = req.substr(hb + 4);
    if (path.empty())
      path = "/";
    HandleRequest(c, path, body);
  } catch (...) {
    Log("[WEB] C++ exception in handler");
  }
  closesocket(c);
}

static DWORD WINAPI WebServerThread(LPVOID) {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  // 附加到 IL2CPP 域：本线程会调游戏对象（冻结/骨骼读写），不附加会触发运行时终止
  if (il2cpp_domain_get && il2cpp_thread_attach) {
    void *domain = il2cpp_domain_get();
    if (domain) {
      il2cpp_thread_attach(domain);
      Log("[WEB] attached to IL2CPP domain");
    }
  }
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    Log("[WEB] socket failed");
    return 0;
  }
  int opt = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((u_short)g_webPort);
  if (bind(s, (sockaddr *)&addr, sizeof(addr)) != 0) {
    Log("[WEB] bind 127.0.0.1:%d failed (port in use?)", g_webPort);
    closesocket(s);
    return 0;
  }
  listen(s, 8);
  Log("[WEB] UI server: http://127.0.0.1:%d", g_webPort);
  g_webRunning = true;
  while (g_webRunning) {
    // 非阻塞 accept：500ms 超时轮询，g_webRunning 置假后可干净退出
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    timeval tv = {0, 500000};
    int sel = select(0, &rfds, nullptr, nullptr, &tv);
    if (sel <= 0)
      continue;
    SOCKET c = accept(s, nullptr, nullptr);
    if (c == INVALID_SOCKET)
      break;
    // 简单串行处理（够用）
    HandleClient(c);
  }
  closesocket(s);
  WSACleanup();
  return 0;
}

static void StartWebServer() {
  if (g_webRunning)
    return;
  CreateThread(nullptr, 0, WebServerThread, nullptr, 0, nullptr);
}

// 内嵌网页（画布骨骼小人 + 滑条 + 按钮）
const char *g_poserHtml = R"HTML(<!DOCTYPE html>
<html lang="zh"><head><meta charset="utf-8"><title>Endfield Poser</title>
<style>
body{background:#15171e;color:#e8e8e8;font-family:system-ui;margin:0;display:flex;height:100vh}
#canvasBox{flex:1;position:relative;background:#1b1e29}
canvas{position:absolute;inset:0;width:100%;height:100%}
#side{width:300px;padding:12px;overflow-y:auto;background:#1e2230}
h1{font-size:16px;margin:0 0 8px}
button{margin:2px;padding:6px 10px;background:#3a4260;color:#fff;border:none;border-radius:4px;cursor:pointer}
button:hover{background:#4a5478}
button.on{background:#e8a020}
label{display:block;margin-top:8px;font-size:12px;color:#aaa}
.sl{display:flex;align-items:center;gap:6px}
.sl input{flex:1}
.sl span{width:52px;text-align:right;font-size:12px}
</style></head><body>
<div id="canvasBox"><canvas id="cv"></canvas><div id="tip" style="position:absolute;top:8px;left:8px;font-size:12px;color:#888">点击关节选中，拖动摆姿势</div></div>
<div id="side">
  <h1>Endfield Poser</h1>
  <div><button id="btnFreeze">冻结</button><button id="btnTpose">T-Pose</button><button id="btnReset">复位</button></div>
  <div style="font-size:12px;margin-top:6px" id="status">连接中...</div>
  <div id="boneInfo" style="font-size:12px;color:#bbb;margin-top:6px">未选中骨骼</div>
  <label>旋转 X</label><div class="sl"><input type="range" id="rx" min="-180" max="180" step="0.5" value="0"><span id="rxv">0</span></div>
  <label>旋转 Y</label><div class="sl"><input type="range" id="ry" min="-180" max="180" step="0.5" value="0"><span id="ryv">0</span></div>
  <label>旋转 Z</label><div class="sl"><input type="range" id="rz" min="-180" max="180" step="0.5" value="0"><span id="rzv">0</span></div>
  <label>位置 X</label><div class="sl"><input type="range" id="px" min="-3" max="3" step="0.01" value="0"><span id="pxv">0</span></div>
  <label>位置 Y</label><div class="sl"><input type="range" id="py" min="-3" max="3" step="0.01" value="0"><span id="pyv">0</span></div>
  <label>位置 Z</label><div class="sl"><input type="range" id="pz" min="-3" max="3" step="0.01" value="0"><span id="pzv">0</span></div>
</div>
<script>
const API='http://127.0.0.1:18923';
const cv=document.getElementById('cv'),ctx=cv.getContext('2d');
let bones=[],selected=-1,frozen=false;
function fit(){cv.width=cv.clientWidth;cv.height=cv.clientHeight;}
window.addEventListener('resize',fit);fit();
async function post(path,data){
  try{const r=await fetch(API+path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data||{})});return await r.json();}
  catch(e){return {ok:false};}
}
async function refresh(){
  try{
    const r=await fetch(API+'/api/bones');const d=await r.json();
    bones=d.bones||[];
    const s=await (await fetch(API+'/api/status')).json();
    frozen=s.frozen;selected=s.selected;
    document.getElementById('status').textContent=(frozen?'已冻结':'未冻结')+' | 骨骼 '+s.bones+' | 选中 '+(selected>=0?bones[selected]?.name:'无');
    draw();
  }catch(e){document.getElementById('status').textContent='未连接';}
}
function toPanel(p){ // 世界XY -> 画布坐标（正视图，Y翻转）
  let minX=1e9,maxX=-1e9,minY=1e9,maxY=-1e9;
  for(const b of bones){if(b.x<minX)minX=b.x;if(b.x>maxX)maxX=b.x;if(b.y<minY)minY=b.y;if(b.y>maxY)maxY=b.y;}
  const spanX=(maxX-minX)||1,spanY=(maxY-minY)||1;
  const sc=Math.min((cv.width-40)/spanX,(cv.height-40)/spanY)||1;
  const mx=(minX+maxX)/2,my=(minY+maxY)/2;
  return [cv.width/2+(p.x-mx)*sc, cv.height/2-(p.y-my)*sc, sc, mx, my];
}
function toWorld(px,py){
  let minX=1e9,maxX=-1e9,minY=1e9,maxY=-1e9;
  for(const b of bones){if(b.x<minX)minX=b.x;if(b.x>maxX)maxX=b.x;if(b.y<minY)minY=b.y;if(b.y>maxY)maxY=b.y;}
  const spanX=(maxX-minX)||1,spanY=(maxY-minY)||1;
  const sc=Math.min((cv.width-40)/spanX,(cv.height-40)/spanY)||1;
  const mx=(minX+maxX)/2,my=(minY+maxY)/2;
  return [mx+(px-cv.width/2)/sc, my-(py-cv.height/2)/sc];
}
function draw(){
  ctx.clearRect(0,0,cv.width,cv.height);
  if(!bones.length)return;
  const base=toPanel(bones[0]);
  // 连线
  ctx.strokeStyle='#c0c8dc';ctx.lineWidth=1.5;
  for(const b of bones){
    if(b.parent<0)continue;
    const p=bones[b.parent];if(!p)continue;
    const a=toPanel(p),c=toPanel(b);
    ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(c[0],c[1]);ctx.stroke();
  }
  // 关节点
  for(const b of bones){
    const c=toPanel(b);
    ctx.beginPath();
    ctx.fillStyle=b.i===selected?'#ffc83c':(b.locked?'#ff5a5a':'#78c8ff');
    ctx.arc(c[0],c[1],b.i===selected?6:4,0,Math.PI*2);ctx.fill();
  }
}
// 鼠标
let drag=-1;
function evPos(e){const r=cv.getBoundingClientRect();return [e.clientX-r.left,e.clientY-r.top];}
cv.addEventListener('mousedown',e=>{
  const [mx,my]=evPos(e);let best=-1,bd=20;
  for(const b of bones){const c=toPanel(b);const d=Math.hypot(mx-c[0],my-c[1]);if(d<bd){bd=d;best=b.i;}}
  if(best>=0){
    if(!frozen) post('/api/freeze',{on:true}).then(r=>{frozen=r.frozen;document.getElementById('btnFreeze').textContent=frozen?'解冻':'冻结';});
    selected=best;drag=best;post('/api/select',{i:best});draw();
  }
});
cv.addEventListener('mousemove',e=>{
  if(drag>=0){const [mx,my]=evPos(e);const [wx,wy]=toWorld(mx,my);post('/api/drag',{i:drag,wx,wy});}
});
cv.addEventListener('mouseup',()=>{drag=-1;});
// 滑条：按下拖动时发送增量
function slider(id,axis){
  const el=document.getElementById(id),lab=document.getElementById(id+'v');
  let base=0;
  el.addEventListener('pointerdown',()=>{base=parseFloat(el.value);});
  el.addEventListener('input',()=>{
    const d=parseFloat(el.value)-base;
    lab.textContent=el.value;
    if(selected>=0)post('/api/setrot',{i:selected,axis,d});
  });
}
slider('rx',0);slider('ry',1);slider('rz',2);
function posSlider(id,key){
  const el=document.getElementById(id),lab=document.getElementById(id+'v');
  let base=0;
  el.addEventListener('pointerdown',()=>{base=parseFloat(el.value);});
  el.addEventListener('input',()=>{
    const d=parseFloat(el.value)-base;
    lab.textContent=el.value;
    if(selected>=0)post('/api/setpos',{i:selected,[key]:d});
  });
}
posSlider('px','x');posSlider('py','y');posSlider('pz','z');
document.getElementById('btnFreeze').onclick=async()=>{
  const r=await post('/api/freeze',{on:!frozen});
  frozen=r.frozen;
  document.getElementById('btnFreeze').textContent=frozen?'解冻':'冻结';
};
document.getElementById('btnTpose').onclick=()=>post('/api/tpose',{});
document.getElementById('btnReset').onclick=()=>post('/api/reset',{});
setInterval(refresh,120);
refresh();
</script></body></html>)HTML";
