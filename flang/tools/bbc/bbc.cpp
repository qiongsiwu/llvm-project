//===- bbc.cpp - Burnside Bridge Compiler -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Coding style: https://mlir.llvm.org/getting_started/DeveloperGuide/
//
//===----------------------------------------------------------------------===//
///
/// This is a tool for translating Fortran sources to the FIR dialect of MLIR.
///
//===----------------------------------------------------------------------===//

#include "flang/Frontend/CodeGenOptions.h"
#include "flang/Frontend/TargetOptions.h"
#include "flang/Lower/Bridge.h"
#include "flang/Lower/PFTBuilder.h"
#include "flang/Lower/Support/Verifier.h"
#include "flang/Optimizer/Dialect/Support/FIRContext.h"
#include "flang/Optimizer/Dialect/Support/KindMapping.h"
#include "flang/Optimizer/Support/InitFIR.h"
#include "flang/Optimizer/Support/InternalNames.h"
#include "flang/Optimizer/Support/Utils.h"
#include "flang/Optimizer/Transforms/Passes.h"
#include "flang/Parser/characters.h"
#include "flang/Parser/dump-parse-tree.h"
#include "flang/Parser/message.h"
#include "flang/Parser/parse-tree-visitor.h"
#include "flang/Parser/parse-tree.h"
#include "flang/Parser/parsing.h"
#include "flang/Parser/provenance.h"
#include "flang/Parser/unparse.h"
#include "flang/Semantics/expression.h"
#include "flang/Semantics/runtime-type-info.h"
#include "flang/Semantics/semantics.h"
#include "flang/Semantics/unparse-with-symbols.h"
#include "flang/Support/Fortran-features.h"
#include "flang/Support/LangOptions.h"
#include "flang/Support/OpenMP-features.h"
#include "flang/Support/Version.h"
#include "flang/Support/default-kinds.h"
#include "flang/Tools/CrossToolHelpers.h"
#include "flang/Tools/TargetSetup.h"
#include "flang/Version.inc"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>

//===----------------------------------------------------------------------===//
// Some basic command-line options
//===----------------------------------------------------------------------===//

static llvm::cl::opt<std::string> inputFilename(llvm::cl::Positional,
                                                llvm::cl::Required,
                                                llvm::cl::desc("<input file>"));

static llvm::cl::opt<std::string>
    outputFilename("o", llvm::cl::desc("Specify the output filename"),
                   llvm::cl::value_desc("filename"));

static llvm::cl::list<std::string>
    includeDirs("I", llvm::cl::desc("include module search paths"));

static llvm::cl::alias includeAlias("module-directory",
                                    llvm::cl::desc("module search directory"),
                                    llvm::cl::aliasopt(includeDirs));

static llvm::cl::list<std::string>
    intrinsicIncludeDirs("J", llvm::cl::desc("intrinsic module search paths"));

static llvm::cl::alias
    intrinsicIncludeAlias("intrinsic-module-directory",
                          llvm::cl::desc("intrinsic module directory"),
                          llvm::cl::aliasopt(intrinsicIncludeDirs));

static llvm::cl::opt<std::string>
    moduleDir("module", llvm::cl::desc("module output directory (default .)"),
              llvm::cl::init("."));

static llvm::cl::opt<std::string>
    moduleSuffix("module-suffix", llvm::cl::desc("module file suffix override"),
                 llvm::cl::init(".mod"));

static llvm::cl::opt<bool>
    emitFIR("emit-fir",
            llvm::cl::desc("Dump the FIR created by lowering and exit"),
            llvm::cl::init(false));

static llvm::cl::opt<bool>
    emitHLFIR("emit-hlfir",
              llvm::cl::desc("Dump the HLFIR created by lowering and exit"),
              llvm::cl::init(false));

static llvm::cl::opt<bool> warnStdViolation("Mstandard",
                                            llvm::cl::desc("emit warnings"),
                                            llvm::cl::init(false));

static llvm::cl::opt<bool> warnIsError("Werror",
                                       llvm::cl::desc("warnings are errors"),
                                       llvm::cl::init(false));

