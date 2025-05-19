//===- DependencyScanningTool.cpp - clang-scan-deps service ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningTool.h"
#include "CachingActions.h"
#include "clang/Basic/DiagnosticSerialization.h"
#include "clang/CAS/IncludeTree.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/Tooling/DependencyScanning/ScanAndUpdateArgs.h"
#include "llvm/CAS/ObjectStore.h"
#include "llvm/TargetParser/Host.h"
#include <optional>

using namespace clang;
using namespace tooling;
using namespace dependencies;
using llvm::Error;

DependencyScanningTool::DependencyScanningTool(
    DependencyScanningService &Service,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS)
    : Worker(Service, std::move(FS)) {}

namespace {
/// Prints out all of the gathered dependencies into a string.
class MakeDependencyPrinterConsumer : public DependencyConsumer {
public:
  void handleBuildCommand(Command) override {}

  void
  handleDependencyOutputOpts(const DependencyOutputOptions &Opts) override {
    this->Opts = std::make_unique<DependencyOutputOptions>(Opts);
  }

  void handleFileDependency(StringRef File) override {
    Dependencies.push_back(std::string(File));
  }

  // These are ignored for the make format as it can't support the full
  // set of deps, and handleFileDependency handles enough for implicitly
  // built modules to work.
  void handlePrebuiltModuleDependency(PrebuiltModuleDep PMD) override {}
  void handleModuleDependency(ModuleDeps MD) override {}
  void handleDirectModuleDependency(ModuleID ID) override {}
  void handleContextHash(std::string Hash) override {}

  void printDependencies(std::string &S) {
    assert(Opts && "Handled dependency output options.");

    class DependencyPrinter : public DependencyFileGenerator {
    public:
      DependencyPrinter(DependencyOutputOptions &Opts,
                        ArrayRef<std::string> Dependencies)
          : DependencyFileGenerator(Opts) {
        for (const auto &Dep : Dependencies)
          addDependency(Dep);
      }

      void printDependencies(std::string &S) {
        llvm::raw_string_ostream OS(S);
        outputDependencyFile(OS);
      }
    };

    DependencyPrinter Generator(*Opts, Dependencies);
    Generator.printDependencies(S);
  }

protected:
  std::unique_ptr<DependencyOutputOptions> Opts;
  std::vector<std::string> Dependencies;
};
} // anonymous namespace

llvm::Expected<std::string> DependencyScanningTool::getDependencyFile(
    const std::vector<std::string> &CommandLine, StringRef CWD) {
  MakeDependencyPrinterConsumer Consumer;
  CallbackActionController Controller(nullptr);
  auto Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, Controller);
  if (Result)
    return std::move(Result);
  std::string Output;
  Consumer.printDependencies(Output);
  return Output;
}

namespace {
class EmptyDependencyConsumer : public DependencyConsumer {
  void
  handleDependencyOutputOpts(const DependencyOutputOptions &Opts) override {}

  void handleFileDependency(StringRef Filename) override {}

  void handlePrebuiltModuleDependency(PrebuiltModuleDep PMD) override {}

  void handleModuleDependency(ModuleDeps MD) override {}

  void handleDirectModuleDependency(ModuleID ID) override {}

  void handleContextHash(std::string Hash) override {}
};

/// Returns a CAS tree containing the dependencies.
class GetDependencyTree : public EmptyDependencyConsumer {
public:
  void handleCASFileSystemRootID(std::string ID) override {
    CASFileSystemRootID = ID;
  }

  Expected<llvm::cas::ObjectProxy> getTree() {
    if (CASFileSystemRootID) {
      auto ID = FS.getCAS().parseID(*CASFileSystemRootID);
      if (!ID)
        return ID.takeError();
      return FS.getCAS().getProxy(*ID);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to get casfs");
  }

  GetDependencyTree(llvm::cas::CachingOnDiskFileSystem &FS) : FS(FS) {}

private:
  llvm::cas::CachingOnDiskFileSystem &FS;
  std::optional<std::string> CASFileSystemRootID;
};

/// Returns an IncludeTree containing the dependencies.
class GetIncludeTree : public EmptyDependencyConsumer {
public:
  void handleIncludeTreeID(std::string ID) override { IncludeTreeID = ID; }

