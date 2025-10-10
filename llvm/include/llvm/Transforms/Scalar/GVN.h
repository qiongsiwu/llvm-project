//===- GVN.h - Eliminate redundant values and loads -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Global Value Numbering pass
/// which eliminates fully redundant instructions. It also does somewhat Ad-Hoc
/// PRE and dead load elimination.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_GVN_H
#define LLVM_TRANSFORMS_SCALAR_GVN_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {

class AAResults;
class AssumeInst;
class AssumptionCache;
class BasicBlock;
class BranchInst;
class CallInst;
class ExtractValueInst;
class Function;
class FunctionPass;
class GetElementPtrInst;
class ImplicitControlFlowTracking;
class LoadInst;
class LoopInfo;
class MemDepResult;
class MemoryAccess;
class MemoryDependenceResults;
class MemoryLocation;
class MemorySSA;
class MemorySSAUpdater;
class NonLocalDepResult;
class OptimizationRemarkEmitter;
class PHINode;
class TargetLibraryInfo;
class Value;
class IntrinsicInst;
/// A private "module" namespace for types and utilities used by GVN. These
/// are implementation details and should not be used by clients.
namespace LLVM_LIBRARY_VISIBILITY_NAMESPACE gvn {

struct AvailableValue;
struct AvailableValueInBlock;
class GVNLegacyPass;

} // end namespace gvn

/// A set of parameters to control various transforms performed by GVN pass.
//  Each of the optional boolean parameters can be set to:
///      true - enabling the transformation.
///      false - disabling the transformation.
///      None - relying on a global default.
/// Intended use is to create a default object, modify parameters with
/// additional setters and then pass it to GVN.
struct GVNOptions {
  std::optional<bool> AllowPRE;
  std::optional<bool> AllowLoadPRE;
  std::optional<bool> AllowLoadInLoopPRE;
  std::optional<bool> AllowLoadPRESplitBackedge;
  std::optional<bool> AllowMemDep;
  std::optional<bool> AllowMemorySSA;

  GVNOptions() = default;

  /// Enables or disables PRE in GVN.
  GVNOptions &setPRE(bool PRE) {
    AllowPRE = PRE;
    return *this;
  }

  /// Enables or disables PRE of loads in GVN.
  GVNOptions &setLoadPRE(bool LoadPRE) {
    AllowLoadPRE = LoadPRE;
    return *this;
  }

  GVNOptions &setLoadInLoopPRE(bool LoadInLoopPRE) {
    AllowLoadInLoopPRE = LoadInLoopPRE;
    return *this;
  }

  /// Enables or disables PRE of loads in GVN.
  GVNOptions &setLoadPRESplitBackedge(bool LoadPRESplitBackedge) {
    AllowLoadPRESplitBackedge = LoadPRESplitBackedge;
    return *this;
  }

  /// Enables or disables use of MemDepAnalysis.
  GVNOptions &setMemDep(bool MemDep) {
    AllowMemDep = MemDep;
    return *this;
  }

  /// Enables or disables use of MemorySSA.
  GVNOptions &setMemorySSA(bool MemSSA) {
    AllowMemorySSA = MemSSA;
    return *this;
  }
};

/// The core GVN pass object.
///
/// FIXME: We should have a good summary of the GVN algorithm implemented by
/// this particular pass here.
class GVNPass : public PassInfoMixin<GVNPass> {
  GVNOptions Options;

public:
  struct Expression;

  GVNPass(GVNOptions Options = {}) : Options(Options) {}

  /// Run the pass over the function.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// This removes the specified instruction from
  /// our various maps and marks it for deletion.
  LLVM_ABI void salvageAndRemoveInstruction(Instruction *I);

  DominatorTree &getDominatorTree() const { return *DT; }
  AAResults *getAliasAnalysis() const { return VN.getAliasAnalysis(); }
  MemoryDependenceResults &getMemDep() const { return *MD; }

  LLVM_ABI bool isPREEnabled() const;
  LLVM_ABI bool isLoadPREEnabled() const;
  LLVM_ABI bool isLoadInLoopPREEnabled() const;
  LLVM_ABI bool isLoadPRESplitBackedgeEnabled() const;
  LLVM_ABI bool isMemDepEnabled() const;
  LLVM_ABI bool isMemorySSAEnabled() const;