static llvm::cl::opt<bool> dumpSymbols("dump-symbols",
                                       llvm::cl::desc("dump the symbol table"),
                                       llvm::cl::init(false));

static llvm::cl::opt<bool> pftDumpTest(
    "pft-test",
    llvm::cl::desc("parse the input, create a PFT, dump it, and exit"),
    llvm::cl::init(false));

static llvm::cl::opt<bool> enableOpenMP("fopenmp",
                                        llvm::cl::desc("enable openmp"),
                                        llvm::cl::init(false));

static llvm::cl::opt<bool>
    enableOpenMPDevice("fopenmp-is-target-device",
                       llvm::cl::desc("enable openmp device compilation"),
                       llvm::cl::init(false));

static llvm::cl::opt<std::string> enableDoConcurrentToOpenMPConversion(
    "fdo-concurrent-to-openmp",
    llvm::cl::desc(
        "Try to map `do concurrent` loops to OpenMP [none|host|device]"),
    llvm::cl::init("none"));

static llvm::cl::opt<bool>
    enableOpenMPGPU("fopenmp-is-gpu",
                    llvm::cl::desc("enable openmp GPU target codegen"),
                    llvm::cl::init(false));

static llvm::cl::opt<bool> enableOpenMPForceUSM(
    "fopenmp-force-usm",
    llvm::cl::desc("force openmp unified shared memory mode"),
    llvm::cl::init(false));

static llvm::cl::list<std::string> targetTriplesOpenMP(
    "fopenmp-targets",
    llvm::cl::desc("comma-separated list of OpenMP offloading triples"),
    llvm::cl::CommaSeparated);

// A simplified subset of the OpenMP RTL Flags from Flang, only the primary
// positive options are available, no negative options e.g. fopen_assume* vs
// fno_open_assume*
static llvm::cl::opt<uint32_t>
    setOpenMPVersion("fopenmp-version",
                     llvm::cl::desc("OpenMP standard version"),
                     llvm::cl::init(31));

static llvm::cl::opt<uint32_t> setOpenMPTargetDebug(
    "fopenmp-target-debug",
    llvm::cl::desc("Enable debugging in the OpenMP offloading device RTL"),
    llvm::cl::init(0));

static llvm::cl::opt<bool> setOpenMPThreadSubscription(
    "fopenmp-assume-threads-oversubscription",
    llvm::cl::desc("Assume work-shared loops do not have more "
                   "iterations than participating threads."),
    llvm::cl::init(false));

static llvm::cl::opt<bool> setOpenMPTeamSubscription(
    "fopenmp-assume-teams-oversubscription",
    llvm::cl::desc("Assume distributed loops do not have more iterations than "
                   "participating teams."),
    llvm::cl::init(false));

static llvm::cl::opt<bool> setOpenMPNoThreadState(
    "fopenmp-assume-no-thread-state",
    llvm::cl::desc(
        "Assume that no thread in a parallel region will modify an ICV."),
    llvm::cl::init(false));

static llvm::cl::opt<bool> setOpenMPNoNestedParallelism(
    "fopenmp-assume-no-nested-parallelism",
    llvm::cl::desc("Assume that no thread in a parallel region will encounter "
                   "a parallel region."),
    llvm::cl::init(false));

static llvm::cl::opt<bool>
    setNoGPULib("nogpulib",
                llvm::cl::desc("Do not link device library for CUDA/HIP device "
                               "compilation"),
                llvm::cl::init(false));

static llvm::cl::opt<bool> enableOpenACC("fopenacc",
                                         llvm::cl::desc("enable openacc"),
                                         llvm::cl::init(false));

static llvm::cl::opt<bool> enableNoPPCNativeVecElemOrder(
    "fno-ppc-native-vector-element-order",
    llvm::cl::desc("no PowerPC native vector element order."),
    llvm::cl::init(false));

static llvm::cl::opt<bool> useHLFIR("hlfir",
                                    llvm::cl::desc("Lower to high level FIR"),
                                    llvm::cl::init(true));

