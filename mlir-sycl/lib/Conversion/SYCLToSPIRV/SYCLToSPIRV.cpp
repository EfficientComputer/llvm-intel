//===- SYCLToSPIRV.cpp - SYCL to SPIRV Patterns ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements patterns to convert SYCL dialect to SPIRV dialect.
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/SYCLToSPIRV/SYCLToSPIRV.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Arith/Utils/Utils.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/Transforms/SPIRVConversion.h"
#include "mlir/Dialect/SYCL/IR/SYCLOps.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/TypeSwitch.h"

namespace mlir {
#define GEN_PASS_DEF_CONVERTSYCLTOSPIRV
#include "mlir/Conversion/SYCLPasses.h.inc"
#undef GEN_PASS_DEF_CONVERTSYCLTOSPIRV
} // namespace mlir

using namespace mlir;
using namespace mlir::sycl;

namespace {
template <typename OpTy> struct spirv_counterpart_builtin;

template <> struct spirv_counterpart_builtin<SYCLGlobalOffsetOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::GlobalOffset};
};

template <> struct spirv_counterpart_builtin<SYCLNumWorkGroupsOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::NumWorkgroups};
};

template <> struct spirv_counterpart_builtin<SYCLSubGroupMaxSizeOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::SubgroupMaxSize};
};

template <> struct spirv_counterpart_builtin<SYCLSubGroupLocalIDOp> {
  constexpr static spirv::BuiltIn value{
      spirv::BuiltIn::SubgroupLocalInvocationId};
};

template <> struct spirv_counterpart_builtin<SYCLWorkGroupIDOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::WorkgroupId};
};

template <> struct spirv_counterpart_builtin<SYCLNumWorkItemsOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::GlobalSize};
};

template <> struct spirv_counterpart_builtin<SYCLWorkGroupSizeOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::WorkgroupSize};
};

template <> struct spirv_counterpart_builtin<SYCLLocalIDOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::LocalInvocationId};
};

template <> struct spirv_counterpart_builtin<SYCLGlobalIDOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::GlobalInvocationId};
};

template <> struct spirv_counterpart_builtin<SYCLSubGroupIDOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::SubgroupId};
};

template <> struct spirv_counterpart_builtin<SYCLNumSubGroupsOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::NumSubgroups};
};

template <> struct spirv_counterpart_builtin<SYCLSubGroupSizeOp> {
  constexpr static spirv::BuiltIn value{spirv::BuiltIn::SubgroupSize};
};

template <typename OpTy>
inline constexpr spirv::BuiltIn spirv_counterpart_builtin_v =
    spirv_counterpart_builtin<OpTy>::value;

static Value getBuiltinVariableValue(Operation *op, spirv::BuiltIn builtin,
                                     Type integerType, OpBuilder &builder) {
  // These should be target-dependent in the future.
  constexpr StringLiteral spirvBuiltinPrefix = "__spirv_BuiltIn";
  constexpr StringLiteral spirvBuiltinSuffix = "";
  return spirv::getBuiltinVariableValue(op, builtin, integerType, builder,
                                        spirvBuiltinPrefix, spirvBuiltinSuffix);
}

/// Returns the result of creating an operation to get a reference to an element
/// of a sycl::id or sycl::range.
Value createGetOp(OpBuilder &builder, Location loc, Type dimMtTy, Value res,
                  Value index) {
  return TypeSwitch<Type, Value>(
             cast<MemRefType>(res.getType()).getElementType())
      .Case<IDType, RangeType>([&](auto arg) {
        // `this` type
        using ArgTy = decltype(arg);
        // Operation type depending on ArgTy
        using OpTy = std::conditional_t<std::is_same_v<ArgTy, IDType>,
                                        SYCLIDGetOp, SYCLRangeGetOp>;
        return builder.create<OpTy>(loc, dimMtTy, res, index);
      });
}

/// Extracts component \p i from SPIRV composite \p values.
Value getDimension(OpBuilder &builder, Location loc, Value values, int64_t i) {
  return builder.create<spirv::CompositeExtractOp>(loc, values, i);
}

