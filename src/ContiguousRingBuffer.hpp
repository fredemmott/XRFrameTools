// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <stdexcept>
#include <type_traits>

/** A basic ring buffer that is NOT thread-safe.
 *
 * It is not actually a 'ring buffer' as such, it just acts like one.
 *
 * This is currently used for the GUI apps' live metrics; it's useful for
 * passing a C array of live data to plotting libraries.
 */
template <class T, std::size_t N>
  requires std::is_trivially_copyable_v<T> && (N > 0)
class ContiguousRingBuffer {
 public:
  ContiguousRingBuffer() = delete;
  ContiguousRingBuffer(const std::size_t initialSize = 0) : mSize(initialSize) {
    if (initialSize > N) {
      throw std::out_of_range("initialSize larger than capacity");
    }
  }

  std::size_t size() const noexcept {
    return mSize;
  }

  T& at(std::size_t index) {
    if (index >= N) {
      throw std::out_of_range("index larger than capacity");
    }
    return mData.at(index);
  }

  T* data() noexcept {
    return mData.data();
  }

  T& back() {
    if (mSize == 0) {
      throw std::out_of_range("no items in container");
    }
    return mData.at(mSize - 1);
  }

  T& front() {
    if (mSize == 0) {
      throw std::out_of_range("no items in container");
    }
    return mData.front();
  }

  template <class U = T>
  void push_back(U&& value) {
    std::memmove(
      mData.data(), mData.data() + 1, sizeof(T) * (mData.size() - 1));
    mData.back() = value;
    if (mSize < N) {
      ++mSize;
    }
  }

  struct Iterator {
    friend class ContiguousRingBuffer;

    using difference_type = std::ptrdiff_t;
    using value_type = T;

    T& operator*() const {
      return *mPtr;
    }

    T* operator->() const {
      return mPtr;
    }

    Iterator& operator++() {
      ++mPtr;
      return *this;
    }

    Iterator operator++(int) {
      auto dup = *this;
      return ++dup;
    }

    constexpr bool operator==(const Iterator&) const noexcept = default;

   private:
    T* mPtr {nullptr};
  };

  auto begin() noexcept {
    Iterator ret;
    ret.mPtr = data();
    return ret;
  }
  auto end() noexcept {
    Iterator ret;
    ret.mPtr = data() + size();
    return ret;
  }

 private:
  std::array<T, N> mData;
  std::size_t mSize {};
};