static llvm::cl::opt<bool> enableCUDA("fcuda",
                                      llvm::cl::desc("enable CUDA Fortran"),
                                      llvm::cl::init(false));

static llvm::cl::opt<bool>
    enableDoConcurrentOffload("fdoconcurrent-offload",
                              llvm::cl::desc("enable do concurrent offload"),
                              llvm::cl::init(false));

static llvm::cl::opt<bool>
    disableCUDAWarpFunction("fcuda-disable-warp-function",
                            llvm::cl::desc("Disable CUDA Warp Function"),
                            llvm::cl::init(false));

static llvm::cl::opt<std::string>
    enableGPUMode("gpu", llvm::cl::desc("Enable GPU Mode managed|unified"),
                  llvm::cl::init(""));

static llvm::cl::opt<bool> fixedForm("ffixed-form",
                                     llvm::cl::desc("enable fixed form"),
                                     llvm::cl::init(false));
static llvm::cl::opt<std::string>
    targetTripleOverride("target",
                         llvm::cl::desc("Override host target triple"),
                         llvm::cl::init(""));

static llvm::cl::opt<bool> integerWrapAround(
    "fwrapv",
    llvm::cl::desc("Treat signed integer overflow as two's complement"),
    llvm::cl::init(false));

static llvm::cl::opt<bool> initGlobalZero(
    "finit-global-zero",
    llvm::cl::desc("Zero initialize globals without default initialization"),
    llvm::cl::init(true));

static llvm::cl::opt<bool>
    reallocateLHS("frealloc-lhs",
                  llvm::cl::desc("Follow Fortran 2003 rules for (re)allocating "
                                 "the LHS of the intrinsic assignment"),
                  llvm::cl::init(true));

static llvm::cl::opt<bool> stackRepackArrays(
    "fstack-repack-arrays",
    llvm::cl::desc("Allocate temporary arrays for -frepack-arrays "
                   "in stack memory"),
    llvm::cl::init(false));

static llvm::cl::opt<bool>
    repackArrays("frepack-arrays",
                 llvm::cl::desc("Pack non-contiguous assummed shape arrays "
                                "into contiguous memory"),
                 llvm::cl::init(false));

static llvm::cl::opt<bool>
    repackArraysWhole("frepack-arrays-continuity-whole",
                      llvm::cl::desc("Repack arrays that are non-contiguous "
                                     "in any dimension. If set to false, "
                                     "only the arrays non-contiguous in the "
                                     "leading dimension will be repacked"),
                      llvm::cl::init(true));

static llvm::cl::opt<std::string> complexRange(
    "complex-range",
    llvm::cl::desc("Controls the various implementations for complex "
                   "multiplication and division [full|improved|basic]"),
    llvm::cl::init(""));

#define FLANG_EXCLUDE_CODEGEN
#include "flang/Optimizer/Passes/CommandLineOpts.h"
#include "flang/Optimizer/Passes/Pipelines.h"

//===----------------------------------------------------------------------===//

using ProgramName = std::string;

// Print the module with the "module { ... }" wrapper, preventing
// information loss from attribute information appended to the module
static void printModule(mlir::ModuleOp mlirModule, llvm::raw_ostream &out) {
  out << mlirModule << '\n';
}

static void registerAllPasses() {
  fir::support::registerMLIRPassesForFortranTools();
  fir::registerOptTransformPasses();
}

/// Create a target machine that is at least sufficient to get data-layout
/// information required by flang semantics and lowering. Note that it may not
/// contain all the CPU feature information to get optimized assembly generation
/// from LLVM IR. Drivers that needs to generate assembly from LLVM IR should
/// create a target machine according to their specific options.
static std::unique_ptr<llvm::TargetMachine>
createTargetMachine(llvm::StringRef targetTriple, std::string &error) {
  std::string triple{targetTriple};
  if (triple.empty())
    triple = llvm::sys::getDefaultTargetTriple();

  const llvm::Target *theTarget =
      llvm::TargetRegistry::lookupTarget(triple, error);
  if (!theTarget)
    return nullptr;
  return std::unique_ptr<llvm::TargetMachine>{
      theTarget->createTargetMachine(llvm::Triple(triple), /*CPU=*/"",
                                     /*Features=*/"", llvm::TargetOptions(),
                                     /*Reloc::Model=*/std::nullopt)};
}

