//===- LoopAnalysis.cpp - Misc loop analysis routines //-------------------===//
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
// This file implements miscellaneous loop analysis routines.
//
//===----------------------------------------------------------------------===//

#include "mlir/Analysis/LoopAnalysis.h"

#include "mlir/AffineOps/AffineOps.h"
#include "mlir/Analysis/AffineAnalysis.h"
#include "mlir/Analysis/AffineStructures.h"
#include "mlir/Analysis/NestedMatcher.h"
#include "mlir/Analysis/VectorAnalysis.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Operation.h"
#include "mlir/StandardOps/Ops.h"
#include "mlir/Support/Functional.h"
#include "mlir/Support/MathExtras.h"
#include "mlir/VectorOps/VectorOps.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include <type_traits>

using namespace mlir;

/// Returns the trip count of the loop as an affine expression if the latter is
/// expressible as an affine expression, and nullptr otherwise. The trip count
/// expression is simplified before returning. This method only utilizes map
/// composition to construct lower and upper bounds before computing the trip
/// count expressions.
// TODO(mlir-team): this should be moved into 'Transforms/' and be replaced by a
// pure analysis method relying on FlatAffineConstraints; the latter will also
// be more powerful (since both inequalities and equalities will be considered).
void mlir::buildTripCountMapAndOperands(
    AffineForOp forOp, AffineMap *map,
    SmallVectorImpl<Value *> *tripCountOperands) {
  int64_t loopSpan;

  int64_t step = forOp.getStep();
  OpBuilder b(forOp.getOperation());

  if (forOp.hasConstantBounds()) {
    int64_t lb = forOp.getConstantLowerBound();
    int64_t ub = forOp.getConstantUpperBound();
    loopSpan = ub - lb;
    if (loopSpan < 0)
      loopSpan = 0;
    *map = b.getConstantAffineMap(ceilDiv(loopSpan, step));
    tripCountOperands->clear();
    return;
  }
  auto lbMap = forOp.getLowerBoundMap();
  auto ubMap = forOp.getUpperBoundMap();
  if (lbMap.getNumResults() != 1) {
    *map = AffineMap();
    return;
  }
  SmallVector<Value *, 4> lbOperands(forOp.getLowerBoundOperands());
  SmallVector<Value *, 4> ubOperands(forOp.getUpperBoundOperands());
  auto lb = b.create<AffineApplyOp>(forOp.getLoc(), lbMap, lbOperands);
  SmallVector<Value *, 4> ubs;
  ubs.reserve(ubMap.getNumResults());
  for (auto ubExpr : ubMap.getResults())
    ubs.push_back(b.create<AffineApplyOp>(
        forOp.getLoc(),
        b.getAffineMap(ubMap.getNumDims(), ubMap.getNumSymbols(), {ubExpr}),
        ubOperands));

  tripCountOperands->clear();
  tripCountOperands->reserve(1 + ubs.size());
  tripCountOperands->push_back(lb);
  tripCountOperands->append(ubs.begin(), ubs.end());

  SmallVector<AffineExpr, 4> tripCountExprs(ubs.size());
  for (unsigned i = 0, e = ubs.size(); i < e; i++)
    tripCountExprs[i] =
        (b.getAffineDimExpr(1 + i) - b.getAffineDimExpr(0)).ceilDiv(step);
  *map = b.getAffineMap(1 + ubs.size(), 0, tripCountExprs);

  fullyComposeAffineMapAndOperands(map, tripCountOperands);
  *map = simplifyAffineMap(*map);
  canonicalizeMapAndOperands(map, tripCountOperands);
  // Remove any affine.apply's that became dead as a result of composition,
  // simplification, and canonicalization above.
  for (auto *v : ubs)
    if (v->use_empty())
      v->getDefiningOp()->erase();
  if (lb.use_empty())
    lb.erase();
}

/// Returns the trip count of the loop if it's a constant, None otherwise. This
/// method uses affine expression analysis (in turn using getTripCount) and is
/// able to determine constant trip count in non-trivial cases.
// FIXME(mlir-team): this is really relying on buildTripCountMapAndOperands;
// being an analysis utility, it shouldn't. Replace with a version that just
// works with analysis structures (FlatAffineConstraints) and thus doesn't
// update the IR.
llvm::Optional<uint64_t> mlir::getConstantTripCount(AffineForOp forOp) {
  SmallVector<Value *, 4> operands;
  AffineMap map;
  buildTripCountMapAndOperands(forOp, &map, &operands);

  if (!map)
    return None;

  // Take the min if all trip counts are constant.
  Optional<uint64_t> tripCount;
  for (auto resultExpr : map.getResults()) {
    if (auto constExpr = resultExpr.dyn_cast<AffineConstantExpr>()) {
      if (tripCount.hasValue())
        tripCount = std::min(tripCount.getValue(),
                             static_cast<uint64_t>(constExpr.getValue()));
      else
        tripCount = constExpr.getValue();
    } else
      return None;
  }
  return tripCount;
}

