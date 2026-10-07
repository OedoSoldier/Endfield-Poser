#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include "math/camera_repair.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cwctype>

namespace fs=std::filesystem;
using camera_repair::Json;
constexpr UINT Done=WM_APP+1;
enum {Vmd=100,Camera,Offset,BrowseVmd,BrowseCamera,Analyze,Export,All,None,List,Status};
struct Result {Json doc;camera_repair::Plan plan;std::wstring error;};
static HWND window;static HFONT font;static float scale=1;
static std::thread worker;static bool busy=false,populating=false;static std::unique_ptr<Result> current;
static fs::path vmdPath,cameraPath;

static std::wstring Wide(const std::string &s) {
  int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
  std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),out.data(),n);return out;
}
static std::wstring Text(int id) {
  auto h=GetDlgItem(window,id);std::wstring s(GetWindowTextLengthW(h)+1,0);
  s.resize(GetWindowTextW(h,s.data(),int(s.size())));return s;
}
static std::wstring Number(double v) {std::wostringstream s;s<<std::fixed<<std::setprecision(4)<<v;return s.str();}
static void Say(const std::wstring &s) {SetWindowTextW(GetDlgItem(window,Status),s.c_str());}
static void Error(const std::wstring &s) {MessageBoxW(window,s.c_str(),L"镜头修复",MB_OK|MB_ICONWARNING);}
static double ReadOffset() {
  auto s=Text(Offset);wchar_t *end=nullptr;double value=wcstod(s.c_str(),&end);
  if(end==s.c_str())throw std::runtime_error(u8"时间偏移请输入秒数，例如 0 或 -10.5");
  while(iswspace(*end))++end;
  if(*end||!std::isfinite(value)||std::abs(value)>86400)
    throw std::runtime_error(u8"时间偏移请输入 -86400 至 86400 之间的秒数");
  return value;
}
static void UpdateButtons() {
  if(populating)return;
  for(int id:{BrowseVmd,BrowseCamera,Offset,Analyze})EnableWindow(GetDlgItem(window,id),!busy);
  const bool ready=!busy&&current&&!current->plan.cuts.empty();
  for(int id:{All,None,List})EnableWindow(GetDlgItem(window,id),ready);
  bool selected=false;
  if(ready)for(size_t i=0;i<current->plan.cuts.size();++i)
    if(ListView_GetCheckState(GetDlgItem(window,List),int(i))&&!current->plan.cuts[i].existing)selected=true;
  EnableWindow(GetDlgItem(window,Export),selected);
}
static void Invalidate() {
  current.reset();ListView_DeleteAllItems(GetDlgItem(window,List));
  Say(L"选择对应的两个镜头文件，再点击“分析切镜点”。");UpdateButtons();
}
static fs::path Choose(bool save,bool vmd) {
  wchar_t name[32768]{};
  if(save&&!cameraPath.empty()) {
    auto proposed=cameraPath.parent_path()/(cameraPath.stem().wstring()+L"_切镜修复.epcamera");
    for(int suffix=2;fs::exists(proposed);++suffix)
      proposed=cameraPath.parent_path()/(cameraPath.stem().wstring()+L"_切镜修复_"+std::to_wstring(suffix)+L".epcamera");
    wcsncpy_s(name,proposed.c_str(),_TRUNCATE);
  }
  OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=window;
  dialog.lpstrFilter=vmd?L"VMD 镜头 (*.vmd)\0*.vmd\0":L"Blender 镜头 (*.epcamera)\0*.epcamera\0";
  dialog.lpstrFile=name;dialog.nMaxFile=32768;dialog.lpstrDefExt=vmd?L"vmd":L"epcamera";
  dialog.lpstrTitle=save?L"另存修复镜头（请选择新文件名）":vmd?L"选择原始 VMD 镜头":L"选择需要修复的 .epcamera";
  auto initial=cameraPath.empty()?vmdPath.parent_path():cameraPath.parent_path();
  dialog.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();
  dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_DONTADDTORECENT|
    (save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
  return (save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))?fs::path(name):fs::path{};
}
static Json ReadCamera(const fs::path &path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  if(!in)throw std::runtime_error(u8"无法打开旧镜头文件");
  auto size=in.tellg();if(size<=0||size>256ll*1024*1024)throw std::runtime_error(u8"旧镜头文件为空或超过 256 MiB");
  std::string bytes(size_t(size),'\0');in.seekg(0);
  if(!in.read(bytes.data(),size))throw std::runtime_error(u8"旧镜头读取不完整");
  // Bound nesting before DOM parsing; accepted camera exports are shallow.
  int depth=0;bool quoted=false,escape=false;
  for(char c:bytes) {
    if(quoted) {if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;}
    else if(c=='"')quoted=true;
    else if(c=='{'||c=='['){if(++depth>64)throw std::runtime_error(u8"镜头 JSON 嵌套过深");}
    else if(c=='}'||c==']')--depth;
  }
  return Json::parse(bytes);
}
static camera_repair::Reference ReadVmd(const fs::path &path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  if(!in)throw std::runtime_error(u8"无法打开参考 VMD");
  auto size=in.tellg();if(size<=0||size>mmd::MaxVmdFileBytes)throw std::runtime_error(u8"参考 VMD 为空或超过 1 GiB");
  in.seekg(0);mmd::Reader reader(in,size_t(size));return camera_repair::ReadReference(reader);
}
static void WriteNew(const fs::path &path,const Json &doc) {
  // CREATE_NEW also rejects aliases, hard links and existing destinations.
  // No partial target is visible: write beside it, flush, then non-replacing move.
  auto payload=doc.dump(-1,' ',false,Json::error_handler_t::strict);
  if(payload.size()>256ull*1024*1024)throw std::runtime_error(u8"修复结果超过 256 MiB");
  if(fs::exists(path))throw std::runtime_error(u8"输出文件已存在，请选择新文件名；原文件不会被覆盖");
  fs::path temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
  HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)throw std::runtime_error(u8"无法创建输出文件，请检查目录与写入权限");
  bool ok=true;size_t offset=0;
  while(offset<payload.size()) {
    DWORD written=0,n=DWORD((std::min)(size_t(1024*1024),payload.size()-offset));
    if(!WriteFile(h,payload.data()+offset,n,&written,nullptr)||written!=n){ok=false;break;}
    offset+=written;
  }
  if(ok)ok=FlushFileBuffers(h)!=0;
  if(!CloseHandle(h))ok=false;
  if(ok)ok=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
  if(!ok) {DeleteFileW(temp.c_str());throw std::runtime_error(u8"保存失败或目标文件已存在；原文件未修改");}
}
static void StartAnalysis() {
  if(vmdPath.empty()||cameraPath.empty()){Error(L"请先选择参考 VMD 和旧版 .epcamera。");return;}
  double offset=ReadOffset();Invalidate();busy=true;UpdateButtons();Say(L"正在读取和分析…");
  auto vmd=vmdPath,camera=cameraPath;
  worker=std::thread([vmd,camera,offset] {
    auto result=std::make_unique<Result>();
    try {auto ref=ReadVmd(vmd);result->doc=ReadCamera(camera);result->plan=camera_repair::Analyze(ref,result->doc,offset);}
    catch(const std::exception &e){result->error=L"分析失败："+Wide(e.what());}
    catch(...){result->error=L"分析失败：文件数据或内存不可用。";}
    if(PostMessageW(window,Done,0,reinterpret_cast<LPARAM>(result.get())))result.release();
  });
}
static void ShowPlan() {
  const auto &p=current->plan;auto list=GetDlgItem(window,List);
  populating=true;SendMessageW(list,WM_SETREDRAW,FALSE,0);
  for(size_t i=0;i<p.cuts.size();++i) {
    const auto &cut=p.cuts[i];auto frame=std::to_wstring(cut.vmdFrame);
    LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=int(i);item.pszText=frame.data();ListView_InsertItem(list,&item);
    std::wstring values[]{Number(cut.requested),Number(cut.actual),cut.existing?L"已有（保留）":L"待补回"};
    for(int col=0;col<3;++col)ListView_SetItemText(list,int(i),col+1,values[col].data());
    ListView_SetCheckState(list,int(i),!p.dense);
  }
  populating=false;SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
  std::wstring summary=L"参考 VMD："+Number(p.referenceDuration)+L" 秒；旧镜头："+Number(p.duration)+L" 秒 / "+std::to_wstring(p.totalFrames)+L" 帧。\r\n";
  summary+=L"范围内 "+std::to_wstring(p.cuts.size())+L" 处；范围外 "+std::to_wstring(p.outside)+L" 处；已有 "+std::to_wstring(p.existing)+L" 处。";
  if(p.rounded)summary+=L"\r\n"+std::to_wstring(p.rounded)+L" 处对齐到后一个采样帧，请核对列表时间。";
  if(p.merged)summary+=L" 同帧合并 "+std::to_wstring(p.merged)+L" 处。";
  if(p.dense)summary+=L"\r\n参考镜头含大量逐帧关键帧，可能是连续运镜：默认未勾选，请手动选择真正的转场。";
  else if(p.cuts.empty())summary+=L"\r\n没有可补回的切镜点，请检查参考文件和时间偏移。";
  else summary+=L"\r\n请确认两文件对应同一镜头、时间轴未缩放；程序无法仅凭时长确认来源。";
  Say(summary);UpdateButtons();
}
static void StartExport() {
  if(!current)return;
  std::vector<bool> selected;
  for(size_t i=0;i<current->plan.cuts.size();++i)selected.push_back(ListView_GetCheckState(GetDlgItem(window,List),int(i))!=0);
  auto path=Choose(true,false);if(path.empty())return;
  busy=true;UpdateButtons();Say(L"正在另存修复镜头…");
  // The UI cannot replace the immutable analysis until this worker completes.
  worker=std::thread([path,selected] {
    auto result=std::make_unique<Result>();
    try {auto doc=camera_repair::Repair(current->doc,current->plan,selected);WriteNew(path,doc);}
    catch(const std::exception &e){result->error=L"保存失败："+Wide(e.what());}
    catch(...){result->error=L"保存失败：文件数据或内存不可用。";}
    result->doc=Json{{"output",path.u8string()}};
    if(PostMessageW(window,Done,1,reinterpret_cast<LPARAM>(result.get())))result.release();
  });
}
static HWND Control(const wchar_t *klass,const wchar_t *text,int id,int x,int y,int w,int h,DWORD style=0) {
  auto hwnd=CreateWindowExW(wcscmp(klass,L"EDIT")==0?WS_EX_CLIENTEDGE:0,klass,text,
    WS_CHILD|WS_VISIBLE|style,int(x*scale),int(y*scale),int(w*scale),int(h*scale),window,
    reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
  SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return hwnd;
}
static LRESULT CALLBACK Proc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
  try {
    switch(msg) {
    case WM_CREATE: {
      window=h;auto dc=GetDC(h);scale=GetDeviceCaps(dc,LOGPIXELSX)/96.f;ReleaseDC(h,dc);
      font=CreateFontW(-int(15*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
      Control(L"STATIC",L"用原始 VMD 的相邻镜头关键帧补回硬切标记，保留旧镜头的位置、角度、焦距和对焦。",0,18,14,794,24);
      Control(L"STATIC",L"参考 VMD",0,18,53,110,24);
      Control(L"EDIT",L"",Vmd,126,48,596,30,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP);
      Control(L"BUTTON",L"选择…",BrowseVmd,730,48,82,30,WS_TABSTOP);
      Control(L"STATIC",L"旧 .epcamera",0,18,91,110,24);
      Control(L"EDIT",L"",Camera,126,86,596,30,ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP);
      Control(L"BUTTON",L"选择…",BrowseCamera,730,86,82,30,WS_TABSTOP);
      Control(L"STATIC",L"时间偏移（秒）",0,18,133,118,24);
      Control(L"EDIT",L"0",Offset,142,126,98,30,ES_AUTOHSCROLL|WS_TABSTOP);
      Control(L"STATIC",L"旧镜头时间 = VMD 时间 + 偏移；从第 10 秒裁切导出，填 -10。",0,254,131,558,25);
      Control(L"BUTTON",L"分析切镜点",Analyze,18,167,142,32,WS_TABSTOP);
      Control(L"BUTTON",L"全选",All,174,167,76,32,WS_TABSTOP);
      Control(L"BUTTON",L"全不选",None,260,167,88,32,WS_TABSTOP);
      Control(L"BUTTON",L"另存修复镜头…",Export,636,167,176,32,WS_TABSTOP);
      auto list=Control(WC_LISTVIEWW,L"",List,18,211,794,230,LVS_REPORT|LVS_SINGLESEL|WS_BORDER|WS_TABSTOP);
      ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_CHECKBOXES|LVS_EX_DOUBLEBUFFER);
      const wchar_t *headers[]{L"VMD 帧（30 FPS）",L"对应旧镜头（秒）",L"实际切镜（秒）",L"处理"};
      for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=int((i==3?185:195)*scale);column.pszText=const_cast<wchar_t*>(headers[i]);ListView_InsertColumn(list,i,&column);}
      Control(L"EDIT",L"",Status,18,452,794,106,ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);
      Control(L"STATIC",L"只补切镜标记，不覆盖输入文件。修复结果可直接在游戏加载，或导回 Blender 继续编辑。",0,18,573,794,24);
      Invalidate();return 0;
    }
    case WM_COMMAND:
      if(busy)return 0;
      switch(LOWORD(wp)) {
      case BrowseVmd: case BrowseCamera: {
        bool vmd=LOWORD(wp)==BrowseVmd;auto path=Choose(false,vmd);if(path.empty())return 0;
        (vmd?vmdPath:cameraPath)=path;SetWindowTextW(GetDlgItem(h,vmd?Vmd:Camera),path.c_str());Invalidate();break;
      }
      case Offset:if(HIWORD(wp)==EN_CHANGE)Invalidate();break;
      case Analyze:StartAnalysis();break;
      case Export:StartExport();break;
      case All:case None:
        populating=true;
        for(int i=0;i<ListView_GetItemCount(GetDlgItem(h,List));++i)ListView_SetCheckState(GetDlgItem(h,List),i,LOWORD(wp)==All);
        populating=false;
        UpdateButtons();break;
      }return 0;
    case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(lp)->idFrom==List&&reinterpret_cast<NMHDR*>(lp)->code==LVN_ITEMCHANGED)UpdateButtons();return 0;
    case Done: {
      if(worker.joinable())worker.join();busy=false;
      std::unique_ptr<Result> result(reinterpret_cast<Result*>(lp));
      if(!result->error.empty()){Say(result->error);Error(result->error);}
      else if(wp==0){current=std::move(result);ShowPlan();}
      else Say(L"已保存："+Wide(result->doc["output"].get<std::string>())+L"\r\n原文件未修改。请在游戏加载修复文件，复测转场。");
      UpdateButtons();return 0;
    }
    case WM_CLOSE:if(busy){Say(L"正在处理文件，请完成后再关闭窗口。");return 0;}DestroyWindow(h);return 0;
    case WM_DESTROY:if(font)DeleteObject(font);PostQuitMessage(0);return 0;
    }
  } catch(const std::exception &e){busy=false;Say(Wide(e.what()));UpdateButtons();Error(Wide(e.what()));}
  return DefWindowProcW(h,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
  SetProcessDPIAware();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
  WNDCLASSW wc{};wc.hInstance=instance;wc.lpszClassName=L"EndfieldCameraRepair";wc.lpfnWndProc=Proc;
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
  RegisterClassW(&wc);auto dc=GetDC(nullptr);float dpi=GetDeviceCaps(dc,LOGPIXELSX)/96.f;ReleaseDC(nullptr,dc);
  DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;RECT rect{0,0,int(832*dpi),int(610*dpi)};AdjustWindowRect(&rect,style,FALSE);
  auto h=CreateWindowW(wc.lpszClassName,L"Endfield Poser · 镜头修复工具",style,CW_USEDEFAULT,CW_USEDEFAULT,
    rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
  if(!h)return 1;ShowWindow(h,show);MSG message{};
  while(GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(h,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  if(worker.joinable())worker.join();return int(message.wParam);
}
