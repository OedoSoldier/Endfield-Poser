#pragma once
#include "il2cpp_api.h"
#include <tlhelp32.h>
#include <string>

static std::string RuntimePathUtf8(const std::wstring &path) {
  if (path.empty()) return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, path.data(), int(path.size()), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) return {};
  std::string result(size_t(bytes), '\0');
  if (!WideCharToMultiByte(CP_UTF8, 0, path.data(), int(path.size()), result.data(), bytes, nullptr, nullptr)) return {};
  return result;
}
struct RuntimeModulePathInfo {
  std::string path;
  DWORD error = ERROR_SUCCESS;
  bool truncated = false;
};
static RuntimeModulePathInfo ReadRuntimeModulePath(HMODULE module) {
  wchar_t wide[32768]{};
  RuntimeModulePathInfo result;
  SetLastError(ERROR_SUCCESS);
  const DWORD count = GetModuleFileNameW(module, wide, DWORD(sizeof(wide) / sizeof(*wide)));
  if (!count) { result.error = GetLastError(); return result; }
  if (count >= sizeof(wide) / sizeof(*wide)) {
    result.error = ERROR_INSUFFICIENT_BUFFER; result.truncated = true; return result;
  }
  result.path = RuntimePathUtf8(std::wstring(wide, count));
  if (result.path.empty()) result.error = GetLastError();
  return result;
}

// Startup diagnostics are emitted once per stage, before any runtime calls.
// Paths are UTF-8 and explicitly marked if the bounded diagnostic was cut off.
static void LogRuntimeModulePath(const char *stage, HMODULE module) {
  const auto info = ReadRuntimeModulePath(module);
  Log("[BOOT] stage=%s pid=%lu tid=%lu module=%p path=%.3072s truncated=%d win32=%lu",
      stage, GetCurrentProcessId(), GetCurrentThreadId(), module,
      info.path.empty() ? "unavailable" : info.path.c_str(),
      int(info.truncated || info.path.size() > 3072), info.error);
}

static void LogRuntimeLoadedModules() {
  constexpr unsigned limit = 24;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                            GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) {
    Log("[BOOT] module-list unavailable pid=%lu win32=%lu", GetCurrentProcessId(), GetLastError());
    return;
  }
  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  char names[2048]{};
  unsigned shown = 0;
  bool truncated = false;
  BOOL next = Module32FirstW(snapshot, &entry);
  DWORD error = next ? ERROR_SUCCESS : GetLastError();
  while (next) {
    if (shown >= limit) { truncated = true; break; }
    char name[768]{};
    WideCharToMultiByte(CP_UTF8, 0, entry.szModule, -1, name, int(sizeof(name)), nullptr, nullptr);
    const size_t used = strlen(names), length = strlen(name);
    if (used + length + (shown ? 1 : 0) >= sizeof(names)) { truncated = true; break; }
    _snprintf_s(names + used, sizeof(names) - used, _TRUNCATE, "%s%s", shown ? "," : "", name);
    ++shown;
    next = Module32NextW(snapshot, &entry);
    if (!next && GetLastError() != ERROR_NO_MORE_FILES) error = GetLastError();
  }
  CloseHandle(snapshot);
  Log("[BOOT] module-list pid=%lu shown=%u limit=%u truncated=%d win32=%lu names=%s",
      GetCurrentProcessId(), shown, limit, int(truncated), error, names);
}

static HMODULE WaitForRuntimeModule(DWORD timeoutMs = 180000,
                                   RuntimeModuleLookup lookup = GetModuleHandleW) {
  Log("[BOOT] Waiting for GameAssembly.dll pid=%lu timeoutMs=%lu", GetCurrentProcessId(), timeoutMs);
  const ULONGLONG start = GetTickCount64();
  for (;;) {
    if (HMODULE module = FindRuntimeModule(lookup)) {
      LogRuntimeModulePath("runtime-module-found", module);
      Log("[BOOT] GameAssembly.dll found hGA=%p elapsedMs=%llu", module, GetTickCount64() - start);
      return module;
    }
    const ULONGLONG elapsed = GetTickCount64() - start;
    if (elapsed >= timeoutMs) {
      Log("[BOOT] GameAssembly.dll load timed out; plugin initialization skipped pid=%lu elapsedMs=%llu",
          GetCurrentProcessId(), elapsed);
      LogRuntimeModulePath("runtime-module-timeout-host", nullptr);
      LogRuntimeLoadedModules();
      return nullptr;
    }
    Sleep(DWORD(timeoutMs - elapsed < 100 ? timeoutMs - elapsed : 100));
  }
}

// Observing a completed managed call on an already registered game thread is a
// readiness signal. A loaded DLL, non-null domain or fixed delay is not one:
// domain_get can create the domain before GC permits external registration.
// This temporary hook forwards the exact exported runtime_invoke ABI. It never
// attaches a thread, calls managed code, logs or blocks the game's calling thread.
static t_il2cpp_runtime_invoke g_bootstrapOriginalInvoke = nullptr;
static std::atomic<bool> g_bootstrapObserved{false};
static void *BootstrapRuntimeInvoke(void *method, void *object, void **args,
                                   void **exception) {
  void *result = g_bootstrapOriginalInvoke(method, object, args, exception);
  if (!g_bootstrapObserved.load(std::memory_order_relaxed) && CurrentRuntimeThread())
    g_bootstrapObserved.store(true, std::memory_order_release);
  return result;
}

static bool WaitForRuntimeReady(DWORD timeoutMs = 180000) {
  if (g_runtimeReady.load(std::memory_order_acquire)) return true;
  if (!il2cpp_runtime_invoke || !il2cpp_thread_current || !il2cpp_domain_get ||
      !il2cpp_thread_attach || !il2cpp_thread_detach) {
    Log("[BOOT] Required runtime exports missing; plugin initialization skipped");
    return false;
  }
  auto target = reinterpret_cast<void *>(il2cpp_runtime_invoke);
  auto status = MH_CreateHook(target, reinterpret_cast<void *>(BootstrapRuntimeInvoke),
                             reinterpret_cast<void **>(&g_bootstrapOriginalInvoke));
  if (status != MH_OK) {
    Log("[BOOT] Cannot observe runtime startup (%d); plugin initialization skipped", status);
    return false;
  }
  if ((status = MH_EnableHook(target)) != MH_OK) {
    Log("[BOOT] Cannot enable runtime observer (%d); plugin initialization skipped", status);
    return false;
  }
  Log("[BOOT] Waiting for a managed call on a registered game thread...");
  ULONGLONG start = GetTickCount64();
  while (!g_bootstrapObserved.load(std::memory_order_acquire) &&
         GetTickCount64() - start < timeoutMs) Sleep(25);
  // Keep the trampoline allocated: another call may still be returning through
  // it. Disabling restores the export without freeing an in-flight trampoline.
  MH_DisableHook(target);
  if (!g_bootstrapObserved.load(std::memory_order_acquire)) {
    Log("[BOOT] Runtime readiness timed out; no plugin thread was attached");
    return false;
  }
  g_runtimeReady.store(true, std::memory_order_release);
  Log("[BOOT] Runtime ready after %llu ms; external threads may now attach",
      GetTickCount64() - start);
  return true;
}
