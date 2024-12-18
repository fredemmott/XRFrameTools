// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#include "D3D11GpuTimer.hpp"

#include "CheckHResult.hpp"

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
    return std::unexpected {
      (ret == S_FALSE) ? GpuDataError::Pending : GpuDataError::Unusable};
  }
  uint64_t start {};
  uint64_t stop {};
  if (
    mContext->GetData(mStartQuery.get(), &start, sizeof(start), 0) != S_OK
    || mContext->GetData(mStopQuery.get(), &stop, sizeof(stop), 0) != S_OK) {
    return std::unexpected {GpuDataError::Unusable};
  }

  if (disjoint.Disjoint || !(disjoint.Frequency && start && stop)) {
    return std::unexpected {GpuDataError::Unusable};
  }

  const auto diff = stop - start;
  constexpr uint64_t microsPerSecond = 1000000;
  const auto micros = (diff * microsPerSecond) / disjoint.Frequency;

  return micros;
}