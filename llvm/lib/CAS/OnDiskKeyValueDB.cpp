//===- OnDiskKeyValueDB.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CAS/OnDiskKeyValueDB.h"
#include "OnDiskCommon.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace llvm::cas;
using namespace llvm::cas::ondisk;

static constexpr StringLiteral ActionCacheFile = "actions";
static constexpr StringLiteral FilePrefix = "v4.";

Expected<ArrayRef<char>> OnDiskKeyValueDB::put(ArrayRef<uint8_t> Key,
                                               ArrayRef<char> Value) {
  if (LLVM_UNLIKELY(Value.size() != ValueSize))
    return createStringError(errc::invalid_argument,
                             "expected value size of " + itostr(ValueSize) +
                                 ", got: " + itostr(Value.size()));
  assert(Value.size() == ValueSize);
  auto ActionP = Cache.insertLazy(
      Key, [&](FileOffset TentativeOffset,
               OnDiskHashMappedTrie::ValueProxy TentativeValue) {
        assert(TentativeValue.Data.size() == ValueSize);
        llvm::copy(Value, TentativeValue.Data.data());
      });
  if (LLVM_UNLIKELY(!ActionP))
    return ActionP.takeError();
  return (*ActionP)->Data;
}

Expected<std::optional<ArrayRef<char>>>
OnDiskKeyValueDB::get(ArrayRef<uint8_t> Key) {
  // Check the result cache.
  OnDiskHashMappedTrie::const_pointer ActionP = Cache.find(Key);
  if (!ActionP)
    return std::nullopt;
  assert(isAddrAligned(Align(8), ActionP->Data.data()));
  return ActionP->Data;
}

Expected<std::unique_ptr<OnDiskKeyValueDB>>
OnDiskKeyValueDB::open(StringRef Path, StringRef HashName, unsigned KeySize,
                       StringRef ValueName, size_t ValueSize,
                       std::shared_ptr<OnDiskCASLogger> Logger) {
  if (std::error_code EC = sys::fs::create_directories(Path))
    return createFileError(Path, EC);

  SmallString<256> CachePath(Path);
  sys::path::append(CachePath, FilePrefix + ActionCacheFile);
  constexpr uint64_t MB = 1024ull * 1024ull;
  constexpr uint64_t GB = 1024ull * 1024ull * 1024ull;

  uint64_t MaxFileSize = GB;
  auto CustomSize = getOverriddenMaxMappingSize();
  if (!CustomSize)
    return CustomSize.takeError();
  if (*CustomSize)
    MaxFileSize = **CustomSize;

  std::optional<OnDiskHashMappedTrie> ActionCache;
  if (Error E = OnDiskHashMappedTrie::create(
                    CachePath,
                    "llvm.actioncache[" + HashName + "->" + ValueName + "]",
                    KeySize * 8,
                    /*DataSize=*/ValueSize, MaxFileSize, /*MinFileSize=*/MB,
                    std::move(Logger))
                    .moveInto(ActionCache))
    return std::move(E);

  return std::unique_ptr<OnDiskKeyValueDB>(
      new OnDiskKeyValueDB(ValueSize, std::move(*ActionCache)));
}

Error OnDiskKeyValueDB::validate(CheckValueT CheckValue) const {
  return Cache.validate(
      [&](FileOffset Offset,
          OnDiskHashMappedTrie::ConstValueProxy Record) -> Error {
        auto formatError = [&](Twine Msg) {
          return createStringError(
              llvm::errc::illegal_byte_sequence,
              "bad cache value at 0x" +
                  utohexstr((unsigned)Offset.get(), /*LowerCase=*/true) + ": " +
                  Msg.str());
        };

        if (Record.Data.size() != ValueSize)
          return formatError("wrong cache value size");
        if (!isAligned(Align(8), Record.Data.size()))
          return formatError("wrong cache value alignment");
        if (CheckValue)
          return CheckValue(Offset, Record.Data);
        return Error::success();
      });
}