/// Converts n-dimensional operations of type \tparam OpTy to calls to \tparam
/// builtin SPIR-V builtin.
template <typename OpTy,
          spirv::BuiltIn builtin = spirv_counterpart_builtin_v<OpTy>>
class GridOpPattern : public OpConversionPattern<OpTy> {
public:
  using OpConversionPattern<OpTy>::OpConversionPattern;

protected:
  constexpr static spirv::BuiltIn spirvBuiltin{builtin};
};

template <unsigned Dimensions>
static std::enable_if_t<(1 <= Dimensions && Dimensions < 4), int64_t>
mirrorIndexCalc(int64_t index);

template <> int64_t mirrorIndexCalc<1>(int64_t index) { return 0; }

template <> int64_t mirrorIndexCalc<2>(int64_t index) { return !index; }

template <> int64_t mirrorIndexCalc<3>(int64_t index) { return 2 - index; }

template <unsigned Dimensions>
static std::enable_if_t<(1 <= Dimensions && Dimensions < 4), int64_t>
mirrorIndex(int64_t index) {
  assert(0 <= index && index < Dimensions && "Invalid index");
  return mirrorIndexCalc<Dimensions>(index);
}

static llvm::function_ref<int64_t(int64_t)> getMirror(unsigned dimensions) {
  switch (dimensions) {
  case 1:
    return mirrorIndex<1>;
  case 2:
    return mirrorIndex<2>;
  case 3:
    return mirrorIndex<3>;
  default:
    llvm_unreachable("Invalid number of dimensions");
  }
}

/// Replace \p op with a sequence of operations that:
/// 1. Allocate a new object of the result type in the stack;
/// 2. Load the object;
/// 3. Initialize the dimensions of the object with the values in builtin \p
/// builtin.
void rewriteND(Operation *op, spirv::BuiltIn builtin,
               TypeConverter &typeConverter,
               ConversionPatternRewriter &rewriter) {
  // This conversion is platform dependent
  const auto dimensions = getDimensions(op->getResultTypes()[0]);
  const auto values = ::getBuiltinVariableValue(
      op, builtin, typeConverter.convertType(rewriter.getIndexType()),
      rewriter);
  const auto loc = op->getLoc();
  const auto getIndexTy = rewriter.getIntegerType(32);
  const auto targetIndexType = rewriter.getI64Type();
  const auto dimMtTy = MemRefType::get(dimensions, targetIndexType);
  // Allocate
  const auto resTy = op->getResultTypes()[0];
  const Value res =
      rewriter.create<memref::AllocaOp>(loc, MemRefType::get(1, resTy));
  // Load
  const Value zero = rewriter.create<arith::ConstantIndexOp>(loc, 0);
  auto mirrorer = getMirror(dimensions);
  // Initialize
  for (int64_t i = 0; i < dimensions; ++i) {
    const Value index =
        rewriter.create<arith::ConstantIntOp>(loc, i, getIndexTy);
    const auto val = convertScalarToDtype(
        rewriter, loc, getDimension(rewriter, loc, values, mirrorer(i)),
        targetIndexType,
        /*isUnsignedCast*/ false);
    const auto ptr = createGetOp(rewriter, loc, dimMtTy, res, index);
    rewriter.create<memref::StoreOp>(loc, val, ptr, zero);
  }
  rewriter.replaceOpWithNewOp<memref::LoadOp>(op, res, zero);
}

/// Converts n-dimensional operations of type \tparam OpTy not being passed an
/// argument to a call to a SPIRV builtin.
template <typename OpTy> class NDGridOpPattern : public GridOpPattern<OpTy> {
public:
  using GridOpPattern<OpTy>::GridOpPattern;

  LogicalResult
  matchAndRewrite(OpTy op, typename OpTy::Adaptor opAdaptor,
                  ConversionPatternRewriter &rewriter) const final {
    rewriteND(op, GridOpPattern<OpTy>::spirvBuiltin,
              *GridOpPattern<OpTy>::getTypeConverter(), rewriter);
    return success();
  }
};