/// Build and execute the OpenMPFIRPassPipeline with its own instance
/// of the pass manager, allowing it to be invoked as soon as it's
/// required without impacting the main pass pipeline that may be invoked
/// more than once for verification.
static llvm::LogicalResult runOpenMPPasses(mlir::ModuleOp mlirModule) {
  mlir::PassManager pm(mlirModule->getName(),
                       mlir::OpPassManager::Nesting::Implicit);
  using DoConcurrentMappingKind =
      Fortran::frontend::CodeGenOptions::DoConcurrentMappingKind;

  fir::OpenMPFIRPassPipelineOpts opts;
  opts.isTargetDevice = enableOpenMPDevice;
  opts.doConcurrentMappingKind =
      llvm::StringSwitch<DoConcurrentMappingKind>(
          enableDoConcurrentToOpenMPConversion)
          .Case("host", DoConcurrentMappingKind::DCMK_Host)
          .Case("device", DoConcurrentMappingKind::DCMK_Device)
          .Default(DoConcurrentMappingKind::DCMK_None);

  fir::createOpenMPFIRPassPipeline(pm, opts);
  (void)mlir::applyPassManagerCLOptions(pm);
  if (mlir::failed(pm.run(mlirModule))) {
    llvm::errs() << "FATAL: failed to correctly apply OpenMP pass pipeline";
    return mlir::failure();
  }
  return mlir::success();
}

//===----------------------------------------------------------------------===//
// Translate Fortran input to FIR, a dialect of MLIR.
//===----------------------------------------------------------------------===//