  /// This class holds the mapping between values and value numbers.  It is used
  /// as an efficient mechanism to determine the expression-wise equivalence of
  /// two values.
  class ValueTable {
    DenseMap<Value *, uint32_t> ValueNumbering;
    DenseMap<Expression, uint32_t> ExpressionNumbering;

    // Expressions is the vector of Expression. ExprIdx is the mapping from
    // value number to the index of Expression in Expressions. We use it
    // instead of a DenseMap because filling such mapping is faster than
    // filling a DenseMap and the compile time is a little better.
    uint32_t NextExprNumber = 0;

    std::vector<Expression> Expressions;
    std::vector<uint32_t> ExprIdx;

    // Value number to PHINode mapping. Used for phi-translate in scalarpre.
    DenseMap<uint32_t, PHINode *> NumberingPhi;

    // Value number to BasicBlock mapping. Used for phi-translate across
    // MemoryPhis.
    DenseMap<uint32_t, BasicBlock *> NumberingBB;

    // Cache for phi-translate in scalarpre.
    using PhiTranslateMap =
        DenseMap<std::pair<uint32_t, const BasicBlock *>, uint32_t>;
    PhiTranslateMap PhiTranslateTable;

    AAResults *AA = nullptr;
    MemoryDependenceResults *MD = nullptr;
    bool IsMDEnabled = false;
    MemorySSA *MSSA = nullptr;
    bool IsMSSAEnabled = false;
    DominatorTree *DT = nullptr;

    uint32_t NextValueNumber = 1;

    Expression createExpr(Instruction *I);
    Expression createCmpExpr(unsigned Opcode, CmpInst::Predicate Predicate,
                             Value *LHS, Value *RHS);
    Expression createExtractvalueExpr(ExtractValueInst *EI);
    Expression createGEPExpr(GetElementPtrInst *GEP);
    uint32_t lookupOrAddCall(CallInst *C);
    uint32_t computeLoadStoreVN(Instruction *I);
    uint32_t phiTranslateImpl(const BasicBlock *BB, const BasicBlock *PhiBlock,
                              uint32_t Num, GVNPass &GVN);
    bool areCallValsEqual(uint32_t Num, uint32_t NewNum, const BasicBlock *Pred,
                          const BasicBlock *PhiBlock, GVNPass &GVN);
    std::pair<uint32_t, bool> assignExpNewValueNum(Expression &Exp);
    bool areAllValsInBB(uint32_t Num, const BasicBlock *BB, GVNPass &GVN);
    void addMemoryStateToExp(Instruction *I, Expression &Exp);

  public:
    LLVM_ABI ValueTable();
    LLVM_ABI ValueTable(const ValueTable &Arg);
    LLVM_ABI ValueTable(ValueTable &&Arg);
    LLVM_ABI ~ValueTable();
    LLVM_ABI ValueTable &operator=(const ValueTable &Arg);

    LLVM_ABI uint32_t lookupOrAdd(MemoryAccess *MA);
    LLVM_ABI uint32_t lookupOrAdd(Value *V);
    LLVM_ABI uint32_t lookup(Value *V, bool Verify = true) const;
    LLVM_ABI uint32_t lookupOrAddCmp(unsigned Opcode, CmpInst::Predicate Pred,
                                     Value *LHS, Value *RHS);
    LLVM_ABI uint32_t phiTranslate(const BasicBlock *BB,
                                   const BasicBlock *PhiBlock, uint32_t Num,
                                   GVNPass &GVN);
    LLVM_ABI void eraseTranslateCacheEntry(uint32_t Num,
                                           const BasicBlock &CurrBlock);
    LLVM_ABI bool exists(Value *V) const;
    LLVM_ABI void add(Value *V, uint32_t Num);
    LLVM_ABI void clear();
    LLVM_ABI void erase(Value *V);
    void setAliasAnalysis(AAResults *A) { AA = A; }
    AAResults *getAliasAnalysis() const { return AA; }
    void setMemDep(MemoryDependenceResults *M, bool MDEnabled = true) {
      MD = M;
      IsMDEnabled = MDEnabled;
    }
    void setMemorySSA(MemorySSA *M, bool MSSAEnabled = false) {
      MSSA = M;
      IsMSSAEnabled = MSSAEnabled;
    }
    void setDomTree(DominatorTree *D) { DT = D; }
    uint32_t getNextUnusedValueNumber() { return NextValueNumber; }
    LLVM_ABI void verifyRemoved(const Value *) const;
  };

private:
  friend class gvn::GVNLegacyPass;
  friend struct DenseMapInfo<Expression>;