static void rewrite1D(Operation *op, spirv::BuiltIn builtin,
                      ConversionPatternRewriter &rewriter) {
  rewriter.replaceOp(op, ::getBuiltinVariableValue(
                             op, builtin, rewriter.getI32Type(), rewriter));
}

/// Converts one-dimensional operations of type \tparam OpTy to calls to a
/// SPIRV dialect builtin.
template <typename OpTy>
class SingleDimGridOpPattern : public GridOpPattern<OpTy> {
public:
  using GridOpPattern<OpTy>::GridOpPattern;

  LogicalResult
  matchAndRewrite(OpTy op, typename OpTy::Adaptor,
                  ConversionPatternRewriter &rewriter) const final {
    rewrite1D(op, GridOpPattern<OpTy>::spirvBuiltin, rewriter);
    return success();
  }
};
} // namespace

void mlir::populateSYCLToSPIRVConversionPatterns(TypeConverter &typeConverter,
                                                 RewritePatternSet &patterns) {
  auto *context = patterns.getContext();
  patterns.add<
      NDGridOpPattern<SYCLGlobalOffsetOp>, NDGridOpPattern<SYCLNumWorkGroupsOp>,
      NDGridOpPattern<SYCLWorkGroupIDOp>, NDGridOpPattern<SYCLNumWorkItemsOp>,
      NDGridOpPattern<SYCLWorkGroupSizeOp>, NDGridOpPattern<SYCLLocalIDOp>,
      NDGridOpPattern<SYCLGlobalIDOp>,
      SingleDimGridOpPattern<SYCLSubGroupMaxSizeOp>,
      SingleDimGridOpPattern<SYCLSubGroupLocalIDOp>,
      SingleDimGridOpPattern<SYCLSubGroupIDOp>,
      SingleDimGridOpPattern<SYCLNumSubGroupsOp>,
      SingleDimGridOpPattern<SYCLSubGroupSizeOp>>(typeConverter, context);
}

namespace {
/// A pass converting MLIR SYCL operations into LLVM dialect.
class ConvertSYCLToSPIRVPass
    : public impl::ConvertSYCLToSPIRVBase<ConvertSYCLToSPIRVPass> {
  void runOnOperation() override;
};
} // namespace

void ConvertSYCLToSPIRVPass::runOnOperation() {
  auto *context = &getContext();
  auto module = getOperation();

  module.walk([&](gpu::GPUModuleOp gpuModule) {
    // We walk the different GPU modules looking for different SPIRV target
    // environment definitions. Currently, this does not affect the behavior of
    // this pass.
    RewritePatternSet patterns(context);
    ConversionTarget target(*context);
    auto targetAttr = spirv::lookupTargetEnvOrDefault(gpuModule);
    SPIRVTypeConverter typeConverter(targetAttr);

    populateSYCLToSPIRVConversionPatterns(typeConverter, patterns);

    target.addLegalDialect<arith::ArithDialect>();
    target.addLegalDialect<spirv::SPIRVDialect>();
    target.addLegalDialect<memref::MemRefDialect>();
    target.addLegalDialect<vector::VectorDialect>();

    target.addDynamicallyLegalDialect<SYCLDialect>([](auto *op) {
      return !isa<SYCLGlobalOffsetOp, SYCLNumWorkGroupsOp,
                  SYCLSubGroupLocalIDOp, SYCLSubGroupMaxSizeOp,
                  SYCLWorkGroupIDOp, SYCLNumWorkItemsOp, SYCLWorkGroupSizeOp,
                  SYCLLocalIDOp, SYCLGlobalIDOp, SYCLSubGroupIDOp,
                  SYCLNumSubGroupsOp, SYCLSubGroupSizeOp>(op);
    });

    if (failed(applyPartialConversion(gpuModule, target, std::move(patterns))))
      signalPassFailure();
  });
}