  Expected<cas::IncludeTreeRoot> getIncludeTree() {
    if (IncludeTreeID) {
      auto ID = DB.parseID(*IncludeTreeID);
      if (!ID)
        return ID.takeError();
      auto Ref = DB.getReference(*ID);
      if (!Ref)
        return llvm::createStringError(
            llvm::inconvertibleErrorCode(),
            llvm::Twine("missing expected include-tree ") + ID->toString());
      return cas::IncludeTreeRoot::get(DB, *Ref);
    }
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to get include-tree");
  }

  GetIncludeTree(cas::ObjectStore &DB) : DB(DB) {}

private:
  cas::ObjectStore &DB;
  std::optional<std::string> IncludeTreeID;
};
}

llvm::Expected<llvm::cas::ObjectProxy>
DependencyScanningTool::getDependencyTree(
    const std::vector<std::string> &CommandLine, StringRef CWD) {
  GetDependencyTree Consumer(*Worker.getCASFS());
  auto Controller = createCASFSActionController(nullptr, *Worker.getCASFS());
  auto Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, *Controller);
  if (Result)
    return std::move(Result);
  return Consumer.getTree();
}

llvm::Expected<llvm::cas::ObjectProxy>
DependencyScanningTool::getDependencyTreeFromCompilerInvocation(
    std::shared_ptr<CompilerInvocation> Invocation, StringRef CWD,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    bool DiagGenerationAsCompilation) {
  GetDependencyTree Consumer(*Worker.getCASFS());
  auto Controller = createCASFSActionController(nullptr, *Worker.getCASFS());
  Worker.computeDependenciesFromCompilerInvocation(
      std::move(Invocation), CWD, Consumer, *Controller, DiagsConsumer,
      VerboseOS, DiagGenerationAsCompilation);
  return Consumer.getTree();
}

Expected<cas::IncludeTreeRoot> DependencyScanningTool::getIncludeTree(
    cas::ObjectStore &DB, const std::vector<std::string> &CommandLine,
    StringRef CWD, LookupModuleOutputCallback LookupModuleOutput) {
  GetIncludeTree Consumer(DB);
  auto Controller = createIncludeTreeActionController(LookupModuleOutput, DB);
  llvm::Error Result =
      Worker.computeDependencies(CWD, CommandLine, Consumer, *Controller);
  if (Result)
    return std::move(Result);
  return Consumer.getIncludeTree();
}

Expected<cas::IncludeTreeRoot>
DependencyScanningTool::getIncludeTreeFromCompilerInvocation(
    cas::ObjectStore &DB, std::shared_ptr<CompilerInvocation> Invocation,
    StringRef CWD, LookupModuleOutputCallback LookupModuleOutput,
    DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
    bool DiagGenerationAsCompilation) {
  GetIncludeTree Consumer(DB);
  auto Controller = createIncludeTreeActionController(LookupModuleOutput, DB);
  Worker.computeDependenciesFromCompilerInvocation(
      std::move(Invocation), CWD, Consumer, *Controller, DiagsConsumer,
      VerboseOS, DiagGenerationAsCompilation);
  return Consumer.getIncludeTree();
}