/// Returns the greatest known integral divisor of the trip count. Affine
/// expression analysis is used (indirectly through getTripCount), and
/// this method is thus able to determine non-trivial divisors.
uint64_t mlir::getLargestDivisorOfTripCount(AffineForOp forOp) {
  SmallVector<Value *, 4> operands;
  AffineMap map;
  buildTripCountMapAndOperands(forOp, &map, &operands);

  if (!map)
    return 1;

  // The largest divisor of the trip count is the GCD of the individual largest
  // divisors.
  assert(map.getNumResults() >= 1 && "expected one or more results");
  Optional<uint64_t> gcd;
  for (auto resultExpr : map.getResults()) {
    uint64_t thisGcd;
    if (auto constExpr = resultExpr.dyn_cast<AffineConstantExpr>()) {
      uint64_t tripCount = constExpr.getValue();
      // 0 iteration loops (greatest divisor is 2^64 - 1).
      if (tripCount == 0)
        thisGcd = std::numeric_limits<uint64_t>::max();
      else
        // The greatest divisor is the trip count.
        thisGcd = tripCount;
    } else {
      // Trip count is not a known constant; return its largest known divisor.
      thisGcd = resultExpr.getLargestKnownDivisor();
    }
    if (gcd.hasValue())
      gcd = llvm::GreatestCommonDivisor64(gcd.getValue(), thisGcd);
    else
      gcd = thisGcd;
  }
  assert(gcd.hasValue() && "value expected per above logic");
  return gcd.getValue();
}

bool mlir::isAccessInvariant(Value *iv, Value *index) {
  assert(isForInductionVar(iv) && "iv must be a AffineForOp");
  assert(index->getType().isa<IndexType>() && "index must be of IndexType");
  SmallVector<Operation *, 4> affineApplyOps;
  getReachableAffineApplyOps({index}, affineApplyOps);

  if (affineApplyOps.empty()) {
    // Pointer equality test because of Value pointer semantics.
    return index != iv;
  }

  if (affineApplyOps.size() > 1) {
    affineApplyOps[0]->emitRemark(
        "CompositionAffineMapsPass must have been run: there should be at most "
        "one AffineApplyOp, returning false conservatively.");
    return false;
  }

  auto composeOp = cast<AffineApplyOp>(affineApplyOps[0]);
  // We need yet another level of indirection because the `dim` index of the
  // access may not correspond to the `dim` index of composeOp.
  return !(AffineValueMap(composeOp).isFunctionOf(0, iv));
}

llvm::DenseSet<Value *>
mlir::getInvariantAccesses(Value *iv, llvm::ArrayRef<Value *> indices) {
  llvm::DenseSet<Value *> res;
  for (unsigned idx = 0, n = indices.size(); idx < n; ++idx) {
    auto *val = indices[idx];
    if (isAccessInvariant(iv, val)) {
      res.insert(val);
    }
  }
  return res;
}

/// Given:
///   1. an induction variable `iv` of type AffineForOp;
///   2. a `memoryOp` of type const LoadOp& or const StoreOp&;
/// determines whether `memoryOp` has a contiguous access along `iv`. Contiguous
/// is defined as either invariant or varying only along a unique MemRef dim.
/// Upon success, the unique MemRef dim is written in `memRefDim` (or -1 to
/// convey the memRef access is invariant along `iv`).
///
/// Prerequisites:
///   1. `memRefDim` ~= nullptr;
///   2. `iv` of the proper type;
///   3. the MemRef accessed by `memoryOp` has no layout map or at most an
///      identity layout map.
///
/// Currently only supports no layoutMap or identity layoutMap in the MemRef.
/// Returns false if the MemRef has a non-identity layoutMap or more than 1
/// layoutMap. This is conservative.
///
// TODO(ntv): check strides.
template <typename LoadOrStoreOp>
static bool isContiguousAccess(Value *iv, LoadOrStoreOp memoryOp,
                               int *memRefDim) {
  static_assert(std::is_same<LoadOrStoreOp, LoadOp>::value ||
                    std::is_same<LoadOrStoreOp, StoreOp>::value,
                "Must be called on either const LoadOp & or const StoreOp &");
  assert(memRefDim && "memRefDim == nullptr");
  auto memRefType = memoryOp.getMemRefType();

  auto layoutMap = memRefType.getAffineMaps();
  // TODO(ntv): remove dependence on Builder once we support non-identity
  // layout map.
  Builder b(memoryOp.getContext());
  if (layoutMap.size() >= 2 ||
      (layoutMap.size() == 1 &&
       !(layoutMap[0] ==
         b.getMultiDimIdentityMap(layoutMap[0].getNumDims())))) {
    return memoryOp.emitError("NYI: non-trivial layoutMap"), false;
  }

  int uniqueVaryingIndexAlongIv = -1;
  auto indices = memoryOp.getIndices();
  unsigned numIndices = llvm::size(indices);
  unsigned dim = 0;
  for (auto *index : indices) {
    if (!isAccessInvariant(iv, index)) {
      if (uniqueVaryingIndexAlongIv != -1) {
        // 2+ varying indices -> do not vectorize along iv.
        return false;
      }
      uniqueVaryingIndexAlongIv = dim;
    }
    ++dim;
  }

  if (uniqueVaryingIndexAlongIv == -1)
    *memRefDim = -1;
  else
    *memRefDim = numIndices - (uniqueVaryingIndexAlongIv + 1);

  return true;
}

