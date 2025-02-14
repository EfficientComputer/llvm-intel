//===- Options.h - cgeist command line options ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Command line flags for cgeist.
//
//===----------------------------------------------------------------------===//

#ifndef CGEIST_OPTIONS_H_
#define CGEIST_OPTIONS_H_

#include "llvm/ADT/SmallString.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/CommandLine.h"
#include <string>

/// Class to pass options to a compilation tool.
class ArgumentList {
public:
  /// Add argument.
  ///
  /// The element stored will not be owned by this.
  void push_back(llvm::StringRef Arg) { Args.push_back(Arg.data()); }

  /// Add argument and ensure it will be valid before this passer's destruction.
  ///
  /// The element stored will be owned by this.
  template <typename... ArgTy> void emplace_back(ArgTy &&...Args) {
    // Store as a SmallString
    push_back(Storage
                  .emplace_back(std::initializer_list<llvm::StringRef>{
                      std::forward<ArgTy>(Args)...})
                  .c_str());
  }

  /// Return the underling argument list.
  ///
  /// The return value of this operation could be invalidated by subsequent
  /// calls to push_back() or emplace_back().
  llvm::ArrayRef<const char *> getArguments() const { return Args; }

private:
  /// Helper storage.
  ///
  /// llvm::SmallString<0> is needed to enforce heap allocation.
  llvm::SmallVector<llvm::SmallString<0>> Storage;
  /// List of arguments
  llvm::SmallVector<const char *> Args;
};

/// Represents command line options affecting the behaviour of cgeist.
class CgeistOptions {
public:
  void setSYCLIsDevice() { SYCLIsDevice = true; }
  void setRelaxedAliasing() { RelaxedAliasing = true; }
  void setOptimizationLevel(llvm::OptimizationLevel OptLevel) {
    OptimizationLevel = OptLevel;
  }

  bool getSYCLIsDevice() const { return SYCLIsDevice; }
  bool getRelaxedAliasing() const { return RelaxedAliasing; }
  const llvm::OptimizationLevel &getOptimizationLevel() const {
    return OptimizationLevel;
  }

private:
  /// Whether to generate MLIR only for kernels.
  bool SYCLIsDevice = false;

  /// Whether to assume the program respects ANSI aliasing rules (i.e. strict
  /// aliasing).
  bool RelaxedAliasing = false;

  /// Optimization level to use.
  llvm::OptimizationLevel OptimizationLevel = llvm::OptimizationLevel::O0;
};

/// Categorizes command line options.
class Options {
public:
  Options(int argc, char **argv) { splitCommandLineOptions(argc, argv); }

  CgeistOptions &getCgeistOpts() { return CgeistOpts; }
  llvm::ArrayRef<const char *> getMLIROpts() { return MLIROpts; }
  llvm::ArrayRef<const char *> getLLVMOpts() { return LLVMOpts; }
  llvm::ArrayRef<const char *> getLinkOpts() { return LinkOpts; }

private:
  /// Split the input arguments into the following buckets:
  ///   - CgeistOpts: options affecting cgeist behaviour.
  ///   - MLIROpts: options affecting MLIR behaviour.
  ///   - LLVMOpts: options affecting LLVM behaviour.
  ///   - LinkOpts: options affecting the linker behaviour.
  void splitCommandLineOptions(int argc, char **argv);

  CgeistOptions CgeistOpts;
  llvm::SmallVector<const char *> MLIROpts;
  llvm::SmallVector<const char *> LLVMOpts;
  llvm::SmallVector<const char *> LinkOpts;
};

static llvm::cl::OptionCategory ToolOptions("clang to mlir - tool options");

llvm::cl::opt<bool> CudaLower("cuda-lower", llvm::cl::init(false),
                              llvm::cl::desc("Add parallel loops around cuda"));

static llvm::cl::opt<bool> EmitLLVM("emit-llvm", llvm::cl::init(false),
                                    llvm::cl::desc("Emit llvm"));

static llvm::cl::opt<bool> EmitOpenMPIR("emit-openmpir", llvm::cl::init(false),
                                        llvm::cl::desc("Emit OpenMP IR"));

static llvm::cl::opt<bool> EmitAssembly("S", llvm::cl::init(false),
                                        llvm::cl::desc("Emit Assembly"));

static llvm::cl::opt<bool> SCFOpenMP("scf-openmp", llvm::cl::init(true),
                                     llvm::cl::desc("Emit llvm"));

static llvm::cl::opt<bool> OpenMPOpt("openmp-opt", llvm::cl::init(true),
                                     llvm::cl::desc("Turn on openmp opt"));

static llvm::cl::opt<bool> EnableLICM("licm", llvm::cl::init(true),
                                      llvm::cl::desc("Turn on LICM"));

