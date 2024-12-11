// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <array>
#include <filesystem>
#include <utility>

std::filesystem::path GetKnownFolderPath(const GUID& folderID);

namespace detail::compile_time_guid {
// Marker for more readable compile errors
template <size_t ActualSize>
struct guids_must_be_36_chars_or_38_with_braces {
  guids_must_be_36_chars_or_38_with_braces() = delete;
};

template <char... Value>
struct GuidParser : guids_must_be_36_chars_or_38_with_braces<sizeof...(Value)> {
};

constexpr size_t SizeOfStringGuid
  = sizeof("00000000-0000-0000-0000-000000000000") - 1;

enum class ByteIndexKind {
  LittleEndian,
  ByteArray,
};

template <char... Value>
  requires(sizeof...(Value) == SizeOfStringGuid)
struct GuidParser<Value...> {
  static consteval GUID Parse() noexcept {
    // clang-format off
    //
    // 00000000-0000-0000-0000-000000000000
    //         8    13   18   23           36 <<< char offsets (separators)
    // 0        9    14   19   24             <<< char offset
    // 0        4    6    8    10             <<< byte offsets
    //
    // clang-format on
    static_assert(ArrayValue.at(8) == '-');
    static_assert(ArrayValue.at(13) == '-');
    static_assert(ArrayValue.at(18) == '-');
    static_assert(ArrayValue.at(23) == '-');

    using enum ByteIndexKind;
    uint8_t ret[sizeof(GUID)];
    // Offsets from ASCII art diagram above
    ParseRange<0, 0, 8, LittleEndian>(ret);
    ParseRange<4, 9, 13, LittleEndian>(ret);
    ParseRange<6, 14, 18, LittleEndian>(ret);
    ParseRange<8, 19, 23, ByteArray>(ret);
    ParseRange<10, 24, 36, ByteArray>(ret);

    return std::bit_cast<GUID>(ret);
  }

 private:
  static constexpr auto ArrayValue = std::array {Value...};

  template <
    size_t TByteOffset,
    size_t TCharBegin,
    size_t TCharEnd,
    ByteIndexKind TByteIndexKind>
  consteval static void ParseRange(uint8_t ret[38]) noexcept {
    static_assert(
      (TCharEnd - TCharBegin) % 2 == 0, "Hex digits should come in pairs");

    []<size_t... I>(uint8_t ret[38], std::index_sequence<I...>) {
      constexpr auto byteIndex = [](size_t It) {
        if constexpr (TByteIndexKind == ByteIndexKind::LittleEndian) {
          constexpr auto TotalBytes = (TCharEnd - TCharBegin) / 2;
          return TotalBytes - (It + 1) + TByteOffset;
        } else {
          return TByteOffset + It;
        }
      };

      ((ret[byteIndex(I)] = FromHexDigitPair<(2 * I) + TCharBegin>()), ...);
    }(ret, std::make_index_sequence<(TCharEnd - TCharBegin) / 2>());
  }

  template <size_t I>
  static consteval uint8_t FromHexDigitPair() {
    return FromHexDigit<I>() << 4 | FromHexDigit<I + 1>();
  }

  template <size_t I>
  static consteval uint8_t FromHexDigit() {
    const auto it = ArrayValue.at(I);
    if constexpr (it >= '0' && it <= '9') {
      return it - '0';
    } else if constexpr (it >= 'a' && it <= 'f') {
      return it - 'a' + 10;
    } else if constexpr (it >= 'A' && it <= 'F') {
      return it - 'A' + 10;
    } else {
      static_assert(false, "Out-of-range character");
      std::unreachable();// bogus 'not all control paths return a value'
    }
  }
};

static constexpr size_t SizeOfStringGuidWithBraces
  = sizeof("{00000000-0000-0000-0000-000000000000}") - 1;
template <char... Value>
  requires(sizeof...(Value) == SizeOfStringGuidWithBraces)
struct GuidParser<Value...> {
  static consteval GUID Parse() noexcept {
    static_assert(ArrayValue.front() == '{');
    static_assert(ArrayValue.back() == '}');

    static_assert(SizeOfStringGuid == SizeOfStringGuidWithBraces - 2);
    return []<std::size_t... Index>(std::index_sequence<Index...>) {
      return GuidParser<std::get<Index + 1>(ArrayValue)...>::Parse();
    }(std::make_index_sequence<SizeOfStringGuid> {});
  }

 private:
  static constexpr auto ArrayValue = std::array {Value...};
};

}// namespace detail::compile_time_guid

namespace detail {
template <std::size_t N>
struct CompileTimeStringHelper {
  char mValue[N - 1] {};
  consteval CompileTimeStringHelper(const char (&value)[N]) {
    for (size_t i = 0; i < (N - 1); ++i) {
      mValue[i] = value[i];
    }
  }
};
}// namespace detail

template <detail::CompileTimeStringHelper CTS>
consteval GUID operator"" _guid() {
  return []<std::size_t... Index>(std::index_sequence<Index...>) {
    // Construct an instance so we get the nice compile error if it's the wrong
    // size
    return detail::compile_time_guid::GuidParser<CTS.mValue[Index]...> {}
      .Parse();
  }(std::make_index_sequence<sizeof(CTS.mValue)> {});
}