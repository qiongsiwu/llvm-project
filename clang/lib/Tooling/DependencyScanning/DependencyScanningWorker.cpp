//===- DependencyScanningWorker.cpp - clang-scan-deps worker --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningWorker.h"
#include "DependencyScannerImpl.h"
#include "clang/Basic/DiagnosticDriver.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/DiagnosticSerialization.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/CompileJobCacheKey.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Serialization/ObjectFilePCHContainerReader.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "clang/Tooling/DependencyScanning/InProcessModuleCache.h"
#include "clang/Tooling/DependencyScanning/ModuleDepCollector.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/CAS/CASProvidingFileSystem.h"
#include "llvm/CAS/CachingOnDiskFileSystem.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/PrefixMapper.h"
#include "llvm/TargetParser/Host.h"
#include <optional>

using namespace clang;
using namespace tooling;
using namespace dependencies;
using llvm::Error;

DependencyScanningWorker::DependencyScanningWorker(
    DependencyScanningService &Service,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : Service(Service),
      CASOpts(Service.getCASOpts()), CAS(Service.getCAS()) {
  PCHContainerOps = std::make_shared<PCHContainerOperations>();
  // We need to read object files from PCH built outside the scanner.
  PCHContainerOps->registerReader(
      std::make_unique<ObjectFilePCHContainerReader>());
  // The scanner itself writes only raw ast files.
  PCHContainerOps->registerWriter(std::make_unique<RawPCHContainerWriter>());

  if (Service.shouldTraceVFS())
    FS = llvm::makeIntrusiveRefCnt<llvm::vfs::TracingFileSystem>(std::move(FS));

  if (Service.useCASFS()) {
    CacheFS = Service.getSharedFS().createProxyFS();
    DepCASFS = new DependencyScanningCASFilesystem(CacheFS, *Service.getCache());
    BaseFS = DepCASFS;
    return;
  }

  switch (Service.getMode()) {
  case ScanningMode::DependencyDirectivesScan:
    DepFS =
        new DependencyScanningWorkerFilesystem(Service.getSharedCache(), FS);
    BaseFS = DepFS;
    break;
  case ScanningMode::CanonicalPreprocessing:
    DepFS = nullptr;
    BaseFS = FS;
    break;
  }
}

llvm::IntrusiveRefCntPtr<FileManager>
DependencyScanningWorker::getOrCreateFileManager() const {
  return new FileManager(FileSystemOptions(), BaseFS);
}

static std::unique_ptr<DiagnosticOptions>
createDiagOptions(const std::vector<std::string> &CommandLine) {
  std::vector<const char *> CLI;
  for (const std::string &Arg : CommandLine)
    CLI.push_back(Arg.c_str());
  auto DiagOpts = CreateAndPopulateDiagOpts(CLI);
  sanitizeDiagOpts(*DiagOpts);
  return DiagOpts;
}

llvm::Error DependencyScanningWorker::computeDependencies(
    StringRef WorkingDirectory, const std::vector<std::string> &CommandLine,
    DependencyConsumer &Consumer, DependencyActionController &Controller,
    std::optional<llvm::MemoryBufferRef> TUBuffer) {
  // Capture the emitted diagnostics and report them to the client
  // in the case of a failure.
  std::string DiagnosticOutput;
  llvm::raw_string_ostream DiagnosticsOS(DiagnosticOutput);
  auto DiagOpts = createDiagOptions(CommandLine);
  TextDiagnosticPrinter DiagPrinter(DiagnosticsOS, *DiagOpts);

  if (computeDependencies(WorkingDirectory, CommandLine, Consumer, Controller,
                          DiagPrinter, TUBuffer))
    return llvm::Error::success();
  return llvm::make_error<llvm::StringError>(DiagnosticsOS.str(),
                                             llvm::inconvertibleErrorCode());
}

llvm::Error DependencyScanningWorker::computeDependencies(
    StringRef WorkingDirectory, const std::vector<std::string> &CommandLine,
    DependencyConsumer &Consumer, DependencyActionController &Controller,
    StringRef ModuleName) {
  // Capture the emitted diagnostics and report them to the client
  // in the case of a failure.
  std::string DiagnosticOutput;
  llvm::raw_string_ostream DiagnosticsOS(DiagnosticOutput);
  auto DiagOpts = createDiagOptions(CommandLine);
  TextDiagnosticPrinter DiagPrinter(DiagnosticsOS, *DiagOpts);

  if (computeDependencies(WorkingDirectory, CommandLine, Consumer, Controller,
                          DiagPrinter, ModuleName))
    return llvm::Error::success();
  return llvm::make_error<llvm::StringError>(DiagnosticsOS.str(),
                                             llvm::inconvertibleErrorCode());
}

static bool forEachDriverJob(
    ArrayRef<std::string> ArgStrs, DiagnosticsEngine &Diags,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
    llvm::function_ref<bool(const driver::Command &Cmd)> Callback) {
  SmallVector<const char *, 256> Argv;
  Argv.reserve(ArgStrs.size());
  for (const std::string &Arg : ArgStrs)
    Argv.push_back(Arg.c_str());

  std::unique_ptr<driver::Driver> Driver = std::make_unique<driver::Driver>(
      Argv[0], llvm::sys::getDefaultTargetTriple(), Diags,
      "clang LLVM compiler", FS);
  Driver->setTitle("clang_based_tool");

  llvm::BumpPtrAllocator Alloc;
  bool CLMode = driver::IsClangCL(
      driver::getDriverMode(Argv[0], ArrayRef(Argv).slice(1)));

  if (llvm::Error E =
          driver::expandResponseFiles(Argv, CLMode, Alloc, FS.get())) {
    Diags.Report(diag::err_drv_expand_response_file)
        << llvm::toString(std::move(E));
    return false;
  }

  const std::unique_ptr<driver::Compilation> Compilation(
      Driver->BuildCompilation(llvm::ArrayRef(Argv)));
  if (!Compilation)
    return false;

  if (Compilation->containsError())
    return false;

  for (const driver::Command &Job : Compilation->getJobs()) {
    if (!Callback(Job))
      return false;
  }
  return true;
}

static bool createAndRunToolInvocation(
    std::vector<std::string> CommandLine, DependencyScanningAction &Action,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
    std::shared_ptr<clang::PCHContainerOperations> &PCHContainerOps,
    DiagnosticsEngine &Diags, DependencyConsumer &Consumer) {

  // Save executable path before providing CommandLine to ToolInvocation
  std::string Executable = CommandLine[0];

  llvm::opt::ArgStringList Argv;
  for (const std::string &Str : ArrayRef(CommandLine).drop_front())
    Argv.push_back(Str.c_str());

  auto Invocation = std::make_shared<CompilerInvocation>();
  if (!CompilerInvocation::CreateFromArgs(*Invocation, Argv, Diags)) {
    // FIXME: Should we just go on like cc1_main does?
    return false;
  }

  if (!Action.runInvocation(std::move(Invocation), std::move(FS),
                            PCHContainerOps, Diags.getClient()))
    return false;

  std::vector<std::string> Args = Action.takeLastCC1Arguments();
  std::optional<std::string> CacheKey = Action.takeLastCC1CacheKey();
  Consumer.handleBuildCommand(
      {std::move(Executable), std::move(Args), std::move(CacheKey)});
  return true;
}

bool DependencyScanningWorker::scanDependencies(
    StringRef WorkingDirectory, const std::vector<std::string> &CommandLine,
    DependencyConsumer &Consumer, DependencyActionController &Controller,
    DiagnosticConsumer &DC, llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
    std::optional<StringRef> ModuleName) {
  std::vector<const char *> CCommandLine(CommandLine.size(), nullptr);
  llvm::transform(CommandLine, CCommandLine.begin(),
                  [](const std::string &Str) { return Str.c_str(); });
  auto DiagOpts = CreateAndPopulateDiagOpts(CCommandLine);
  sanitizeDiagOpts(*DiagOpts);
  auto Diags = CompilerInstance::createDiagnostics(*FS, *DiagOpts, &DC,
                                                   /*ShouldOwnClient=*/false);

  DependencyScanningAction Action(
      Service, WorkingDirectory, Consumer, Controller, DepFS, DepCASFS, CacheFS,
      /*EmitDependencyFile=*/false,
      /*DiagGenerationAsCompilation=*/false, getCASOpts(), ModuleName);
  bool Success = false;
  if (CommandLine[1] == "-cc1") {
    Success = createAndRunToolInvocation(CommandLine, Action, FS,
                                         PCHContainerOps, *Diags, Consumer);
  } else {
    Success = forEachDriverJob(
        CommandLine, *Diags, FS, [&](const driver::Command &Cmd) {
          if (StringRef(Cmd.getCreator().getName()) != "clang") {
            // Non-clang command. Just pass through to the dependency
            // consumer.
            Consumer.handleBuildCommand(
                {Cmd.getExecutable(),
                 {Cmd.getArguments().begin(), Cmd.getArguments().end()},
                 {}});
            return true;
          }

          // Insert -cc1 comand line options into Argv
          std::vector<std::string> Argv;
          Argv.push_back(Cmd.getExecutable());
          llvm::append_range(Argv, Cmd.getArguments());

          // Create an invocation that uses the underlying file
          // system to ensure that any file system requests that
          // are made by the driver do not go through the
          // dependency scanning filesystem.
          return createAndRunToolInvocation(std::move(Argv), Action, FS,
                                            PCHContainerOps, *Diags, Consumer);
        });
  }

  if (Success && !Action.hasScanned())
    Diags->Report(diag::err_fe_expected_compiler_job)
        << llvm::join(CommandLine, " ");

  // Ensure finish() is called even if we never reached ExecuteAction().
  if (!Action.hasDiagConsumerFinished())
    DC.finish();

  return Success && Action.hasScanned();
}

bool DependencyScanningWorker::computeDependencies(
    StringRef WorkingDirectory, const std::vector<std::string> &CommandLine,
    DependencyConsumer &Consumer, DependencyActionController &Controller,
    DiagnosticConsumer &DC, std::optional<llvm::MemoryBufferRef> TUBuffer) {
  // Reset what might have been modified in the previous worker invocation.
  BaseFS->setCurrentWorkingDirectory(WorkingDirectory);

  std::optional<std::vector<std::string>> ModifiedCommandLine;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> ModifiedFS;

  // If we're scanning based on a module name alone, we don't expect the client
  // to provide us with an input file. However, the driver really wants to have
  // one. Let's just make it up to make the driver happy.
  if (TUBuffer) {
    auto OverlayFS =
        llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(BaseFS);
    auto InMemoryFS =
        llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
    InMemoryFS->setCurrentWorkingDirectory(WorkingDirectory);
    auto InputPath = TUBuffer->getBufferIdentifier();
    InMemoryFS->addFile(
        InputPath, 0,
        llvm::MemoryBuffer::getMemBufferCopy(TUBuffer->getBuffer()));
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> InMemoryOverlay =
        InMemoryFS;

    // If we are using a CAS but not dependency CASFS, we need to provide the
    // fake input file in a CASProvidingFS for include-tree.
    if (CAS && !DepCASFS)
      InMemoryOverlay =
          llvm::cas::createCASProvidingFileSystem(CAS, std::move(InMemoryFS));

    OverlayFS->pushOverlay(InMemoryOverlay);
    ModifiedFS = OverlayFS;
    ModifiedCommandLine = CommandLine;
    ModifiedCommandLine->emplace_back(InputPath);
  }

  const std::vector<std::string> &FinalCommandLine =
      ModifiedCommandLine ? *ModifiedCommandLine : CommandLine;
  auto &FinalFS = ModifiedFS ? ModifiedFS : BaseFS;

  return scanDependencies(WorkingDirectory, FinalCommandLine, Consumer,
                          Controller, DC, FinalFS, /*ModuleName=*/std::nullopt);
}

bool DependencyScanningWorker::computeDependencies(
    StringRef WorkingDirectory, const std::vector<std::string> &CommandLine,
    DependencyConsumer &Consumer, DependencyActionController &Controller,
    DiagnosticConsumer &DC, StringRef ModuleName) {
  // Reset what might have been modified in the previous worker invocation.
  BaseFS->setCurrentWorkingDirectory(WorkingDirectory);

  // If we're scanning based on a module name alone, we don't expect the client
  // to provide us with an input file. However, the driver really wants to have
  // one. Let's just make it up to make the driver happy.
  auto OverlayFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(BaseFS);
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory(WorkingDirectory);
  SmallString<128> FakeInputPath;
  // TODO: We should retry the creation if the path already exists.
  llvm::sys::fs::createUniquePath(ModuleName + "-%%%%%%%%.input", FakeInputPath,
                                  /*MakeAbsolute=*/false);
  InMemoryFS->addFile(FakeInputPath, 0, llvm::MemoryBuffer::getMemBuffer(""));
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> InMemoryOverlay = InMemoryFS;

  // If we are using a CAS but not dependency CASFS, we need to provide the
  // fake input file in a CASProvidingFS for include-tree.
  if (CAS && !DepCASFS)
    InMemoryOverlay =
        llvm::cas::createCASProvidingFileSystem(CAS, std::move(InMemoryFS));

  OverlayFS->pushOverlay(InMemoryOverlay);
  auto ModifiedCommandLine = CommandLine;
  ModifiedCommandLine.emplace_back(FakeInputPath);

  return scanDependencies(WorkingDirectory, ModifiedCommandLine, Consumer,
                          Controller, DC, OverlayFS, ModuleName);
}

DependencyActionController::~DependencyActionController() {}

void DependencyScanningWorker::computeDependenciesFromCompilerInvocation(
    std::shared_ptr<CompilerInvocation> Invocation, StringRef WorkingDirectory,
    DependencyConsumer &DepsConsumer, DependencyActionController &Controller,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    bool DiagGenerationAsCompilation) {
  BaseFS->setCurrentWorkingDirectory(WorkingDirectory);

  // Adjust the invocation.
  auto &Frontend = Invocation->getFrontendOpts();
  Frontend.OutputFile = "/dev/null";
  Frontend.DisableFree = false;

  // // Reset dependency options.
  // Dependencies = DependencyOutputOptions();
  // Dependencies.IncludeSystemHeaders = true;
  // Dependencies.OutputFile = "/dev/null";

  // Make the output file path absolute relative to WorkingDirectory.
  std::string &DepFile = Invocation->getDependencyOutputOpts().OutputFile;
  if (!DepFile.empty() && !llvm::sys::path::is_absolute(DepFile)) {
    // FIXME: On Windows, WorkingDirectory is insufficient for making an
    // absolute path if OutputFile has a root name.
    llvm::SmallString<128> Path = StringRef(DepFile);
    llvm::sys::fs::make_absolute(WorkingDirectory, Path);
    DepFile = Path.str().str();
  }

  // FIXME: EmitDependencyFile should only be set when it's for a real
  // compilation.
  DependencyScanningAction Action(Service, WorkingDirectory, DepsConsumer,
                                  Controller, DepFS, DepCASFS, CacheFS,
                                  /*EmitDependencyFile=*/!DepFile.empty(),
                                  DiagGenerationAsCompilation, getCASOpts(),
                                  /*ModuleName=*/std::nullopt, VerboseOS);

  // Ignore result; we're just collecting dependencies.
  //
  // FIXME: will clients other than -cc1scand care?
  IntrusiveRefCntPtr<FileManager> ActiveFiles =
      new FileManager(Invocation->getFileSystemOpts(), BaseFS);
  (void)Action.runInvocation(std::move(Invocation), BaseFS, PCHContainerOps,
                             &DiagsConsumer);
}
