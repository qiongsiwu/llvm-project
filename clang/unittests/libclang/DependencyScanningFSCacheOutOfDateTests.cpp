//===- unittests/libclang/DependencyScanningFSCacheOutOfDateTests.cpp ---- ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang-c/Dependencies.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace tooling;
using namespace dependencies;

TEST(DependencyScanningFSCacheOutOfDate, Basic) {
  DependencyScanningService Service(ScanningMode::DependencyDirectivesScan,
                                    ScanningOutputFormat::Full, CASOptions(),
                                    nullptr, nullptr, nullptr);

  auto &SharedCache = Service.getSharedCache();

  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  DependencyScanningWorkerFilesystem DepFS(SharedCache, InMemoryFS);

  // file1 is negatively stat cached.
  // file2 has different sizes in the cache and on the physical file system.
  // The test creates file1 and file2 on the physical file system.
  // Service's cache is setup to use an in-memory file system so we
  // can leave out file1 and have a file2 with size 8 (instead of 0).
  int fd;
  llvm::SmallString<128> File1Path;
  llvm::SmallString<128> File2Path;
  auto EC = llvm::sys::fs::createTemporaryFile("file1", "h", fd, File1Path);
  ASSERT_FALSE(EC);
  EC = llvm::sys::fs::createTemporaryFile("file2", "h", fd, File2Path);
  ASSERT_FALSE(EC);

  // Trigger negative stat caching on DepFS.
  bool PathExists = DepFS.exists(File1Path);
  ASSERT_FALSE(PathExists);

  // Add file2 to the in memory FS with non-zero size.
  InMemoryFS->addFile(File2Path, 1,
                      llvm::MemoryBuffer::getMemBuffer("        "));
  PathExists = DepFS.exists(File2Path);
  ASSERT_TRUE(PathExists);

  CXDepScanFSOutOfDateEntrySet Entries =
      clang_experimental_DepScanFSCacheOutOfEntrySet_getSet(
          reinterpret_cast<CXDependencyScannerService>(&Service));

  size_t NumEntries =
      clang_experimental_DepScanFSCacheOutOfEntrySet_getNumOfEntries(Entries);
  EXPECT_EQ(NumEntries, 2u);

  for (size_t Idx = 0; Idx < NumEntries; Idx++) {
    CXDepScanFSOutOfDateEntry Entry =
        clang_experimental_DepScanFSCacheOutOfEntrySet_getEntry(Entries, 0);
    CXDepScanFSCacheOutOfDateKind Kind =
        clang_experimental_DepScanFSCacheOutOfEntrySet_getEntryKind(Entry);
    CXString Path =
        clang_experimental_DepScanFSCacheOutOfEntrySet_getEntryPath(Entry);
    ASSERT_TRUE(Kind == NegativelyCached || Kind == SizeChanged);
    switch (Kind) {
    case NegativelyCached:
      ASSERT_STREQ(clang_getCString(Path), File1Path.c_str());
      break;
    case SizeChanged:
      ASSERT_STREQ(clang_getCString(Path), File2Path.c_str());
      ASSERT_EQ(
          clang_experimental_DepScanFSCacheOutOfEntrySet_getEntryCachedSize(
              Entry),
          8u);
      ASSERT_EQ(
          clang_experimental_DepScanFSCacheOutOfEntrySet_getEntryActualSize(
              Entry),
          0u);
      break;
    }
  }

  clang_experimental_DepScanFSCacheOutOfEntrySet_disposeSet(Entries);
}