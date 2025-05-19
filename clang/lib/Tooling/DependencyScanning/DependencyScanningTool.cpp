//===- DependencyScanningTool.cpp - clang-scan-deps service ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningTool.h"
#include "clang/Basic/DiagnosticSerialization.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "llvm/TargetParser/Host.h"
#include <optional>

using namespace clang;
using namespace tooling;
using namespace dependencies;

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
  CallbackActionController Controller(LookupModuleOutput);
  llvm::Error Result = Worker.computeDependencies(CWD, CommandLine, Consumer,
                                                  Controller, TUBuffer);

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

class ScanningDependencyDirectivesGetter : public DependencyDirectivesGetter {
  DependencyScanningWorkerFilesystem *DepFS;

public:
  ScanningDependencyDirectivesGetter(FileManager &FileMgr) : DepFS(nullptr) {
    FileMgr.getVirtualFileSystem().visit([&](llvm::vfs::FileSystem &FS) {
      auto *DFS = llvm::dyn_cast<DependencyScanningWorkerFilesystem>(&FS);
      if (DFS) {
        assert(!DepFS && "Found multiple scanning VFSs");
        DepFS = DFS;
      }
    });
    assert(DepFS && "Did not find scanning VFS");
  }

  std::unique_ptr<DependencyDirectivesGetter>
  cloneFor(FileManager &FileMgr) override {
    return std::make_unique<ScanningDependencyDirectivesGetter>(FileMgr);
  }

  std::optional<ArrayRef<dependency_directives_scan::Directive>>
  operator()(FileEntryRef File) override {
    return DepFS->getDirectiveTokens(File.getName());
  }
};

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
  PrebuiltModuleListener(PrebuiltModuleFilesT &PrebuiltModuleFiles,
                         llvm::SmallVector<std::string> &NewModuleFiles,
                         PrebuiltModulesAttrsMap &PrebuiltModulesASTMap,
                         const HeaderSearchOptions &HSOpts,
                         const LangOptions &LangOpts, DiagnosticsEngine &Diags,
                         const ArrayRef<StringRef> StableDirs)
      : PrebuiltModuleFiles(PrebuiltModuleFiles),
        NewModuleFiles(NewModuleFiles),
        PrebuiltModulesASTMap(PrebuiltModulesASTMap), ExistingHSOpts(HSOpts),
        ExistingLangOpts(LangOpts), Diags(Diags), StableDirs(StableDirs) {}

  bool needsImportVisitation() const override { return true; }
  bool needsInputFileVisitation() override { return true; }
  bool needsSystemInputFileVisitation() override { return true; }

  /// Accumulate the modules are transitively depended on by the initial
  /// prebuilt module.
  void visitImport(StringRef ModuleName, StringRef Filename) override {
    if (PrebuiltModuleFiles.insert({ModuleName.str(), Filename.str()}).second)
      NewModuleFiles.push_back(Filename.str());

    auto PrebuiltMapEntry = PrebuiltModulesASTMap.try_emplace(Filename);
    PrebuiltModuleASTAttrs &PrebuiltModule = PrebuiltMapEntry.first->second;
    if (PrebuiltMapEntry.second)
      PrebuiltModule.setInStableDir(!StableDirs.empty());

    if (auto It = PrebuiltModulesASTMap.find(CurrentFile);
        It != PrebuiltModulesASTMap.end() && CurrentFile != Filename)
      PrebuiltModule.addDependent(It->getKey());
  }

  /// For each input file discovered, check whether it's external path is in a
  /// stable directory. Traversal is stopped if the current module is not
  /// considered stable.
  bool visitInputFile(StringRef FilenameAsRequested, StringRef Filename,
                      bool isSystem, bool isOverridden,
                      bool isExplicitModule) override {
    if (StableDirs.empty())
      return false;
    auto PrebuiltEntryIt = PrebuiltModulesASTMap.find(CurrentFile);
    if ((PrebuiltEntryIt == PrebuiltModulesASTMap.end()) ||
        (!PrebuiltEntryIt->second.isInStableDir()))
      return false;

    PrebuiltEntryIt->second.setInStableDir(
        isPathInStableDir(StableDirs, Filename));
    return PrebuiltEntryIt->second.isInStableDir();
  }

  /// Update which module that is being actively traversed.
  void visitModuleFile(StringRef Filename,
                       serialization::ModuleKind Kind) override {
    // If the CurrentFile is not
    // considered stable, update any of it's transitive dependents.
    auto PrebuiltEntryIt = PrebuiltModulesASTMap.find(CurrentFile);
    if ((PrebuiltEntryIt != PrebuiltModulesASTMap.end()) &&
        !PrebuiltEntryIt->second.isInStableDir())
      PrebuiltEntryIt->second.updateDependentsNotInStableDirs(
          PrebuiltModulesASTMap);
    CurrentFile = Filename;
  }

  /// Check the header search options for a given module when considering
  /// if the module comes from stable directories.
  bool ReadHeaderSearchOptions(const HeaderSearchOptions &HSOpts,
                               StringRef ModuleFilename,
                               StringRef SpecificModuleCachePath,
                               bool Complain) override {

    auto PrebuiltMapEntry = PrebuiltModulesASTMap.try_emplace(CurrentFile);
    PrebuiltModuleASTAttrs &PrebuiltModule = PrebuiltMapEntry.first->second;
    if (PrebuiltMapEntry.second)
      PrebuiltModule.setInStableDir(!StableDirs.empty());

    if (PrebuiltModule.isInStableDir())
      PrebuiltModule.setInStableDir(areOptionsInStableDir(StableDirs, HSOpts));

    return false;
  }

  /// Accumulate vfsoverlays used to build these prebuilt modules.
  bool ReadHeaderSearchPaths(const HeaderSearchOptions &HSOpts,
                             bool Complain) override {

    auto PrebuiltMapEntry = PrebuiltModulesASTMap.try_emplace(CurrentFile);
    PrebuiltModuleASTAttrs &PrebuiltModule = PrebuiltMapEntry.first->second;
    if (PrebuiltMapEntry.second)
      PrebuiltModule.setInStableDir(!StableDirs.empty());

    PrebuiltModule.setVFS(
        llvm::StringSet<>(llvm::from_range, HSOpts.VFSOverlayFiles));

    return checkHeaderSearchPaths(
        HSOpts, ExistingHSOpts, Complain ? &Diags : nullptr, ExistingLangOpts);
  }

