//===- EmulateAtomics.cpp - Emulate unsupported AMDGPU atomics ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/AMDGPU/Transforms/Passes.h"

#include "mlir/Dialect/AMDGPU/IR/AMDGPUDialect.h"
#include "mlir/Dialect/AMDGPU/Utils/Chipset.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/TypeUtilities.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir::amdgpu {
#define GEN_PASS_DEF_AMDGPUEMULATEATOMICSPASS
#include "mlir/Dialect/AMDGPU/Transforms/Passes.h.inc"
} // namespace mlir::amdgpu

using namespace mlir;
using namespace mlir::amdgpu;

namespace {
struct AmdgpuEmulateAtomicsPass
    : public amdgpu::impl::AmdgpuEmulateAtomicsPassBase<
          AmdgpuEmulateAtomicsPass> {
  using AmdgpuEmulateAtomicsPassBase<
      AmdgpuEmulateAtomicsPass>::AmdgpuEmulateAtomicsPassBase;
  void runOnOperation() override;
};

template <typename AtomicOp, typename ArithOp>
struct RawBufferAtomicByCasPattern : public OpConversionPattern<AtomicOp> {
  using OpConversionPattern<AtomicOp>::OpConversionPattern;
  using Adaptor = typename AtomicOp::Adaptor;