  MemoryDependenceResults *MD = nullptr;
  DominatorTree *DT = nullptr;
  const TargetLibraryInfo *TLI = nullptr;
  AssumptionCache *AC = nullptr;
  SetVector<BasicBlock *> DeadBlocks;
  OptimizationRemarkEmitter *ORE = nullptr;
  ImplicitControlFlowTracking *ICF = nullptr;
  LoopInfo *LI = nullptr;
  MemorySSAUpdater *MSSAU = nullptr;

  ValueTable VN;

  /// A mapping from value numbers to lists of Value*'s that
  /// have that value number.  Use findLeader to query it.
  class LeaderMap {
  public:
    struct LeaderTableEntry {
      Value *Val;
      const BasicBlock *BB;
    };

  private:
    struct LeaderListNode {
      LeaderTableEntry Entry;
      LeaderListNode *Next;
    };
    DenseMap<uint32_t, LeaderListNode> NumToLeaders;
    BumpPtrAllocator TableAllocator;

  public:
    class leader_iterator {
      const LeaderListNode *Current;

    public:
      using iterator_category = std::forward_iterator_tag;
      using value_type = const LeaderTableEntry;
      using difference_type = std::ptrdiff_t;
      using pointer = value_type *;
      using reference = value_type &;

      leader_iterator(const LeaderListNode *C) : Current(C) {}
      leader_iterator &operator++() {
        assert(Current && "Dereferenced end of leader list!");
        Current = Current->Next;
        return *this;
      }
      bool operator==(const leader_iterator &Other) const {
        return Current == Other.Current;
      }
      bool operator!=(const leader_iterator &Other) const {
        return Current != Other.Current;
      }
      reference operator*() const { return Current->Entry; }
    };

    iterator_range<leader_iterator> getLeaders(uint32_t N) {
      auto I = NumToLeaders.find(N);
      if (I == NumToLeaders.end()) {
        return iterator_range(leader_iterator(nullptr),
                              leader_iterator(nullptr));
      }

      return iterator_range(leader_iterator(&I->second),
                            leader_iterator(nullptr));
    }

    LLVM_ABI void insert(uint32_t N, Value *V, const BasicBlock *BB);
    LLVM_ABI void erase(uint32_t N, Instruction *I, const BasicBlock *BB);
    LLVM_ABI void verifyRemoved(const Value *Inst) const;
    void clear() {
      NumToLeaders.clear();
      TableAllocator.Reset();
    }
  };
  LeaderMap LeaderTable;

  // Map the block to reversed postorder traversal number. It is used to
  // find back edge easily.
  DenseMap<AssertingVH<BasicBlock>, uint32_t> BlockRPONumber;

  // This is set 'true' initially and also when new blocks have been added to
  // the function being analyzed. This boolean is used to control the updating
  // of BlockRPONumber prior to accessing the contents of BlockRPONumber.
  bool InvalidBlockRPONumbers = true;

  using LoadDepVect = SmallVector<NonLocalDepResult, 64>;
  using AvailValInBlkVect = SmallVector<gvn::AvailableValueInBlock, 64>;
  using UnavailBlkVect = SmallVector<BasicBlock *, 64>;

  bool runImpl(Function &F, AssumptionCache &RunAC, DominatorTree &RunDT,
               const TargetLibraryInfo &RunTLI, AAResults &RunAA,
               MemoryDependenceResults *RunMD, LoopInfo &LI,
               OptimizationRemarkEmitter *ORE, MemorySSA *MSSA = nullptr);

  // List of critical edges to be split between iterations.
  SmallVector<std::pair<Instruction *, unsigned>, 4> ToSplit;

  // Helper functions of redundant load elimination.
  bool processLoad(LoadInst *L);
  bool processMaskedLoad(IntrinsicInst *I);
  bool processNonLocalLoad(LoadInst *L);
  bool processAssumeIntrinsic(AssumeInst *II);

  /// Given a local dependency (Def or Clobber) determine if a value is
  /// available for the load.
  std::optional<gvn::AvailableValue>
  AnalyzeLoadAvailability(LoadInst *Load, MemDepResult DepInfo, Value *Address);