static llvm::cl::opt<bool>
    InnerSerialize("inner-serialize", llvm::cl::init(false),
                   llvm::cl::desc("Turn on Inner Serialize"));

static llvm::cl::opt<bool> ShowAST("show-ast", llvm::cl::init(false),
                                   llvm::cl::desc("Show AST"));

static llvm::cl::opt<bool> RaiseToAffine("raise-scf-to-affine",
                                         llvm::cl::init(true),
                                         llvm::cl::desc("Raise SCF to Affine"));

static llvm::cl::opt<bool>
    ScalarReplacement("scal-rep", llvm::cl::init(true),
                      llvm::cl::desc("Raise SCF to Affine"));

static llvm::cl::opt<bool> LoopUnroll("unroll-loops", llvm::cl::init(true),
                                      llvm::cl::desc("Unroll Affine Loops"));

static llvm::cl::opt<bool>
    DetectReduction("detect-reduction", llvm::cl::init(true),
                    llvm::cl::desc("Detect reduction in inner most loop"));

static llvm::cl::opt<std::string> Standard("std", llvm::cl::init(""),
                                           llvm::cl::desc("C/C++ std"));

static llvm::cl::opt<std::string> CUDAGPUArch("cuda-gpu-arch",
                                              llvm::cl::init(""),
                                              llvm::cl::desc("CUDA GPU arch"));

static llvm::cl::opt<std::string> CUDAPath("cuda-path", llvm::cl::init(""),
                                           llvm::cl::desc("CUDA Path"));

static llvm::cl::opt<bool>
    NoCUDAInc("nocudainc", llvm::cl::init(false),
              llvm::cl::desc("Do not include CUDA headers"));

static llvm::cl::opt<bool>
    NoCUDALib("nocudalib", llvm::cl::init(false),
              llvm::cl::desc("Do not link CUDA libdevice"));

static llvm::cl::opt<std::string> Output("o", llvm::cl::init("-"),
                                         llvm::cl::desc("Output file"));

static llvm::cl::opt<std::string>
    Cfunction("function", llvm::cl::desc("<Specify function>"),
              llvm::cl::init("main"), llvm::cl::cat(ToolOptions));

static llvm::cl::opt<bool> FOpenMP("fopenmp", llvm::cl::init(false),
                                   llvm::cl::desc("Enable OpenMP"));

static llvm::cl::opt<std::string> ToCPU("cpuify", llvm::cl::init(""),
                                        llvm::cl::desc("Convert to cpu"));

static llvm::cl::opt<std::string> MArch("march", llvm::cl::init(""),
                                        llvm::cl::desc("Architecture"));

static llvm::cl::opt<std::string> ResourceDir("resource-dir",
                                              llvm::cl::init(""),
                                              llvm::cl::desc("Resource-dir"));

static llvm::cl::opt<std::string> SysRoot("sysroot", llvm::cl::init(""),
                                          llvm::cl::desc("sysroot"));

static llvm::cl::opt<bool>
    EarlyVerifier("early-verifier", llvm::cl::init(false),
                  llvm::cl::desc("Enable verifier ASAP"));

static llvm::cl::opt<bool> Verbose("v", llvm::cl::init(false),
                                   llvm::cl::desc("Verbose"));

llvm::cl::opt<bool> SuppressWarnings("w", llvm::cl::init(false),
                                     llvm::cl::desc("Suppress all warnings"));

static llvm::cl::list<std::string>
    IncludeDirs("I", llvm::cl::desc("include search path"),
                llvm::cl::cat(ToolOptions));

static llvm::cl::list<std::string> Defines("D", llvm::cl::desc("defines"),
                                           llvm::cl::cat(ToolOptions));

static llvm::cl::list<std::string>
    Includes("include", llvm::cl::desc("includes"), llvm::cl::cat(ToolOptions));

static llvm::cl::opt<std::string>
    TargetTripleOpt("target", llvm::cl::init(""),
                    llvm::cl::desc("Target triple"),
                    llvm::cl::cat(ToolOptions));

static llvm::cl::opt<int> CanonicalizeIterations(
    "canonicalizeiters", llvm::cl::init(400),
    llvm::cl::desc("Number of canonicalization iterations"));

static llvm::cl::opt<std::string> McpuOpt("mcpu", llvm::cl::init(""),
                                          llvm::cl::desc("Target CPU"),
                                          llvm::cl::cat(ToolOptions));

static llvm::cl::opt<std::string>
    SYCLUseHostModule("sycl-use-host-module", llvm::cl::init(""),
                      llvm::cl::desc("Use input file as host SYCL module"),
                      llvm::cl::cat(ToolOptions));

llvm::cl::opt<bool>
    UseOpaquePointers("use-opaque-pointers", llvm::cl::init(false),
                      llvm::cl::desc("Whether to use opaque pointers in MLIR"));

#endif /* CGEIST_OPTIONS_H_ */
