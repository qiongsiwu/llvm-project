//===- LegalizeForExport.cpp - Prepare for translation to LLVM IR ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/LLVMIR/Transforms/LegalizeForExport.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/Transforms/DIExpressionLegalization.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"

namespace mlir {
namespace LLVM {
#define GEN_PASS_DEF_LLVMLEGALIZEFOREXPORTPASS
#include "mlir/Dialect/LLVMIR/Transforms/Passes.h.inc"
} // namespace LLVM
} // namespace mlir

using namespace mlir;

/// If the given block has the same successor with different arguments,
/// introduce dummy successor blocks so that all successors of the given block
/// are different.
static void ensureDistinctSuccessors(Block &bb) {
  // Early exit if the block cannot have successors.
  if (bb.empty() || !bb.back().mightHaveTrait<OpTrait::IsTerminator>())
    return;

  auto *terminator = bb.getTerminator();

  // Find repeated successors with arguments.
  llvm::SmallDenseMap<Block *, SmallVector<int, 4>> successorPositions;
  for (int i = 0, e = terminator->getNumSuccessors(); i < e; ++i) {
    Block *successor = terminator->getSuccessor(i);
    // Blocks with no arguments are safe even if they appear multiple times
    // because they don't need PHI nodes.
    if (successor->getNumArguments() == 0)
      continue;
    successorPositions[successor].push_back(i);
  }

  // If a successor appears for the second or more time in the terminator,
  // create a new dummy block that unconditionally branches to the original
  // destination, and retarget the terminator to branch to this new block.
  // There is no need to pass arguments to the dummy block because it will be
  // dominated by the original block and can therefore use any values defined in
  // the original block.
  OpBuilder builder(terminator->getContext());
  for (const auto &successor : successorPositions) {
    // Start from the second occurrence of a block in the successor list.
    for (int position : llvm::drop_begin(successor.second, 1)) {
      Block *dummyBlock = builder.createBlock(bb.getParent());
      terminator->setSuccessor(dummyBlock, position);
      for (BlockArgument arg : successor.first->getArguments())
        dummyBlock->addArgument(arg.getType(), arg.getLoc());
      builder.create<LLVM::BrOp>(terminator->getLoc(),
                                 dummyBlock->getArguments(), successor.first);
    }
  }
}

void mlir::LLVM::ensureDistinctSuccessors(Operation *op) {
  op->walk([](Operation *nested) {
    for (Region &region : llvm::make_early_inc_range(nested->getRegions())) {
      for (Block &block : llvm::make_early_inc_range(region)) {
        ::ensureDistinctSuccessors(block);
      }
    }
  });
}

namespace {
struct LegalizeForExportPass
    : public LLVM::impl::LLVMLegalizeForExportPassBase<LegalizeForExportPass> {
  void runOnOperation() override {
    LLVM::ensureDistinctSuccessors(getOperation());
    LLVM::legalizeDIExpressionsRecursively(getOperation());
  }
};
} // namespace