  /// Given a list of non-local dependencies, determine if a value is
  /// available for the load in each specified block.  If it is, add it to
  /// ValuesPerBlock.  If not, add it to UnavailableBlocks.
  void AnalyzeLoadAvailability(LoadInst *Load, LoadDepVect &Deps,
                               AvailValInBlkVect &ValuesPerBlock,
                               UnavailBlkVect &UnavailableBlocks);

  /// Given a critical edge from Pred to LoadBB, find a load instruction
  /// which is identical to Load from another successor of Pred.
  LoadInst *findLoadToHoistIntoPred(BasicBlock *Pred, BasicBlock *LoadBB,
                                    LoadInst *Load);

  bool PerformLoadPRE(LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
                      UnavailBlkVect &UnavailableBlocks);

  /// Try to replace a load which executes on each loop iteraiton with Phi
  /// translation of load in preheader and load(s) in conditionally executed
  /// paths.
  bool performLoopLoadPRE(LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
                          UnavailBlkVect &UnavailableBlocks);

  /// Eliminates partially redundant \p Load, replacing it with \p
  /// AvailableLoads (connected by Phis if needed).
  void eliminatePartiallyRedundantLoad(
      LoadInst *Load, AvailValInBlkVect &ValuesPerBlock,
      MapVector<BasicBlock *, Value *> &AvailableLoads,
      MapVector<BasicBlock *, LoadInst *> *CriticalEdgePredAndLoad);

  // Other helper routines.
  bool processInstruction(Instruction *I);
  bool processBlock(BasicBlock *BB);
  void dump(DenseMap<uint32_t, Value *> &Map) const;
  bool iterateOnFunction(Function &F);
  bool performPRE(Function &F);
  bool performScalarPRE(Instruction *I);
  bool performScalarPREInsertion(Instruction *Instr, BasicBlock *Pred,
                                 BasicBlock *Curr, unsigned int ValNo);
  Value *findLeader(const BasicBlock *BB, uint32_t Num);
  void cleanupGlobalSets();
  void removeInstruction(Instruction *I);
  void verifyRemoved(const Instruction *I) const;
  bool splitCriticalEdges();
  BasicBlock *splitCriticalEdges(BasicBlock *Pred, BasicBlock *Succ);
  bool
  propagateEquality(Value *LHS, Value *RHS,
                    const std::variant<BasicBlockEdge, Instruction *> &Root);
  bool processFoldableCondBr(BranchInst *BI);
  void addDeadBlock(BasicBlock *BB);
  void assignValNumForDeadCode();
  void assignBlockRPONumber(Function &F);
};

/// Create a legacy GVN pass.
LLVM_ABI FunctionPass *createGVNPass();

/// A simple and fast domtree-based GVN pass to hoist common expressions
/// from sibling branches.
struct GVNHoistPass : PassInfoMixin<GVNHoistPass> {
  /// Run the pass over the function.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Uses an "inverted" value numbering to decide the similarity of
/// expressions and sinks similar expressions into successors.
struct GVNSinkPass : PassInfoMixin<GVNSinkPass> {
  /// Run the pass over the function.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

struct llvm::GVNPass::Expression {
  uint32_t Opcode;
  bool Commutative = false;
  // The type is not necessarily the result type of the expression, it may be
  // any additional type needed to disambiguate the expression.
  Type *Ty = nullptr;
  SmallVector<uint32_t, 4> VarArgs;

  AttributeList Attrs;

  Expression(uint32_t Op = ~2U) : Opcode(Op) {}

  bool operator==(const Expression &Other) const {
    if (Opcode != Other.Opcode)
      return false;
    if (Opcode == ~0U || Opcode == ~1U)
      return true;
    if (Ty != Other.Ty)
      return false;
    if (VarArgs != Other.VarArgs)
      return false;
    if ((!Attrs.isEmpty() || !Other.Attrs.isEmpty()) &&
        !Attrs.intersectWith(Ty->getContext(), Other.Attrs).has_value())
      return false;
    return true;
  }