template <typename LoadOrStoreOpPointer>
static bool isVectorElement(LoadOrStoreOpPointer memoryOp) {
  auto memRefType = memoryOp.getMemRefType();
  return memRefType.getElementType().template isa<VectorType>();
}

static bool isVectorTransferReadOrWrite(Operation &op) {
  return isa<VectorTransferReadOp>(op) || isa<VectorTransferWriteOp>(op);
}

using VectorizableOpFun = std::function<bool(AffineForOp, Operation &)>;

static bool
isVectorizableLoopBodyWithOpCond(AffineForOp loop,
                                 VectorizableOpFun isVectorizableOp) {
  auto *forOp = loop.getOperation();

  // No vectorization across conditionals for now.
  auto conditionals = matcher::If();
  SmallVector<NestedMatch, 8> conditionalsMatched;
  conditionals.match(forOp, &conditionalsMatched);
  if (!conditionalsMatched.empty()) {
    return false;
  }

  // No vectorization across unknown regions.
  auto regions = matcher::Op([](Operation &op) -> bool {
    return op.getNumRegions() != 0 &&
           !(isa<AffineIfOp>(op) || isa<AffineForOp>(op));
  });
  SmallVector<NestedMatch, 8> regionsMatched;
  regions.match(forOp, &regionsMatched);
  if (!regionsMatched.empty()) {
    return false;
  }

  auto vectorTransfers = matcher::Op(isVectorTransferReadOrWrite);
  SmallVector<NestedMatch, 8> vectorTransfersMatched;
  vectorTransfers.match(forOp, &vectorTransfersMatched);
  if (!vectorTransfersMatched.empty()) {
    return false;
  }

  auto loadAndStores = matcher::Op(matcher::isLoadOrStore);
  SmallVector<NestedMatch, 8> loadAndStoresMatched;
  loadAndStores.match(forOp, &loadAndStoresMatched);
  for (auto ls : loadAndStoresMatched) {
    auto *op = ls.getMatchedOperation();
    auto load = dyn_cast<LoadOp>(op);
    auto store = dyn_cast<StoreOp>(op);
    // Only scalar types are considered vectorizable, all load/store must be
    // vectorizable for a loop to qualify as vectorizable.
    // TODO(ntv): ponder whether we want to be more general here.
    bool vector = load ? isVectorElement(load) : isVectorElement(store);
    if (vector) {
      return false;
    }
    if (isVectorizableOp && !isVectorizableOp(loop, *op)) {
      return false;
    }
  }
  return true;
}

bool mlir::isVectorizableLoopBody(AffineForOp loop, int *memRefDim) {
  VectorizableOpFun fun([memRefDim](AffineForOp loop, Operation &op) {
    auto load = dyn_cast<LoadOp>(op);
    auto store = dyn_cast<StoreOp>(op);
    return load ? isContiguousAccess(loop.getInductionVar(), load, memRefDim)
                : isContiguousAccess(loop.getInductionVar(), store, memRefDim);
  });
  return isVectorizableLoopBodyWithOpCond(loop, fun);
}

bool mlir::isVectorizableLoopBody(AffineForOp loop) {
  return isVectorizableLoopBodyWithOpCond(loop, nullptr);
}

/// Checks whether SSA dominance would be violated if a for op's body
/// operations are shifted by the specified shifts. This method checks if a
/// 'def' and all its uses have the same shift factor.
// TODO(mlir-team): extend this to check for memory-based dependence violation
// when we have the support.
bool mlir::isInstwiseShiftValid(AffineForOp forOp, ArrayRef<uint64_t> shifts) {
  auto *forBody = forOp.getBody();
  assert(shifts.size() == forBody->getOperations().size());

  // Work backwards over the body of the block so that the shift of a use's
  // ancestor operation in the block gets recorded before it's looked up.
  DenseMap<Operation *, uint64_t> forBodyShift;
  for (auto it : llvm::enumerate(llvm::reverse(forBody->getOperations()))) {
    auto &op = it.value();

    // Get the index of the current operation, note that we are iterating in
    // reverse so we need to fix it up.
    size_t index = shifts.size() - it.index() - 1;

    // Remember the shift of this operation.
    uint64_t shift = shifts[index];
    forBodyShift.try_emplace(&op, shift);

    // Validate the results of this operation if it were to be shifted.
    for (unsigned i = 0, e = op.getNumResults(); i < e; ++i) {
      Value *result = op.getResult(i);
      for (auto *user : result->getUsers()) {
        // If an ancestor operation doesn't lie in the block of forOp,
        // there is no shift to check.
        if (auto *ancInst = forBody->findAncestorInstInBlock(*user)) {
          assert(forBodyShift.count(ancInst) > 0 && "ancestor expected in map");
          if (shift != forBodyShift[ancInst])
            return false;
        }
      }
    }
  }
  return true;
}
