//===- DependencyScanner.h - Performs module dependency scanning *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNER_H
#define LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNER_H

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Serialization/ObjectFilePCHContainerReader.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningFilesystem.h"
#include "clang/Tooling/DependencyScanning/ModuleDepCollector.h"

namespace clang {
class DiagnosticConsumer;

namespace tooling {
namespace dependencies {
class DependencyScanningService;
class DependencyConsumer;
class DependencyActionController;
class DependencyScanningWorkerFilesystem;

class DependencyScanningAction {
public:
  DependencyScanningAction(
      DependencyScanningService &Service, StringRef WorkingDirectory,
      DependencyConsumer &Consumer, DependencyActionController &Controller,
      llvm::IntrusiveRefCntPtr<DependencyScanningWorkerFilesystem> DepFS,
      llvm::IntrusiveRefCntPtr<DependencyScanningCASFilesystem> DepCASFS,
      llvm::IntrusiveRefCntPtr<llvm::cas::CachingOnDiskFileSystem> CacheFS,
      bool EmitDependencyFile, bool DiagGenerationAsCompilation,
      const CASOptions &CASOpts,
      std::optional<StringRef> ModuleName = std::nullopt,
      raw_ostream *VerboseOS = nullptr)
      : Service(Service), WorkingDirectory(WorkingDirectory),
        Consumer(Consumer), Controller(Controller), DepFS(std::move(DepFS)),
        DepCASFS(std::move(DepCASFS)), CacheFS(std::move(CacheFS)),
        CASOpts(CASOpts), EmitDependencyFile(EmitDependencyFile),
        DiagGenerationAsCompilation(DiagGenerationAsCompilation),
        ModuleName(ModuleName), VerboseOS(VerboseOS) {}
  bool runInvocation(std::shared_ptr<CompilerInvocation> Invocation,
                     IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                     DiagnosticConsumer *DiagConsumer);

  bool hasScanned() const { return Scanned; }
  bool hasDiagConsumerFinished() const { return DiagConsumerFinished; }

  /// Take the cc1 arguments corresponding to the most recent invocation used
  /// with this action. Any modifications implied by the discovered dependencies
  /// will have already been applied.
  std::vector<std::string> takeLastCC1Arguments();

  std::optional<std::string> takeLastCC1CacheKey();

  IntrusiveRefCntPtr<llvm::vfs::FileSystem> getDepScanFS();

private:
  DependencyScanningService &Service;
  StringRef WorkingDirectory;
  DependencyConsumer &Consumer;
  DependencyActionController &Controller;
  llvm::IntrusiveRefCntPtr<DependencyScanningWorkerFilesystem> DepFS;
  llvm::IntrusiveRefCntPtr<DependencyScanningCASFilesystem> DepCASFS;
  llvm::IntrusiveRefCntPtr<llvm::cas::CachingOnDiskFileSystem> CacheFS;
  const CASOptions &CASOpts;
  bool EmitDependencyFile = false;
  bool DiagGenerationAsCompilation;
  std::optional<StringRef> ModuleName;
  std::optional<CompilerInstance> ScanInstanceStorage;
  std::shared_ptr<ModuleDepCollector> MDC;
  std::vector<std::string> LastCC1Arguments;
  std::optional<std::string> LastCC1CacheKey;
  bool Scanned = false;
  bool DiagConsumerFinished = false;
  raw_ostream *VerboseOS;
};

// Helper functions
void sanitizeDiagOpts(DiagnosticOptions &DiagOpts);

} // namespace dependencies
} // namespace tooling
} // namespace clang

#endif