static llvm::LogicalResult convertFortranSourceToMLIR(
    std::string path, Fortran::parser::Options options,
    const ProgramName &programPrefix,
    Fortran::semantics::SemanticsContext &semanticsContext,
    const mlir::PassPipelineCLParser &passPipeline,
    const llvm::TargetMachine &targetMachine) {

  // prep for prescan and parse
  Fortran::parser::Parsing parsing{semanticsContext.allCookedSources()};
  parsing.Prescan(path, options);
  if (!parsing.messages().empty() && (parsing.messages().AnyFatalError())) {
    llvm::errs() << programPrefix << "could not scan " << path << '\n';
    parsing.messages().Emit(llvm::errs(), parsing.allCooked());
    return mlir::failure();
  }

  // parse the input Fortran
  parsing.Parse(llvm::outs());
  if (!parsing.consumedWholeFile()) {
    parsing.messages().Emit(llvm::errs(), parsing.allCooked());
    parsing.EmitMessage(llvm::errs(), parsing.finalRestingPlace(),
                        "parser FAIL (final position)",
                        "error: ", llvm::raw_ostream::RED);
    return mlir::failure();
  } else if ((!parsing.messages().empty() &&
              (parsing.messages().AnyFatalError())) ||
             !parsing.parseTree().has_value()) {
    parsing.messages().Emit(llvm::errs(), parsing.allCooked());
    llvm::errs() << programPrefix << "could not parse " << path << '\n';
    return mlir::failure();
  } else {
    semanticsContext.messages().Annex(std::move(parsing.messages()));
  }

  // run semantics
  auto &parseTree = *parsing.parseTree();
  Fortran::semantics::Semantics semantics(semanticsContext, parseTree);
  semantics.Perform();
  semantics.EmitMessages(llvm::errs());
  if (semantics.AnyFatalError()) {
    llvm::errs() << programPrefix << "semantic errors in " << path << '\n';
    return mlir::failure();
  }
  Fortran::semantics::RuntimeDerivedTypeTables tables;
  if (!semantics.AnyFatalError()) {
    tables =
        Fortran::semantics::BuildRuntimeDerivedTypeTables(semanticsContext);
    if (!tables.schemata)
      llvm::errs() << programPrefix
                   << "could not find module file for __fortran_type_info\n";
  }

  if (dumpSymbols) {
    semantics.DumpSymbols(llvm::outs());
    return mlir::success();
  }

  if (pftDumpTest) {
    if (auto ast = Fortran::lower::createPFT(parseTree, semanticsContext)) {
      Fortran::lower::dumpPFT(llvm::outs(), *ast);
      return mlir::success();
    }
    llvm::errs() << "Pre FIR Tree is NULL.\n";
    return mlir::failure();
  }

  // translate to FIR dialect of MLIR
  mlir::DialectRegistry registry;
  fir::support::registerNonCodegenDialects(registry);
  fir::support::addFIRExtensions(registry);
  mlir::MLIRContext ctx(registry);
  fir::support::loadNonCodegenDialects(ctx);
  auto &defKinds = semanticsContext.defaultKinds();
  fir::KindMapping kindMap(
      &ctx, llvm::ArrayRef<fir::KindTy>{fir::fromDefaultKinds(defKinds)});
  std::string targetTriple = targetMachine.getTargetTriple().normalize();
  // Use default lowering options for bbc.
  Fortran::lower::LoweringOptions loweringOptions{};
  loweringOptions.setNoPPCNativeVecElemOrder(enableNoPPCNativeVecElemOrder);
  loweringOptions.setLowerToHighLevelFIR(useHLFIR || emitHLFIR);
  loweringOptions.setIntegerWrapAround(integerWrapAround);
  loweringOptions.setInitGlobalZero(initGlobalZero);
  loweringOptions.setReallocateLHS(reallocateLHS);
  loweringOptions.setStackRepackArrays(stackRepackArrays);
  loweringOptions.setRepackArrays(repackArrays);
  loweringOptions.setRepackArraysWhole(repackArraysWhole);
  loweringOptions.setSkipExternalRttiDefinition(skipExternalRttiDefinition);
  if (enableCUDA)
    loweringOptions.setCUDARuntimeCheck(true);
  if (complexRange == "improved" || complexRange == "basic")
    loweringOptions.setComplexDivisionToRuntime(false);
  std::vector<Fortran::lower::EnvironmentDefault> envDefaults = {};
  Fortran::frontend::TargetOptions targetOpts;
  Fortran::frontend::CodeGenOptions cgOpts;
  auto burnside = Fortran::lower::LoweringBridge::create(
      ctx, semanticsContext, defKinds, semanticsContext.intrinsics(),
      semanticsContext.targetCharacteristics(), parsing.allCooked(),
      targetTriple, kindMap, loweringOptions, envDefaults,
      semanticsContext.languageFeatures(), targetMachine, targetOpts, cgOpts);
  mlir::ModuleOp mlirModule = burnside.getModule();
  if (enableOpenMP) {
    if (enableOpenMPGPU && !enableOpenMPDevice) {
      llvm::errs() << "FATAL: -fopenmp-is-gpu can only be set if "
                      "-fopenmp-is-target-device is also set";
      return mlir::failure();
    }
    // Construct offloading target triples vector.
    std::vector<llvm::Triple> targetTriples;
    targetTriples.reserve(targetTriplesOpenMP.size());
    for (llvm::StringRef s : targetTriplesOpenMP)
      targetTriples.emplace_back(s);

    auto offloadModuleOpts = OffloadModuleOpts(
        setOpenMPTargetDebug, setOpenMPTeamSubscription,
        setOpenMPThreadSubscription, setOpenMPNoThreadState,
        setOpenMPNoNestedParallelism, enableOpenMPDevice, enableOpenMPGPU,
        enableOpenMPForceUSM, setOpenMPVersion, "", targetTriples, setNoGPULib);
    setOffloadModuleInterfaceAttributes(mlirModule, offloadModuleOpts);
    setOpenMPVersionAttribute(mlirModule, setOpenMPVersion);
  }
  burnside.lower(parseTree, semanticsContext);
  std::error_code ec;
  std::string outputName = outputFilename;
  if (!outputName.size())
    outputName = llvm::sys::path::stem(inputFilename).str().append(".mlir");
  llvm::raw_fd_ostream out(outputName, ec);
  if (ec)
    return mlir::emitError(mlir::UnknownLoc::get(&ctx),
                           "could not open output file ")
           << outputName;

  // WARNING: This pipeline must be run immediately after the lowering to
  // ensure that the FIR is correct with respect to OpenMP operations/
  // attributes.
  if (enableOpenMP)
    if (mlir::failed(runOpenMPPasses(mlirModule)))
      return mlir::failure();

  // Otherwise run the default passes.
  mlir::PassManager pm(mlirModule->getName(),
                       mlir::OpPassManager::Nesting::Implicit);
  pm.enableVerifier(/*verifyPasses=*/true);
  (void)mlir::applyPassManagerCLOptions(pm);
  if (passPipeline.hasAnyOccurrences()) {
    // run the command-line specified pipeline
    hlfir::registerHLFIRPasses();
    (void)passPipeline.addToPipeline(pm, [&](const llvm::Twine &msg) {
      mlir::emitError(mlir::UnknownLoc::get(&ctx)) << msg;
      return mlir::failure();
    });
  } else if (emitFIR || emitHLFIR) {
    // --emit-fir: Build the IR, verify it, and dump the IR if the IR passes
    // verification. Use --dump-module-on-failure to dump invalid IR.
    pm.addPass(std::make_unique<Fortran::lower::VerifierPass>());
    if (mlir::failed(pm.run(mlirModule))) {
      llvm::errs() << "FATAL: verification of lowering to FIR failed";
      return mlir::failure();
    }

    if (emitFIR && useHLFIR) {
      // lower HLFIR to FIR
      fir::createHLFIRToFIRPassPipeline(pm, enableOpenMP,
                                        llvm::OptimizationLevel::O2);
      if (mlir::failed(pm.run(mlirModule))) {
        llvm::errs() << "FATAL: lowering from HLFIR to FIR failed";
        return mlir::failure();
      }
    }

    printModule(mlirModule, out);
    return mlir::success();
  } else {
    // run the default canned pipeline
    pm.addPass(std::make_unique<Fortran::lower::VerifierPass>());

    // Add O2 optimizer pass pipeline.
    MLIRToLLVMPassPipelineConfig config(llvm::OptimizationLevel::O2);
    if (enableOpenMP)
      config.EnableOpenMP = true;
    config.NSWOnLoopVarInc = !integerWrapAround;
    fir::registerDefaultInlinerPass(config);
    fir::createDefaultFIROptimizerPassPipeline(pm, config);
  }

  if (mlir::succeeded(pm.run(mlirModule))) {
    // Emit MLIR and do not lower to LLVM IR.
    printModule(mlirModule, out);
    return mlir::success();
  }
  // Something went wrong. Try to dump the MLIR module.
  llvm::errs() << "oops, pass manager reported failure\n";
  return mlir::failure();
}

