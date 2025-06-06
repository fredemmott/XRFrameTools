// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// USAGE:
// 1. Define `hooked_xrFoo()` functions
// 2. Define `next_xxrFoo` PFN variables
// 3. Define HOOKED_OPENXR_FUNCS(X) macro, e.g. X(WaitFrame)
// 4. include "APILayerEntrypoints.inc.cpp"

#include <source_location>
#include <span>
template <class F, auto Next, auto Layer>
struct XRFuncDelegator;

template <class TRet, class... TArgs, auto LayerFn, auto NextFn>
struct XRFuncDelegator<TRet(XRAPI_PTR*)(TArgs...), LayerFn, NextFn> {
  static XRAPI_ATTR TRet XRAPI_CALL Invoke(TArgs... args) try {
    if (!*NextFn) {
      return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    return std::invoke(LayerFn, args...);
  } catch (const std::exception& e) {
    dprint("Exception thrown from XR func: {}", e.what());
    return XR_ERROR_RUNTIME_FAILURE;
  } catch (...) {
    dprint("Unknown exception thrown from XR func");
    return XR_ERROR_RUNTIME_FAILURE;
  }
};

PFN_xrGetInstanceProcAddr next_xrGetInstanceProcAddr {nullptr};
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
  XrInstance instance,
  const char* name,
  PFN_xrVoidFunction* function) {
  if (!next_xrGetInstanceProcAddr) {
    return XR_ERROR_FUNCTION_UNSUPPORTED;
  }

  const std::string_view nameView {name};

#define HOOK(FN) \
  if (nameView == "xr" #FN) { \
    *function = reinterpret_cast<PFN_xrVoidFunction>( \
      &XRFuncDelegator<PFN_xr##FN, &hooked_xr##FN, &next_xr##FN>::Invoke); \
    return XR_SUCCESS; \
  }
  HOOKED_OPENXR_FUNCS(HOOK)
#undef HOOK

  return next_xrGetInstanceProcAddr(instance, name, function);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(
  const XrInstanceCreateInfo* info,
  const struct XrApiLayerCreateInfo* layerInfo,
  XrInstance* instance) {
  next_xrGetInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;

  XrInstanceCreateInfo nextCreateInfo {*info};
#ifdef DESIRED_OPENXR_EXTENSIONS
  std::vector<const char*> extensionNames;
  extensionNames.append_range(
    std::span {info->enabledExtensionNames, info->enabledExtensionCount});
#define ADD_EXT(X) \
  bool have##X {true}; \
  extensionNames.push_back(X);
  DESIRED_OPENXR_EXTENSIONS(ADD_EXT)
#undef ADD_EXT
  nextCreateInfo.enabledExtensionCount
    = static_cast<uint32_t>(extensionNames.size());
  nextCreateInfo.enabledExtensionNames = extensionNames.data();
#endif

  auto nextLayerInfo = *layerInfo;
  nextLayerInfo.nextInfo = nextLayerInfo.nextInfo->next;
  auto ret = layerInfo->nextInfo->nextCreateApiLayerInstance(
    &nextCreateInfo, &nextLayerInfo, instance);
  if (XR_FAILED(ret)) {
    dprint(
      "⚠️ xrCreateApiLayerInstance failed, retrying without additional "
      "extensions");
    ret = layerInfo->nextInfo->nextCreateApiLayerInstance(
      info, &nextLayerInfo, instance);
    if (XR_FAILED(ret)) {
      return ret;
    }
#ifdef DESIRED_OPENXR_EXTENSIONS
#define DISABLE_EXT(X) have##X = false;
    DESIRED_OPENXR_EXTENSIONS(DISABLE_EXT)
#undef DISABLE_EXT
#endif
  }

#define INIT_NEXT(FUNC) \
  next_xrGetInstanceProcAddr( \
    *instance, \
    "xr" #FUNC, \
    reinterpret_cast<PFN_xrVoidFunction*>(&next_xr##FUNC));
  HOOKED_OPENXR_FUNCS(INIT_NEXT)
#define INIT_NEXT_EXT(EXT, FUNC) \
  if (have##EXT) { \
    INIT_NEXT(FUNC); \
  }
#ifdef NEXT_OPENXR_FUNCS
  NEXT_OPENXR_FUNCS(INIT_NEXT)
#endif
#ifdef NEXT_OPENXR_EXT_FUNCS
  NEXT_OPENXR_EXT_FUNCS(INIT_NEXT_EXT)
#endif
#undef INIT_NEXT_EXT
#undef INIT_NEXT

  return ret;
}

// - __declspec(dllexport) is sufficient on x64
// - on x86, `XRAPI_ATTR` adds `__stcall`, which leads to mangling even with
//   `extern "C"`
#if defined(_WIN32) && !defined(_WIN64)
#pragma comment( \
  linker, \
  "/export:xrNegotiateLoaderApiLayerInterface=_xrNegotiateLoaderApiLayerInterface@12")
#endif
extern "C" __declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(
  const XrNegotiateLoaderInfo* loaderInfo,
  const char* layerName,
  XrNegotiateApiLayerRequest* apiLayerRequest) {
  // "The API layer **must**..."
  if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO) {
    dprint("Bad loaderInfo structType");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION) {
    dprint("Bad loaderInfo structVersion");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
    dprint("Bad loaderInfo structSize");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (
    apiLayerRequest->structType
    != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST) {
    dprint("Bad apiLayerRequest structType");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION) {
    dprint("Bad apiLayerRequest structVersion");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest)) {
    dprint("Bad apiLayerRequest structSize");
    return XR_ERROR_INITIALIZATION_FAILED;
  }

  // Return our info
  const bool supports1_0 = XR_API_VERSION_1_0 >= loaderInfo->minApiVersion
    && XR_API_VERSION_1_0 <= loaderInfo->maxApiVersion;
  const bool supports1_1 = XR_API_VERSION_1_1 >= loaderInfo->minApiVersion
    && XR_API_VERSION_1_1 <= loaderInfo->maxApiVersion;

  if (!(supports1_0 || supports1_1)) {
    dprint("No compatible OpenXR version");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (supports1_1) {
    dprint("[{}] Using OpenXR version 1.1", API_LAYER_TARGET_NAME);
    apiLayerRequest->layerApiVersion = XR_API_VERSION_1_1;
  } else {
    dprint("[{}] Using OpenXR version 1.0", API_LAYER_TARGET_NAME);
    apiLayerRequest->layerApiVersion = XR_API_VERSION_1_0;
  }

  apiLayerRequest->getInstanceProcAddr = &xrGetInstanceProcAddr;
  apiLayerRequest->createApiLayerInstance = &xrCreateApiLayerInstance;

  dprint(
    "[{}] xrNegotiateLoaderApiLayerInterface success", API_LAYER_TARGET_NAME);

  return XR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD dwReason, LPVOID reserved) {
  switch (dwReason) {
    case DLL_PROCESS_ATTACH:
      TraceLoggingRegister(gTraceProvider);
      TraceLoggingWrite(gTraceProvider, "DllMain/ProcessAttach");
      break;
    case DLL_PROCESS_DETACH:
      TraceLoggingWrite(gTraceProvider, "DllMain/ProcessDetach");
      TraceLoggingUnregister(gTraceProvider);
      break;
  }
  return TRUE;
}