//===- OnDiskCASLogger.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CAS/OnDiskCASLogger.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#ifdef __APPLE__
#include <sys/time.h>
#endif

using namespace llvm;
using namespace llvm::cas;
using namespace llvm::cas::ondisk;

// The version number in this log should be bumped if the log format is changed
// in an incompatible way. It is currently a human-readable text file, so in
// practice this would be if the log changed to binary or other machine-
// readable format.
static constexpr StringLiteral Filename = "v1.log";

OnDiskCASLogger::OnDiskCASLogger(raw_fd_ostream &OS, bool LogAllocations)
    : OS(OS), LogAllocations(LogAllocations) {}

OnDiskCASLogger::~OnDiskCASLogger() {
  OS.flush();
  delete &OS;
}

static bool isDisabledEnv(StringRef V) {
  return StringSwitch<bool>(V)
      .Case("0", true)
      .CaseLower("no", true)
      .CaseLower("false", true)
      .Default(false);
}

Expected<std::unique_ptr<OnDiskCASLogger>>
OnDiskCASLogger::openIfEnabled(const Twine &Path) {
  const char *V = getenv("LLVM_CAS_LOG");
  if (V && !isDisabledEnv(V)) {
    int LogLevel = -1;
    StringRef(V).getAsInteger(10, LogLevel);
    return OnDiskCASLogger::open(Path, /*LogAllocations=*/LogLevel > 1 ? true
                                                                       : false);
  }
  return nullptr;
}
Expected<std::unique_ptr<OnDiskCASLogger>>
OnDiskCASLogger::open(const Twine &Path, bool LogAllocations) {
  std::error_code EC;
  SmallString<128> FullPath;
  Path.toVector(FullPath);
  sys::path::append(FullPath, Filename);

  auto OS =
      std::make_unique<raw_fd_ostream>(FullPath, EC, sys::fs::CD_OpenAlways,
                                       sys::fs::FA_Write, sys::fs::OF_Append);
  if (EC)
    return createFileError(FullPath, EC);

  // Buffer is not thread-safe.
  OS->SetUnbuffered();

  return std::unique_ptr<OnDiskCASLogger>(
      new OnDiskCASLogger{*OS.release(), LogAllocations});
}

static uint64_t getTimestampMillis() {
  #ifdef __APPLE__
    // Using chrono is roughly 50% slower.
    struct timeval T;
    gettimeofday(&T, 0);
    return T.tv_sec * 1000 + T.tv_usec / 1000;
  #else
    auto Time = std::chrono::system_clock::now();
    auto Millis = std::chrono::duration_cast<std::chrono::milliseconds>(Time.time_since_epoch());
    return Millis.count();
  #endif
}

namespace {
/// Helper to log a single line that adds the timestamp, pid, and tid. The line
/// is buffered and written in a single call to write() so that if the
/// underlying OS syscall is handled atomically so is this log message.
class TextLogLine : public raw_svector_ostream {
public:
  TextLogLine(raw_ostream &LogOS) : raw_svector_ostream(Buffer), LogOS(LogOS) {
    startLogMsg(*this);
  }

  ~TextLogLine() {
    finishLogMsg(*this);
    LogOS.write(Buffer.data(), Buffer.size());
  }

  static void startLogMsg(raw_ostream &OS) {
    auto Millis = getTimestampMillis();
    OS << format("%lld.%0.3lld", Millis / 1000, Millis % 1000);
    OS << ' ' << sys::Process::getProcessId() << ' ' << get_threadid() << ": ";
  }

  static void finishLogMsg(raw_ostream &OS) { OS << '\n'; }

private:
  raw_ostream &LogOS;
  SmallString<128> Buffer;
};
} // anonymous namespace

static void formatTrieOffset(raw_ostream &OS, int64_t Off) {
  if (Off < 0) {
    OS << '-';
    Off = -Off;
  }
  OS << format_hex(Off, 0);
}

void OnDiskCASLogger::log_compare_exchange_strong(void *Region, TrieOffset Trie,
                                                  size_t SlotI,
                                                  TrieOffset Expected,
                                                  TrieOffset New,
                                                  TrieOffset Previous) {
  TextLogLine Log(OS);
  Log << "cmpxcgh subtrie region=" << Region << " offset=";
  formatTrieOffset(Log, Trie);
  Log << " slot=" << SlotI << " expected=";
  formatTrieOffset(Log, Expected);
  Log << " new=";
  formatTrieOffset(Log, New);
  Log << " prev=";
  formatTrieOffset(Log, Previous);
}