  friend hash_code hash_value(const Expression &Value) {
    return hash_combine(Value.Opcode, Value.Ty,
                        hash_combine_range(Value.VarArgs));
  }
};

/// Represents a particular available value that we know how to materialize.
/// Materialization of an AvailableValue never fails.  An AvailableValue is
/// implicitly associated with a rematerialization point which is the
/// location of the instruction from which it was formed.
struct llvm::gvn::AvailableValue {
  enum class ValType {
    SimpleVal, // A simple offsetted value that is accessed.
    LoadVal,   // A value produced by a load.
    MemIntrin, // A memory intrinsic which is loaded from.
    UndefVal,  // A UndefValue representing a value from dead block (which
               // is not yet physically removed from the CFG).
    SelectVal, // A pointer select which is loaded from and for which the load
               // can be replace by a value select.
  };
  
  /// Val - The value that is live out of the block.
  Value *Val;
  /// Kind of the live-out value.
  ValType Kind;

  /// Offset - The byte offset in Val that is interesting for the load query.
  unsigned Offset = 0;
  /// V1, V2 - The dominating non-clobbered values of SelectVal.
  Value *V1 = nullptr, *V2 = nullptr;

  static AvailableValue get(Value *V, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val = V;
    Res.Kind = ValType::SimpleVal;
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getMI(MemIntrinsic *MI, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val = MI;
    Res.Kind = ValType::MemIntrin;
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getLoad(LoadInst *Load, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val = Load;
    Res.Kind = ValType::LoadVal;
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getUndef() {
    AvailableValue Res;
    Res.Val = nullptr;
    Res.Kind = ValType::UndefVal;
    Res.Offset = 0;
    return Res;
  }

  static AvailableValue getSelect(SelectInst *Sel, Value *V1, Value *V2) {
    AvailableValue Res;
    Res.Val = Sel;
    Res.Kind = ValType::SelectVal;
    Res.Offset = 0;
    Res.V1 = V1;
    Res.V2 = V2;
    return Res;
  }

  bool isSimpleValue() const { return Kind == ValType::SimpleVal; }
  bool isCoercedLoadValue() const { return Kind == ValType::LoadVal; }
  bool isMemIntrinValue() const { return Kind == ValType::MemIntrin; }
  bool isUndefValue() const { return Kind == ValType::UndefVal; }
  bool isSelectValue() const { return Kind == ValType::SelectVal; }

  Value *getSimpleValue() const {
    assert(isSimpleValue() && "Wrong accessor");
    return Val;
  }

  LoadInst *getCoercedLoadValue() const {
    assert(isCoercedLoadValue() && "Wrong accessor");
    return cast<LoadInst>(Val);
  }

  MemIntrinsic *getMemIntrinValue() const {
    assert(isMemIntrinValue() && "Wrong accessor");
    return cast<MemIntrinsic>(Val);
  }

  SelectInst *getSelectValue() const {
    assert(isSelectValue() && "Wrong accessor");
    return cast<SelectInst>(Val);
  }

  /// Emit code at the specified insertion point to adjust the value defined
  /// here to the specified type. This handles various coercion cases.
  Value *MaterializeAdjustedValue(LoadInst *Load, Instruction *InsertPt) const;
};

/// Represents an AvailableValue which can be rematerialized at the end of
/// the associated BasicBlock.
struct llvm::gvn::AvailableValueInBlock {
  /// BB - The basic block in question.
  BasicBlock *BB = nullptr;
  
  /// AV - The actual available value.
  AvailableValue AV;

  static AvailableValueInBlock get(BasicBlock *BB, AvailableValue &&AV) {
    AvailableValueInBlock Res;
    Res.BB = BB;
    Res.AV = std::move(AV);
    return Res;
  }

  static AvailableValueInBlock get(BasicBlock *BB, Value *V,
                                   unsigned Offset = 0) {
    return get(BB, AvailableValue::get(V, Offset));
  }

  static AvailableValueInBlock getUndef(BasicBlock *BB) {
    return get(BB, AvailableValue::getUndef());
  }

  static AvailableValueInBlock getSelect(BasicBlock *BB, SelectInst *Sel,
                                         Value *V1, Value *V2) {
    return get(BB, AvailableValue::getSelect(Sel, V1, V2));
  }

  /// Emit code at the end of this block to adjust the value defined here to
  /// the specified type. This handles various coercion cases.
  Value *MaterializeAdjustedValue(LoadInst *Load) const {
    return AV.MaterializeAdjustedValue(Load, BB->getTerminator());
  }
};

#endif // LLVM_TRANSFORMS_SCALAR_GVN_H
