// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
#include <TraceLoggingProvider.h>
// clang-format on

#include "D3D11GpuTimer.hpp"

#include "CheckHResult.hpp"

TRACELOGGING_DECLARE_PROVIDER(gTraceProvider);

D3D11GpuTimer::D3D11GpuTimer(ID3D11Device* device) {
  device->GetImmediateContext(mContext.put());

  D3D11_QUERY_DESC desc {D3D11_QUERY_TIMESTAMP_DISJOINT};
  CheckHResult(device->CreateQuery(&desc, mDisjointQuery.put()));
  desc = {D3D11_QUERY_TIMESTAMP};
  CheckHResult(device->CreateQuery(&desc, mStartQuery.put()));
  CheckHResult(device->CreateQuery(&desc, mStopQuery.put()));
}

void D3D11GpuTimer::Start() {
  // Disjoint queries have a 'begin' and 'end'
  // Timestamp queries *only* have an 'end'
  mContext->Begin(mDisjointQuery.get());
  mContext->End(mStartQuery.get());
}
void D3D11GpuTimer::Stop() {
  mContext->End(mStopQuery.get());
  mContext->End(mDisjointQuery.get());
}

std::expected<uint64_t, GpuDataError> D3D11GpuTimer::GetMicroseconds() {
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint {};
  if (const auto ret = mContext->GetData(
        mDisjointQuery.get(),
        &disjoint,
        sizeof(disjoint),
        D3D11_ASYNC_GETDATA_DONOTFLUSH);
      ret != S_OK) {
    TraceLoggingWrite(
      gTraceProvider,
      "D3D11GpuTimer/Disjoint-GetData/Failure",
      TraceLoggingValue(ret, "HRESULT"));
    return std::unexpected {
      (ret == S_FALSE) ? GpuDataError::Pending : GpuDataError::Unusable};
  }
  uint64_t start {};
  uint64_t stop {};

  const auto startHr
    = mContext->GetData(mStartQuery.get(), &start, sizeof(start), 0);
  const auto stopHr
    = mContext->GetData(mStopQuery.get(), &stop, sizeof(stop), 0);
  if (startHr != S_OK || stopHr != S_OK) {
    if (startHr == S_FALSE || stopHr == S_FALSE) {
      return std::unexpected {GpuDataError::Pending};
    }
    TraceLoggingWrite(
      gTraceProvider,
      "D3D11GpuTimer/Timer-GetData/Failure",
      TraceLoggingValue(startHr, "StartHRESULT"),
      TraceLoggingValue(stopHr, "StopHRESULT"));
    return std::unexpected {GpuDataError::Unusable};
  }

  if (disjoint.Disjoint || !(disjoint.Frequency && start && stop)) {
    TraceLoggingWrite(
      gTraceProvider,
      "D3D11GpuTimer/Failure",
      TraceLoggingValue(disjoint.Disjoint, "Disjoint"),
      TraceLoggingValue(disjoint.Frequency, "Frequency"),
      TraceLoggingValue(start, "Start"),
      TraceLoggingValue(stop, "Stop"));
    return std::unexpected {GpuDataError::Unusable};
  }

  const auto diff = stop - start;
  constexpr uint64_t microsPerSecond = 1000000;
  const auto micros = (diff * microsPerSecond) / disjoint.Frequency;

  return micros;
}