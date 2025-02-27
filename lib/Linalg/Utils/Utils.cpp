//===- Utils.cpp - Utilities to support the Linalg dialect ----------------===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================
//
// This file implements utilities for the Linalg dialect.
//
//===----------------------------------------------------------------------===//

#include "mlir/Linalg/Utils/Utils.h"
#include "mlir/EDSC/Helpers.h"
#include "mlir/IR/AffineExpr.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/Linalg/IR/LinalgOps.h"
#include "mlir/Linalg/IR/LinalgTypes.h"
#include "mlir/Linalg/Passes.h"
#include "mlir/Linalg/Utils/Intrinsics.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/STLExtras.h"
#include "mlir/Transforms/FoldUtils.h"

using namespace mlir;
using namespace mlir::edsc;
using namespace mlir::edsc::intrinsics;
using namespace mlir::linalg;
using namespace mlir::linalg::intrinsics;

mlir::edsc::LoopRangeBuilder::LoopRangeBuilder(ValueHandle *iv,
                                               ValueHandle range) {
  assert(range.getType() && "expected !linalg.range type");
  assert(range.getValue()->getDefiningOp() &&
         "need operations to extract range parts");
  auto rangeOp = cast<RangeOp>(range.getValue()->getDefiningOp());
  auto lb = rangeOp.min();
  auto ub = rangeOp.max();
  auto step = rangeOp.step();
  auto forOp = OperationHandle::createOp<linalg::ForOp>(lb, ub, step);
  *iv = ValueHandle(forOp.getInductionVar());
  auto *body = forOp.getBody();
  enter(body, /*prev=*/1);
}

ValueHandle
mlir::edsc::LoopRangeBuilder::operator()(std::function<void(void)> fun) {
  if (fun)
    fun();
  exit();
  return ValueHandle::null();
}

mlir::edsc::LoopNestRangeBuilder::LoopNestRangeBuilder(
    ArrayRef<ValueHandle *> ivs, ArrayRef<ValueHandle> ranges) {
  loops.reserve(ranges.size());
  for (unsigned i = 0, e = ranges.size(); i < e; ++i) {
    loops.emplace_back(ivs[i], ranges[i]);
  }
  assert(loops.size() == ivs.size() && "Mismatch loops vs ivs size");
}

mlir::edsc::LoopNestRangeBuilder::LoopNestRangeBuilder(
    ArrayRef<ValueHandle *> ivs, ArrayRef<Value *> ranges)
    : LoopNestRangeBuilder(
          ivs, SmallVector<ValueHandle, 4>(ranges.begin(), ranges.end())) {}

ValueHandle LoopNestRangeBuilder::LoopNestRangeBuilder::operator()(
    std::function<void(void)> fun) {
  if (fun)
    fun();
  for (auto &lit : reverse(loops)) {
    lit({});
  }
  return ValueHandle::null();
}

SmallVector<Value *, 8> mlir::linalg::getViewSizes(LinalgOp &linalgOp) {
  SmallVector<Value *, 8> res;
  for (auto v : linalgOp.getInputsAndOutputs()) {
    ViewType t = v->getType().cast<ViewType>();
    for (unsigned i = 0; i < t.getRank(); ++i)
      res.push_back(linalg::intrinsics::dim(v, i));
  }
  return res;
}

static Value *emitOrFoldComposedAffineApply(OpBuilder &b, Location loc,
                                            AffineMap map,
                                            ArrayRef<Value *> operandsRef,
                                            OperationFolder &state) {
  SmallVector<Value *, 4> operands(operandsRef.begin(), operandsRef.end());
  fullyComposeAffineMapAndOperands(&map, &operands);
  return state.create<AffineApplyOp>(b, loc, map, operands);
}

SmallVector<Value *, 4> mlir::linalg::applyMapToValues(OpBuilder &b,
                                                       Location loc,
                                                       AffineMap map,
                                                       ArrayRef<Value *> values,
                                                       OperationFolder &state) {
  SmallVector<Value *, 4> res;
  res.reserve(map.getNumResults());
  unsigned numDims = map.getNumDims();
  // For each `expr` in `map`, applies the `expr` to the values extracted from
  // ranges. If the resulting application can be folded into a Value*, the
  // folding occurs eagerly. Otherwise, an affine.apply operation is emitted.
  for (auto expr : map.getResults()) {
    AffineMap map = AffineMap::get(numDims, 0, expr);
    res.push_back(emitOrFoldComposedAffineApply(b, loc, map, values, state));
  }
  return res;
}