  LogicalResult
  matchAndRewrite(AtomicOp atomicOp, Adaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override;
};
} // namespace

namespace {
enum class DataArgAction : unsigned char {
  Duplicate,
  Drop,
};
} // namespace

// Fix up the fact that, when we're migrating from a general bugffer atomic
// to a load or to a CAS, the number of openrands, and thus the number of
// entries needed in operandSegmentSizes, needs to change. We use this method
// because we'd like to preserve unknown attributes on the atomic instead of
// discarding them.
static void patchOperandSegmentSizes(ArrayRef<NamedAttribute> attrs,
                                     SmallVectorImpl<NamedAttribute> &newAttrs,
                                     DataArgAction action) {
  newAttrs.reserve(attrs.size());
  for (NamedAttribute attr : attrs) {
    if (attr.getName().getValue() != "operandSegmentSizes") {
      newAttrs.push_back(attr);
      continue;
    }
    auto segmentAttr = cast<DenseI32ArrayAttr>(attr.getValue());
    MLIRContext *context = segmentAttr.getContext();
    DenseI32ArrayAttr newSegments;
    switch (action) {
    case DataArgAction::Drop:
      newSegments = DenseI32ArrayAttr::get(
          context, segmentAttr.asArrayRef().drop_front());
      break;
    case DataArgAction::Duplicate: {
      SmallVector<int32_t> newVals;
      ArrayRef<int32_t> oldVals = segmentAttr.asArrayRef();
      newVals.push_back(oldVals[0]);
      newVals.append(oldVals.begin(), oldVals.end());
      newSegments = DenseI32ArrayAttr::get(context, newVals);
      break;
    }
    }
    newAttrs.push_back(NamedAttribute(attr.getName(), newSegments));
  }
}

// A helper function to flatten a vector value to a scalar containing its bits,
// returning the value itself if othetwise.
static Value flattenVecToBits(ConversionPatternRewriter &rewriter, Location loc,
                              Value val) {
  auto vectorType = dyn_cast<VectorType>(val.getType());
  if (!vectorType)
    return val;

  int64_t bitwidth =
      vectorType.getElementTypeBitWidth() * vectorType.getNumElements();
  Type allBitsType = rewriter.getIntegerType(bitwidth);
  auto allBitsVecType = VectorType::get({1}, allBitsType);
  Value bitcast = vector::BitCastOp::create(rewriter, loc, allBitsVecType, val);
  Value scalar = vector::ExtractOp::create(rewriter, loc, bitcast, 0);
  return scalar;
}

template <typename AtomicOp, typename ArithOp>
LogicalResult RawBufferAtomicByCasPattern<AtomicOp, ArithOp>::matchAndRewrite(
    AtomicOp atomicOp, Adaptor adaptor,
    ConversionPatternRewriter &rewriter) const {
  Location loc = atomicOp.getLoc();

  ArrayRef<NamedAttribute> origAttrs = atomicOp->getAttrs();
  ValueRange operands = adaptor.getOperands();
  Value data = operands.take_front()[0];
  ValueRange invariantArgs = operands.drop_front();
  Type dataType = data.getType();

  SmallVector<NamedAttribute> loadAttrs;
  patchOperandSegmentSizes(origAttrs, loadAttrs, DataArgAction::Drop);
  Value initialLoad = RawBufferLoadOp::create(rewriter, loc, dataType,
                                              invariantArgs, loadAttrs);
  Block *currentBlock = rewriter.getInsertionBlock();
  Block *afterAtomic =
      rewriter.splitBlock(currentBlock, rewriter.getInsertionPoint());
  Block *loopBlock = rewriter.createBlock(afterAtomic, {dataType}, {loc});

  rewriter.setInsertionPointToEnd(currentBlock);
  cf::BranchOp::create(rewriter, loc, loopBlock, initialLoad);

  rewriter.setInsertionPointToEnd(loopBlock);
  Value prevLoad = loopBlock->getArgument(0);
  Value operated = ArithOp::create(rewriter, loc, data, prevLoad);
  dataType = operated.getType();

  SmallVector<NamedAttribute> cmpswapAttrs;
  patchOperandSegmentSizes(origAttrs, cmpswapAttrs, DataArgAction::Duplicate);
  SmallVector<Value> cmpswapArgs = {operated, prevLoad};
  cmpswapArgs.append(invariantArgs.begin(), invariantArgs.end());
  Value atomicRes = RawBufferAtomicCmpswapOp::create(rewriter, loc, dataType,
                                                     cmpswapArgs, cmpswapAttrs);

  // We care about exact bitwise equality here, so do some bitcasts.
  // These will fold away during lowering to the ROCDL dialect, where
  // an int->float bitcast is introduced to account for the fact that cmpswap
  // only takes integer arguments.

  Value prevLoadForCompare = flattenVecToBits(rewriter, loc, prevLoad);
  Value atomicResForCompare = flattenVecToBits(rewriter, loc, atomicRes);
  if (auto floatDataTy = dyn_cast<FloatType>(dataType)) {
    Type equivInt = rewriter.getIntegerType(floatDataTy.getWidth());
    prevLoadForCompare =
        arith::BitcastOp::create(rewriter, loc, equivInt, prevLoad);
    atomicResForCompare =
        arith::BitcastOp::create(rewriter, loc, equivInt, atomicRes);
  }
  Value canLeave =
      arith::CmpIOp::create(rewriter, loc, arith::CmpIPredicate::eq,
                            atomicResForCompare, prevLoadForCompare);
  cf::CondBranchOp::create(rewriter, loc, canLeave, afterAtomic, ValueRange{},
                           loopBlock, atomicRes);
  rewriter.eraseOp(atomicOp);
  return success();
}

void mlir::amdgpu::populateAmdgpuEmulateAtomicsPatterns(
    ConversionTarget &target, RewritePatternSet &patterns, Chipset chipset,
    PatternBenefit benefit) {
  // gfx10 has no atomic adds.
  if (chipset.majorVersion == 10 || chipset < Chipset(9, 0, 8)) {
    target.addIllegalOp<RawBufferAtomicFaddOp>();
  }
  // gfx11 has no fp16 atomics
  if (chipset.majorVersion == 11) {
    target.addDynamicallyLegalOp<RawBufferAtomicFaddOp>(
        [](RawBufferAtomicFaddOp op) -> bool {
          Type elemType = getElementTypeOrSelf(op.getValue().getType());
          return !isa<Float16Type, BFloat16Type>(elemType);
        });
  }
  // gfx9 has no to a very limited support for floating-point min and max.
  if (chipset.majorVersion == 9) {
    if (chipset >= Chipset(9, 0, 0xa)) {
      // gfx90a supports f64 max (and min, but we don't have a min wrapper right
      // now) but all other types need to be emulated.
      target.addDynamicallyLegalOp<RawBufferAtomicFmaxOp>(
          [](RawBufferAtomicFmaxOp op) -> bool {
            return op.getValue().getType().isF64();
          });
    } else {
      target.addIllegalOp<RawBufferAtomicFmaxOp>();
    }
    // TODO(https://github.com/llvm/llvm-project/issues/129206): Refactor
    // this to avoid hardcoding ISA version: gfx950 has bf16 atomics.
    if (chipset < Chipset(9, 5, 0)) {
      target.addDynamicallyLegalOp<RawBufferAtomicFaddOp>(
          [](RawBufferAtomicFaddOp op) -> bool {
            Type elemType = getElementTypeOrSelf(op.getValue().getType());
            return !isa<BFloat16Type>(elemType);
          });
    }
  }
  patterns.add<
      RawBufferAtomicByCasPattern<RawBufferAtomicFaddOp, arith::AddFOp>,
      RawBufferAtomicByCasPattern<RawBufferAtomicFmaxOp, arith::MaximumFOp>,
      RawBufferAtomicByCasPattern<RawBufferAtomicSmaxOp, arith::MaxSIOp>,
      RawBufferAtomicByCasPattern<RawBufferAtomicUminOp, arith::MinUIOp>>(
      patterns.getContext(), benefit);
}

void AmdgpuEmulateAtomicsPass::runOnOperation() {
  Operation *op = getOperation();
  FailureOr<Chipset> maybeChipset = Chipset::parse(chipset);
  if (failed(maybeChipset)) {
    emitError(op->getLoc(), "Invalid chipset name: " + chipset);
    return signalPassFailure();
  }

  MLIRContext &ctx = getContext();
  ConversionTarget target(ctx);
  RewritePatternSet patterns(&ctx);
  target.markUnknownOpDynamicallyLegal(
      [](Operation *op) -> bool { return true; });

  populateAmdgpuEmulateAtomicsPatterns(target, patterns, *maybeChipset);
  if (failed(applyPartialConversion(op, target, std::move(patterns))))
    return signalPassFailure();
}