int main(int argc, char **argv) {
  [[maybe_unused]] llvm::InitLLVM y(argc, argv);
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  registerAllPasses();

  mlir::registerMLIRContextCLOptions();
  mlir::registerAsmPrinterCLOptions();
  mlir::registerPassManagerCLOptions();
  mlir::PassPipelineCLParser passPipe("", "Compiler passes to run");
  llvm::cl::ParseCommandLineOptions(argc, argv, "Burnside Bridge Compiler\n");

  ProgramName programPrefix;
  programPrefix = argv[0] + ": "s;

  if (includeDirs.size() == 0) {
    includeDirs.push_back(".");
    // Default Fortran modules should be installed in include/flang (a sibling
    // to the bin) directory.
    intrinsicIncludeDirs.push_back(
        llvm::sys::path::parent_path(
            llvm::sys::path::parent_path(
                llvm::sys::fs::getMainExecutable(argv[0], nullptr)))
            .str() +
        "/include/flang");
  }

  Fortran::parser::Options options;
  options.predefinitions.emplace_back("__flang__"s, "1"s);
  options.predefinitions.emplace_back("__flang_major__"s,
                                      std::string{FLANG_VERSION_MAJOR_STRING});
  options.predefinitions.emplace_back("__flang_minor__"s,
                                      std::string{FLANG_VERSION_MINOR_STRING});
  options.predefinitions.emplace_back(
      "__flang_patchlevel__"s, std::string{FLANG_VERSION_PATCHLEVEL_STRING});

  Fortran::common::LangOptions langOpts;
  langOpts.NoGPULib = setNoGPULib;
  langOpts.OpenMPVersion = setOpenMPVersion;
  langOpts.OpenMPIsTargetDevice = enableOpenMPDevice;
  langOpts.OpenMPIsGPU = enableOpenMPGPU;
  langOpts.OpenMPForceUSM = enableOpenMPForceUSM;
  langOpts.OpenMPTargetDebug = setOpenMPTargetDebug;
  langOpts.OpenMPThreadSubscription = setOpenMPThreadSubscription;
  langOpts.OpenMPTeamSubscription = setOpenMPTeamSubscription;
  langOpts.OpenMPNoThreadState = setOpenMPNoThreadState;
  langOpts.OpenMPNoNestedParallelism = setOpenMPNoNestedParallelism;
  std::transform(targetTriplesOpenMP.begin(), targetTriplesOpenMP.end(),
                 std::back_inserter(langOpts.OMPTargetTriples),
                 [](const std::string &str) { return llvm::Triple(str); });

  // enable parsing of OpenMP
  if (enableOpenMP) {
    options.features.Enable(Fortran::common::LanguageFeature::OpenMP);
    Fortran::common::setOpenMPMacro(setOpenMPVersion, options.predefinitions);
  }

  // enable parsing of OpenACC
  if (enableOpenACC) {
    options.features.Enable(Fortran::common::LanguageFeature::OpenACC);
    options.predefinitions.emplace_back("_OPENACC", "202211");
  }

  // enable parsing of CUDA Fortran
  if (enableCUDA) {
    options.features.Enable(Fortran::common::LanguageFeature::CUDA);
  }

  if (enableDoConcurrentOffload) {
    options.features.Enable(
        Fortran::common::LanguageFeature::DoConcurrentOffload);
  }

  if (disableCUDAWarpFunction) {
    options.features.Enable(
        Fortran::common::LanguageFeature::CudaWarpMatchFunction, false);
  }

  if (enableGPUMode == "managed") {
    options.features.Enable(Fortran::common::LanguageFeature::CudaManaged);
  } else if (enableGPUMode == "unified") {
    options.features.Enable(Fortran::common::LanguageFeature::CudaUnified);
  }

  if (fixedForm) {
    options.isFixedForm = fixedForm;
  }

  Fortran::common::IntrinsicTypeDefaultKinds defaultKinds;
  Fortran::parser::AllSources allSources;
  Fortran::parser::AllCookedSources allCookedSources(allSources);
  Fortran::semantics::SemanticsContext semanticsContext{
      defaultKinds, options.features, langOpts, allCookedSources};
  semanticsContext.set_moduleDirectory(moduleDir)
      .set_moduleFileSuffix(moduleSuffix)
      .set_searchDirectories(includeDirs)
      .set_intrinsicModuleDirectories(intrinsicIncludeDirs)
      .set_warnOnNonstandardUsage(warnStdViolation)
      .set_warningsAreErrors(warnIsError);

  std::string error;
  // Create host target machine.
  std::unique_ptr<llvm::TargetMachine> targetMachine =
      createTargetMachine(targetTripleOverride, error);
  if (!targetMachine) {
    llvm::errs() << "failed to create target machine: " << error << "\n";
    return mlir::failed(mlir::failure());
  }
  std::string compilerVersion = Fortran::common::getFlangToolFullVersion("bbc");
  std::string compilerOptions = "";
  Fortran::tools::setUpTargetCharacteristics(
      semanticsContext.targetCharacteristics(), *targetMachine, {},
      compilerVersion, compilerOptions);

  return mlir::failed(
      convertFortranSourceToMLIR(inputFilename, options, programPrefix,
                                 semanticsContext, passPipe, *targetMachine));
}