llvm::Expected<P1689Rule> DependencyScanningTool::getP1689ModuleDependencyFile(
    const CompileCommand &Command, StringRef CWD, std::string &MakeformatOutput,
    std::string &MakeformatOutputPath) {
  class P1689ModuleDependencyPrinterConsumer
      : public MakeDependencyPrinterConsumer {
  public:
    P1689ModuleDependencyPrinterConsumer(P1689Rule &Rule,
                                         const CompileCommand &Command)
        : Filename(Command.Filename), Rule(Rule) {
      Rule.PrimaryOutput = Command.Output;
    }

    void handleProvidedAndRequiredStdCXXModules(
        std::optional<P1689ModuleInfo> Provided,
        std::vector<P1689ModuleInfo> Requires) override {
      Rule.Provides = Provided;
      if (Rule.Provides)
        Rule.Provides->SourcePath = Filename.str();
      Rule.Requires = Requires;
    }

    StringRef getMakeFormatDependencyOutputPath() {
      if (Opts->OutputFormat != DependencyOutputFormat::Make)
        return {};
      return Opts->OutputFile;
    }

  private:
    StringRef Filename;
    P1689Rule &Rule;
  };

  class P1689ActionController : public DependencyActionController {
  public:
    // The lookupModuleOutput is for clang modules. P1689 format don't need it.
    std::string lookupModuleOutput(const ModuleDeps &,
                                   ModuleOutputKind Kind) override {
      return "";
    }
  };

  P1689Rule Rule;
  P1689ModuleDependencyPrinterConsumer Consumer(Rule, Command);
  P1689ActionController Controller;
  auto Result = Worker.computeDependencies(CWD, Command.CommandLine, Consumer,
                                           Controller);
  if (Result)
    return std::move(Result);

  MakeformatOutputPath = Consumer.getMakeFormatDependencyOutputPath();
  if (!MakeformatOutputPath.empty())
    Consumer.printDependencies(MakeformatOutput);
  return Rule;
}

llvm::Expected<TranslationUnitDeps>
DependencyScanningTool::getTranslationUnitDependencies(
    const std::vector<std::string> &CommandLine, StringRef CWD,
    const llvm::DenseSet<ModuleID> &AlreadySeen,
    LookupModuleOutputCallback LookupModuleOutput,
    std::optional<llvm::MemoryBufferRef> TUBuffer) {
  FullDependencyConsumer Consumer(AlreadySeen);
  auto Controller = createActionController(LookupModuleOutput);
  llvm::Error Result = Worker.computeDependencies(CWD, CommandLine, Consumer,
                                                  *Controller, TUBuffer);
  if (Result)
    return std::move(Result);
  return Consumer.takeTranslationUnitDeps();
}