private:
  PrebuiltModuleFilesT &PrebuiltModuleFiles;
  llvm::SmallVector<std::string> &NewModuleFiles;
  PrebuiltModulesAttrsMap &PrebuiltModulesASTMap;
  const HeaderSearchOptions &ExistingHSOpts;
  const LangOptions &ExistingLangOpts;
  DiagnosticsEngine &Diags;
  std::string CurrentFile;
  const ArrayRef<StringRef> StableDirs;
};

static bool visitPrebuiltModule(StringRef PrebuiltModuleFilename,
                                CompilerInstance &CI,
                                PrebuiltModuleFilesT &ModuleFiles,
                                PrebuiltModulesAttrsMap &PrebuiltModulesASTMap,
                                DiagnosticsEngine &Diags,
                                const ArrayRef<StringRef> StableDirs) {
  // List of module files to be processed.
  llvm::SmallVector<std::string> Worklist;

  PrebuiltModuleListener Listener(ModuleFiles, Worklist, PrebuiltModulesASTMap,
                                  CI.getHeaderSearchOpts(), CI.getLangOpts(),
                                  Diags, StableDirs);

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
  CallbackActionController Controller(LookupModuleOutput);
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
      CompilerInstance::createDiagnostics(FileMgr->getVirtualFileSystem(),
                                          DiagOpts2.release(), &DiagPrinter,
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
  CompilerInstance CI(std::move(Invocation), Worker.getPCHContainerOps(),
                      ModCache.get());

  // Now initialize the compiler instance.
  // Set hardcoded flags.
  CI.setBuildingModule(false);
  CI.getPreprocessorOpts().AllowPCHWithDifferentModulesCachePath = true;
  CI.getFrontendOpts().GenerateGlobalModuleIndex = false;
  CI.getFrontendOpts().UseGlobalModuleIndex = false;
  CI.getFrontendOpts().ModulesShareFileManager = true;
  CI.getHeaderSearchOpts().ModuleFormat = "raw";
  CI.getHeaderSearchOpts().ModulesIncludeVFSUsage =
      any(Worker.getService().getOptimizeArgs() & ScanningOptimizations::VFS);
  CI.getHeaderSearchOpts().ModulesStrictContextHash = true;
  CI.getHeaderSearchOpts().ModulesSerializeOnlyPreprocessor = true;
  CI.getHeaderSearchOpts().ModulesSkipDiagnosticOptions = true;
  CI.getHeaderSearchOpts().ModulesSkipHeaderSearchPaths = true;
  CI.getHeaderSearchOpts().ModulesSkipPragmaDiagnosticMappings = true;
  CI.getHeaderSearchOpts().ModulesForceValidateUserHeaders = false;
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
  auto DepFS = Worker.getDepFS();
  if (DepFS) {
    StringRef ModulesCachePath = CI.getHeaderSearchOpts().ModuleCachePath;

    DepFS->resetBypassedPathPrefix();
    if (!ModulesCachePath.empty())
      DepFS->setBypassedPathPrefix(ModulesCachePath);

    CI.setDependencyDirectivesGetter(
        std::make_unique<ScanningDependencyDirectivesGetter>(*FileMgr));
  }

  CI.createSourceManager(*FileMgr);
  PrebuiltModulesAttrsMap PrebuiltModulesASTMap;

  llvm::SmallVector<StringRef> StableDirs;
  const StringRef Sysroot = CI.getHeaderSearchOpts().Sysroot;
  if (!Sysroot.empty() && (llvm::sys::path::root_directory(Sysroot) != Sysroot))
    StableDirs = {Sysroot, CI.getHeaderSearchOpts().ResourceDir};

  if (!CI.getPreprocessorOpts().ImplicitPCHInclude.empty())
    if (visitPrebuiltModule(CI.getPreprocessorOpts().ImplicitPCHInclude, CI,
                            CI.getHeaderSearchOpts().PrebuiltModuleFiles,
                            PrebuiltModulesASTMap, CI.getDiagnostics(),
                            StableDirs))
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
      Worker.getService(), std::move(Opts), CI, Consumer, Controller, CICopy,
      std::move(PrebuiltModulesASTMap), StableDirs);

  CI.addDependencyCollector(MDC);
  std::unique_ptr<FrontendAction> Action =
      std::make_unique<GetDependenciesByModuleNameAction>(ModuleName);
  auto InputFile = CI.getFrontendOpts().Inputs.begin();
  CI.createTarget();
  Action->BeginSourceFile(CI, *InputFile);
  Preprocessor &PP = CI.getPreprocessor();
  SourceManager &SM = PP.getSourceManager();
  FileID MainFileID = SM.getMainFileID();
  SourceLocation FileStart = SM.getLocForStartOfFile(MainFileID);
  SmallVector<IdentifierLoc, 2> Path;
  IdentifierInfo *ModuleID = PP.getIdentifierInfo(ModuleName);
  Path.emplace_back(FileStart, ModuleID);
  auto ModResult = CI.loadModule(FileStart, Path, Module::Hidden, false);
  PPCallbacks *CB = PP.getPPCallbacks();
  CB->moduleImport(SourceLocation(), Path, ModResult);
  CB->EndOfMainFile();
  ////////////////////////////////////////////////////////////////////////////

  // llvm::Error Result = Worker.computeDependencies(CWD, CommandLine, Consumer,
  //                                                 Controller, ModuleName);
  // if (Result)
  //   return std::move(Result);
  // return Consumer.takeModuleGraphDeps();

  return Consumer.takeModuleGraphDeps();
}

TranslationUnitDeps FullDependencyConsumer::takeTranslationUnitDeps() {
  TranslationUnitDeps TU;

  TU.ID.ContextHash = std::move(ContextHash);
  TU.FileDeps = std::move(Dependencies);
  TU.PrebuiltModuleDeps = std::move(PrebuiltModuleDeps);
  TU.Commands = std::move(Commands);

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
