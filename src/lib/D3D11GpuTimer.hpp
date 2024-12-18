// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <d3d11.h>
#include <wil/com.h>

#include <expected>

enum class GpuDataError {
  Pending,
  Unusable,// e.g. disjoint
};

class D3D11GpuTimer {
 public:
  D3D11GpuTimer() = delete;

  D3D11GpuTimer(ID3D11Device* device);
  void Start();
  void Stop();
  std::expected<uint64_t, GpuDataError> GetMicroseconds();

 private:
  wil::com_ptr<ID3D11DeviceContext> mContext;

  wil::com_ptr<ID3D11Query> mDisjointQuery;
  wil::com_ptr<ID3D11Query> mStartQuery;
  wil::com_ptr<ID3D11Query> mStopQuery;
};