////////////////////////////////////////////////////////////////////////////
// Prototype: helpers
// Copied from DependencyScanningWorker.cpp
static void sanitizeDiagOpts(DiagnosticOptions &DiagOpts) {
  // Don't print 'X warnings and Y errors generated'.
  DiagOpts.ShowCarets = false;
  // Don't write out diagnostic file.
  DiagOpts.DiagnosticSerializationFile.clear();
  // Don't emit warnings except for scanning specific warnings.
  // TODO: It would be useful to add a more principled way to ignore all
  //       warnings that come from source code. The issue is that we need to
  //       ignore warnings that could be surpressed by
  //       `#pragma clang diagnostic`, while still allowing some scanning
  //       warnings for things we're not ready to turn into errors yet.
  //       See `test/ClangScanDeps/diagnostic-pragmas.c` for an example.
  llvm::erase_if(DiagOpts.Warnings, [](StringRef Warning) {
    return llvm::StringSwitch<bool>(Warning)
        .Cases("pch-vfs-diff", "error=pch-vfs-diff", false)
        .StartsWith("no-error=", false)
        .Default(true);
  });
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

static std::optional<StringRef> getSimpleMacroName(StringRef Macro) {
  StringRef Name = Macro.split("=").first.ltrim(" \t");
  std::size_t I = 0;

  auto FinishName = [&]() -> std::optional<StringRef> {
    StringRef SimpleName = Name.slice(0, I);
    if (SimpleName.empty())
      return std::nullopt;
    return SimpleName;
  };

  for (; I != Name.size(); ++I) {
    switch (Name[I]) {
    case '(': // Start of macro parameter list
    case ' ': // End of macro name
    case '\t':
      return FinishName();
    case '_':
      continue;
    default:
      if (llvm::isAlnum(Name[I]))
        continue;
      return std::nullopt;
    }
  }
  return FinishName();
}

static void canonicalizeDefines(PreprocessorOptions &PPOpts) {
  using MacroOpt = std::pair<StringRef, std::size_t>;
  std::vector<MacroOpt> SimpleNames;
  SimpleNames.reserve(PPOpts.Macros.size());
  std::size_t Index = 0;
  for (const auto &M : PPOpts.Macros) {
    auto SName = getSimpleMacroName(M.first);
    // Skip optimizing if we can't guarantee we can preserve relative order.
    if (!SName)
      return;
    SimpleNames.emplace_back(*SName, Index);
    ++Index;
  }

  llvm::stable_sort(SimpleNames, llvm::less_first());
  // Keep the last instance of each macro name by going in reverse
  auto NewEnd = std::unique(
      SimpleNames.rbegin(), SimpleNames.rend(),
      [](const MacroOpt &A, const MacroOpt &B) { return A.first == B.first; });
  SimpleNames.erase(SimpleNames.begin(), NewEnd.base());

  // Apply permutation.
  decltype(PPOpts.Macros) NewMacros;
  NewMacros.reserve(SimpleNames.size());
  for (std::size_t I = 0, E = SimpleNames.size(); I != E; ++I) {
    std::size_t OriginalIndex = SimpleNames[I].second;
    // We still emit undefines here as they may be undefining a predefined macro
    NewMacros.push_back(std::move(PPOpts.Macros[OriginalIndex]));
  }
  std::swap(PPOpts.Macros, NewMacros);
}

static bool checkHeaderSearchPaths(const HeaderSearchOptions &HSOpts,
                                   const HeaderSearchOptions &ExistingHSOpts,
                                   DiagnosticsEngine *Diags,
                                   const LangOptions &LangOpts) {
  if (LangOpts.Modules) {
    if (HSOpts.VFSOverlayFiles != ExistingHSOpts.VFSOverlayFiles) {
      if (Diags) {
        Diags->Report(diag::warn_pch_vfsoverlay_mismatch);
        auto VFSNote = [&](int Type, ArrayRef<std::string> VFSOverlays) {
          if (VFSOverlays.empty()) {
            Diags->Report(diag::note_pch_vfsoverlay_empty) << Type;
          } else {
            std::string Files = llvm::join(VFSOverlays, "\n");
            Diags->Report(diag::note_pch_vfsoverlay_files) << Type << Files;
          }
        };
        VFSNote(0, HSOpts.VFSOverlayFiles);
        VFSNote(1, ExistingHSOpts.VFSOverlayFiles);
      }
    }
  }
  return false;
}

using PrebuiltModuleFilesT = decltype(HeaderSearchOptions::PrebuiltModuleFiles);
/// A listener that collects the imported modules and the input
/// files. While visiting, collect vfsoverlays and file inputs that determine
/// whether prebuilt modules fully resolve in stable directories.
class PrebuiltModuleListener : public ASTReaderListener {
public:
  PrebuiltModuleListener(CompilerInstance &CI,
                         PrebuiltModuleFilesT &PrebuiltModuleFiles,
                         llvm::SmallVector<std::string> &NewModuleFiles,
                         PrebuiltModuleVFSMapT &PrebuiltModuleVFSMap,
                         DiagnosticsEngine &Diags)
      : CI(CI), PrebuiltModuleFiles(PrebuiltModuleFiles),
        NewModuleFiles(NewModuleFiles),
        PrebuiltModuleVFSMap(PrebuiltModuleVFSMap), Diags(Diags) {}

  bool needsImportVisitation() const override { return true; }

  void visitImport(StringRef ModuleName, StringRef Filename) override {
    if (PrebuiltModuleFiles.insert({ModuleName.str(), Filename.str()}).second)
      NewModuleFiles.push_back(Filename.str());
  }

  void visitModuleFile(StringRef Filename,
                       serialization::ModuleKind Kind) override {
    CurrentFile = Filename;
  }

  bool ReadHeaderSearchPaths(const HeaderSearchOptions &HSOpts,
                             bool Complain) override {
    std::vector<std::string> VFSOverlayFiles = HSOpts.VFSOverlayFiles;
    PrebuiltModuleVFSMap.insert(
        {CurrentFile, llvm::StringSet<>(VFSOverlayFiles)});
    return checkHeaderSearchPaths(
        HSOpts, CI.getHeaderSearchOpts(), Complain ? &Diags : nullptr, CI.getLangOpts());
  }

  bool readModuleCacheKey(StringRef ModuleName, StringRef Filename,
                          StringRef CacheKey) override {
    CI.getFrontendOpts().ModuleCacheKeys.emplace_back(std::string(Filename),
                                                      std::string(CacheKey));
    // FIXME: add name/path of the importing module?
    return CI.addCachedModuleFile(Filename, CacheKey, "imported module");
  }

private:
  CompilerInstance &CI;
  PrebuiltModuleFilesT &PrebuiltModuleFiles;
  llvm::SmallVector<std::string> &NewModuleFiles;
  PrebuiltModuleVFSMapT &PrebuiltModuleVFSMap;
  DiagnosticsEngine &Diags;
  std::string CurrentFile;
};

static bool visitPrebuiltModule(StringRef PrebuiltModuleFilename,
                                CompilerInstance &CI,
                                PrebuiltModuleFilesT &ModuleFiles,
                                PrebuiltModuleVFSMapT &PrebuiltModuleVFSMap,
                                DiagnosticsEngine &Diags) {
  // List of module files to be processed.
  llvm::SmallVector<std::string> Worklist;
  PrebuiltModuleListener Listener(CI, ModuleFiles, Worklist,
                                  PrebuiltModuleVFSMap, Diags);

  Listener.visitModuleFile(PrebuiltModuleFilename,
                           serialization::MK_ExplicitModule);
  if (ASTReader::readASTFileControlBlock(
          PrebuiltModuleFilename, CI.getFileManager(), CI.getModuleCache(),
          CI.getPCHContainerReader(),
          /*FindModuleFileExtensions=*/false, Listener,
          /*ValidateDiagnosticOptions=*/false, ASTReader::ARR_OutOfDate))
    return true;

  while (!Worklist.empty()) {
    Listener.visitModuleFile(Worklist.back(), serialization::MK_ExplicitModule);
    if (ASTReader::readASTFileControlBlock(
            Worklist.pop_back_val(), CI.getFileManager(), CI.getModuleCache(),
            CI.getPCHContainerReader(),
            /*FindModuleFileExtensions=*/false, Listener,
            /*ValidateDiagnosticOptions=*/false))
      return true;
  }
  return false;
}

static std::string makeObjFileName(StringRef FileName) {
  SmallString<128> ObjFileName(FileName);
  llvm::sys::path::replace_extension(ObjFileName, "o");
  return std::string(ObjFileName);
}

static std::string
deduceDepTarget(const std::string &OutputFile,
                const SmallVectorImpl<FrontendInputFile> &InputFiles) {
  if (OutputFile != "-")
    return OutputFile;

  if (InputFiles.empty() || !InputFiles.front().isFile())
    return "clang-scan-deps\\ dependency";

  return makeObjFileName(InputFiles.front().getFile());
}
////////////////////////////////////////////////////////////////////////////

llvm::Expected<ModuleDepsGraph> DependencyScanningTool::getModuleDependencies(
    StringRef ModuleName, const std::vector<std::string> &CommandLine,
    StringRef CWD, const llvm::DenseSet<ModuleID> &AlreadySeen,
    LookupModuleOutputCallback LookupModuleOutput) {
  FullDependencyConsumer Consumer(AlreadySeen);
  auto Controller = createActionController(LookupModuleOutput);
  //llvm::Error Result = Worker.computeDependencies(CWD, CommandLine, Consumer,
  //                                                *Controller, ModuleName);
  //if (Result)
  //  return std::move(Result);

  ////////////////////////////////////////////////////////////////////////////
  // Try initialize the compiler instance and eveyrthing else directly here.
  // We need the following:
  // - DependencySCanningWorkerFilesystem
  // - Working directory
  //   - we have this as CWD.
  // - DependencyConsumer
  //   - we have this as Consumer.
  // - DependencyActionController
  //   - we have this ac Controller.
  // Prepare the VFS and the fake file.
  auto BaseFS = Worker.getVFSPtr();
  BaseFS->setCurrentWorkingDirectory(CWD);
  auto OverlayFS =
      llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(BaseFS);
  auto InMemoryFS = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  InMemoryFS->setCurrentWorkingDirectory(CWD);
  SmallString<128> FakeInputPath;
  // TODO: We should retry the creation if the path already exists.
  llvm::sys::fs::createUniquePath(ModuleName + "-%%%%%%%%.input", FakeInputPath,
                                  /*MakeAbsolute=*/false);
  InMemoryFS->addFile(FakeInputPath, 0, llvm::MemoryBuffer::getMemBuffer(""));
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> InMemoryOverlay = InMemoryFS;
  OverlayFS->pushOverlay(InMemoryOverlay);
  // - FileManager
  auto FileMgr =
      llvm::makeIntrusiveRefCnt<FileManager>(FileSystemOptions{}, OverlayFS);
  std::vector<std::string> CMD(CommandLine);
  CMD.emplace_back(FakeInputPath);
  // - DiagnosticsConsumer
  std::string DiagnosticOutput;
  llvm::raw_string_ostream DiagnosticsOS(DiagnosticOutput);
  auto DiagOpts = createDiagOptions(CommandLine);
  TextDiagnosticPrinter DiagPrinter(DiagnosticsOS, DiagOpts.release());

  std::vector<const char *> CCommandLine(CMD.size(), nullptr);
  llvm::transform(CMD, CCommandLine.begin(),
                  [](const std::string &Str) { return Str.c_str(); });
  auto DiagOpts2 = CreateAndPopulateDiagOpts(CCommandLine);
  sanitizeDiagOpts(*DiagOpts2);
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
      CompilerInstance::createDiagnostics(DiagOpts2.release(), &DiagPrinter,
                                          /*ShouldOwnClient=*/false);

  // - CompilerInvocation
  // 1. Takes the commandline and turns it into a compilation from driver.
  //    Similar to the code in forEachDriverJob.
  SmallVector<const char *, 256> Argv;
  Argv.reserve(CMD.size());
  for (const std::string &Arg : CMD)
    Argv.push_back(Arg.c_str());

  std::unique_ptr<driver::Driver> Driver = std::make_unique<driver::Driver>(
      Argv[0], llvm::sys::getDefaultTargetTriple(), *Diags,
      "clang LLVM compiler", OverlayFS);
  Driver->setTitle("clang_based_tool");
  const std::unique_ptr<driver::Compilation> Compilation(
      Driver->BuildCompilation(llvm::ArrayRef(Argv)));
  assert(Compilation && "Have to have a good compilation");
  if (Compilation->containsError()) {
    return llvm::make_error<llvm::StringError>(DiagnosticsOS.str(),
                                               llvm::inconvertibleErrorCode());
  }
  const driver::Command &Command = *(Compilation->getJobs().begin());
  std::vector<std::string> Argv1;
  Argv1.push_back(Command.getExecutable());
  llvm::append_range(Argv1, Command.getArguments());
  // 2. Create the invocation similar to the code in ToolInvocation::run
  assert(Argv1.size() >= 2 && Argv1[1] == "-cc1" && "Incorrect commands!");
  llvm::opt::ArgStringList Argv2;
  for (const std::string &Str : Argv1)
    Argv2.push_back(Str.c_str());
  const char *const BinaryName = Argv2[0];
  ArrayRef<const char *> CC1Args = ArrayRef(Argv2).drop_front();
  std::unique_ptr<CompilerInvocation> Invocation =
      std::make_unique<CompilerInvocation>();
  CompilerInvocation::CreateFromArgs(*Invocation, CC1Args, *Diags, BinaryName);
  // We set DisableFree to true in DependencyScanningAction::runInvocation.
  // Might as well set them here correctly.
  Invocation->getFrontendOpts().DisableFree = true;
  Invocation->getCodeGenOpts().DisableFree = true;
  canonicalizeDefines(Invocation->getPreprocessorOpts());
  // -> CompilerInstance
  CompilerInvocation CICopy(*Invocation);
  auto ModCache =
      makeInProcessModuleCache(Worker.getService().getModuleCacheEntries());
  CompilerInstance CI(Worker.getPCHContainerOps(), ModCache.get());
  CI.setInvocation(std::move(Invocation));

  auto DepCASFS = Worker.getCASFSPtr();

  // Now initialize the compiler instance.
  // Set hardcoded flags.
  CI.setBuildingModule(false);
  CI.getPreprocessorOpts().AllowPCHWithDifferentModulesCachePath = true;
  CI.getFrontendOpts().GenerateGlobalModuleIndex = false;
  CI.getFrontendOpts().UseGlobalModuleIndex = false;
  CI.getFrontendOpts().ModulesShareFileManager = DepCASFS? false : true;
  CI.getHeaderSearchOpts().ModuleFormat = "raw";
  CI.getHeaderSearchOpts().ModulesIncludeVFSUsage =
      any(Worker.getService().getOptimizeArgs() & ScanningOptimizations::VFS);
  CI.getHeaderSearchOpts().ModulesStrictContextHash = true;
  CI.getHeaderSearchOpts().ModulesSerializeOnlyPreprocessor = true;
  CI.getHeaderSearchOpts().ModulesSkipDiagnosticOptions = true;
  CI.getHeaderSearchOpts().ModulesSkipHeaderSearchPaths = true;
  CI.getHeaderSearchOpts().ModulesSkipPragmaDiagnosticMappings = true;
  CI.getPreprocessorOpts().ModulesCheckRelocated = false;

  // Set time stamp.
  if (CI.getHeaderSearchOpts().ModulesValidateOncePerBuildSession)
    CI.getHeaderSearchOpts().BuildSessionTimestamp =
        Worker.getService().getBuildSessionTimestamp();

  CI.setDiagnostics(Diags.get());

  auto FS =
      createVFSFromCompilerInvocation(CI.getInvocation(), CI.getDiagnostics(),
                                      FileMgr->getVirtualFileSystemPtr());
  CI.createFileManager(FS);
  CI.createSourceManager(*FileMgr);
  auto DepFS = Worker.getDepFS();
  if (DepFS) {
    CI.getPreprocessorOpts().DependencyDirectivesForFile =
          [LocalDepFS = DepFS](FileEntryRef File)
          -> std::optional<ArrayRef<dependency_directives_scan::Directive>> {
        if (llvm::ErrorOr<EntryRef> Entry =
                LocalDepFS->getOrCreateFileSystemEntry(File.getName()))
          if (LocalDepFS->ensureDirectiveTokensArePopulated(*Entry))
            return Entry->getDirectiveTokens();
        return std::nullopt;
      };
  }

  if (DepCASFS)
    CI.getPreprocessorOpts().DependencyDirectivesForFile =
        [LocalDepCASFS = DepCASFS](FileEntryRef File) {
          return LocalDepCASFS->getDirectiveTokens(File.getName());
        };

  PrebuiltModuleVFSMapT PrebuiltModuleVFSMap;

  llvm::SmallVector<StringRef> StableDirs;
  const StringRef Sysroot = CI.getHeaderSearchOpts().Sysroot;
  if (!Sysroot.empty() && (llvm::sys::path::root_directory(Sysroot) != Sysroot))
    StableDirs = {Sysroot, CI.getHeaderSearchOpts().ResourceDir};

  if (!CI.getPreprocessorOpts().ImplicitPCHInclude.empty())
    if (visitPrebuiltModule(CI.getPreprocessorOpts().ImplicitPCHInclude, CI,
                            CI.getHeaderSearchOpts().PrebuiltModuleFiles,
                            PrebuiltModuleVFSMap, CI.getDiagnostics()))
      return llvm::make_error<llvm::StringError>(
          "Tmp error", llvm::inconvertibleErrorCode());

  auto Opts = std::make_unique<DependencyOutputOptions>();
  std::swap(*Opts, CI.getInvocation().getDependencyOutputOpts());
  // We need at least one -MT equivalent for the generator of make dependency
  // files to work.
  if (Opts->Targets.empty())
    Opts->Targets = {deduceDepTarget(CI.getFrontendOpts().OutputFile,
                                     CI.getFrontendOpts().Inputs)};
  Opts->IncludeSystemHeaders = true;

  auto MDC = std::make_shared<ModuleDepCollector>(
      Worker.getService(), std::move(Opts), CI, Consumer, *Controller, CICopy,
      std::move(PrebuiltModuleVFSMap));

  CI.addDependencyCollector(MDC);
  std::unique_ptr<FrontendAction> Action =
      std::make_unique<GetDependenciesByModuleNameAction>(ModuleName);
  auto InputFile = CI.getFrontendOpts().Inputs.begin();
  CI.createTarget();
  Action->BeginSourceFile(CI, *InputFile);
  Preprocessor &PP = CI.getPreprocessor();
  SourceManager &SM = PP.getSourceManager();
  FileID MainFileID = SM.getMainFileID();
  PP.EnterSourceFile(MainFileID, nullptr, SourceLocation());
  SourceLocation FileStart = SM.getLocForStartOfFile(MainFileID);
  SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Path;
  IdentifierInfo *ModuleID = PP.getIdentifierInfo(ModuleName);
  Path.push_back(std::make_pair(ModuleID, FileStart));
  auto ModResult = CI.loadModule(FileStart, Path, Module::Hidden, false);
  PPCallbacks *CB = PP.getPPCallbacks();
  CB->moduleImport(SourceLocation(), Path, ModResult);
  CB->EndOfMainFile();
  
  return Consumer.takeModuleGraphDeps();
  ////////////////////////////////////////////////////////////////////////////
}

TranslationUnitDeps FullDependencyConsumer::takeTranslationUnitDeps() {
  TranslationUnitDeps TU;

  TU.ID.ContextHash = std::move(ContextHash);
  TU.FileDeps = std::move(Dependencies);
  TU.PrebuiltModuleDeps = std::move(PrebuiltModuleDeps);
  TU.Commands = std::move(Commands);
  TU.CASFileSystemRootID = std::move(CASFileSystemRootID);
  TU.IncludeTreeID = std::move(IncludeTreeID);

  for (auto &&M : ClangModuleDeps) {
    auto &MD = M.second;
    // TODO: Avoid handleModuleDependency even being called for modules
    //   we've already seen.
    if (AlreadySeen.count(M.first))
      continue;
    TU.ModuleGraph.push_back(std::move(MD));
  }
  TU.ClangModuleDeps = std::move(DirectModuleDeps);

  return TU;
}

ModuleDepsGraph FullDependencyConsumer::takeModuleGraphDeps() {
  ModuleDepsGraph ModuleGraph;

  for (auto &&M : ClangModuleDeps) {
    auto &MD = M.second;
    // TODO: Avoid handleModuleDependency even being called for modules
    //   we've already seen.
    if (AlreadySeen.count(M.first))
      continue;
    ModuleGraph.push_back(std::move(MD));
  }

  return ModuleGraph;
}

CallbackActionController::~CallbackActionController() {}

std::unique_ptr<DependencyActionController>
DependencyScanningTool::createActionController(
    DependencyScanningWorker &Worker,
    LookupModuleOutputCallback LookupModuleOutput) {
  if (Worker.getScanningFormat() == ScanningOutputFormat::FullIncludeTree)
    return createIncludeTreeActionController(LookupModuleOutput,
                                             *Worker.getCAS());
  if (auto CacheFS = Worker.getCASFS())
    return createCASFSActionController(LookupModuleOutput, *CacheFS);
  return std::make_unique<CallbackActionController>(LookupModuleOutput);
}

std::unique_ptr<DependencyActionController>
DependencyScanningTool::createActionController(
    LookupModuleOutputCallback LookupModuleOutput) {
  return createActionController(Worker, std::move(LookupModuleOutput));
}