void OnDiskCASLogger::log_SubtrieHandle_create(void *Region, TrieOffset Trie,
                                               uint32_t StartBit,
                                               uint32_t NumBits) {
  TextLogLine Log(OS);
  Log << "create subtrie region=" << Region << " offset=";
  formatTrieOffset(Log, Trie);
  Log << " start-bit=" << StartBit << " num-bits=" << NumBits;
}

void OnDiskCASLogger::log_HashMappedTrieHandle_createRecord(
    void *Region, TrieOffset Off, ArrayRef<uint8_t> Hash) {
  TextLogLine Log(OS);
  Log << "create record region=" << Region << " offset=";
  formatTrieOffset(Log, Off);
  Log << " hash=" << format_bytes(Hash, std::nullopt, 32, 32);
}

void OnDiskCASLogger::log_MappedFileRegionBumpPtr_resizeFile(StringRef Path,
                                                             size_t Before,
                                                             size_t After) {
  TextLogLine Log(OS);
  Log << "resize mapped file '" << Path << "' from=" << Before
      << " to=" << After;
}

void OnDiskCASLogger::log_MappedFileRegionBumpPtr_create(StringRef Path, int FD,
                                                         void *Region,
                                                         size_t Capacity,
                                                         size_t Size) {
  sys::fs::file_status Stat;
  std::error_code EC = status(FD, Stat);

  TextLogLine Log(OS);
  Log << "mmap '" << Path << "' " << Region;
  Log << " dev=" << (EC ? ~0ull : Stat.getUniqueID().getDevice());
  Log << " inode=" << (EC ? ~0ull : Stat.getUniqueID().getFile());
  ;
  Log << " size=" << Size << " capacity=" << Capacity;
}

void OnDiskCASLogger::log_MappedFileRegionBumpPtr_oom(StringRef Path,
                                                      size_t Capacity,
                                                      size_t Size,
                                                      size_t AllocSize) {
  TextLogLine Log(OS);
  Log << "oom '" << Path << "' old-size=" << Size << " capacity=" << Capacity
      << "alloc-size=" << AllocSize;
}
void OnDiskCASLogger::log_MappedFileRegionBumpPtr_close(StringRef Path) {
  TextLogLine Log(OS);
  Log << "close mmap '" << Path << "'";
}
void OnDiskCASLogger::log_MappedFileRegionBumpPtr_allocate(void *Region,
                                                           TrieOffset Off,
                                                           size_t Size) {
  if (!LogAllocations)
    return;
  TextLogLine Log(OS);
  Log << "alloc " << Region << " offset=";
  formatTrieOffset(Log, Off);
  Log << " size=" << Size;
}

void OnDiskCASLogger::log_UnifiedOnDiskCache_collectGarbage(StringRef Path) {
  TextLogLine Log(OS);
  Log << "collect garbage '" << Path << "'";
}

void OnDiskCASLogger::log_UnifiedOnDiskCache_validateIfNeeded(
    StringRef Path, uint64_t BootTime, uint64_t ValidationTime, bool CheckHash,
    bool AllowRecovery, bool Force, std::optional<StringRef> LLVMCas,
    StringRef ValidationError, bool Skipped, bool Recovered) {
  TextLogLine Log(OS);
  Log << "validate-if-needed '" << Path << "'";
  Log << " boot=" << BootTime << " last-valid=" << ValidationTime;
  Log << " check-hash=" << CheckHash << " allow-recovery=" << AllowRecovery;
  Log << " force=" << Force;
  if (LLVMCas)
    Log << " llvm-cas=" << *LLVMCas;
  if (Skipped)
    Log << " skipped";
  if (Recovered)
    Log << " recovered";
  if (!ValidationError.empty())
    Log << " data was invalid " << ValidationError;
}

void OnDiskCASLogger::log_TempFile_create(StringRef Name) {
  TextLogLine Log(OS);
  Log << "standalone file create '" << Name << "'";
}

void OnDiskCASLogger::log_TempFile_keep(StringRef TmpName, StringRef Name,
                                        std::error_code EC) {
  TextLogLine Log(OS);
  Log << "standalone file rename '" << TmpName << "' to '" << Name << "'";
  if (EC)
    Log << " error: " << EC.message();
}

void OnDiskCASLogger::log_TempFile_remove(StringRef TmpName,
                                          std::error_code EC) {
  TextLogLine Log(OS);
  Log << "standalone file remove '" << TmpName << "'";
  if (EC)
    Log << " error: " << EC.message();
}
