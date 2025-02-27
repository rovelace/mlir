//===- OpDefinition.h - Classes for defining concrete Op types --*- C++ -*-===//
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
// This file implements helper classes for implementing the "Op" types.  This
// includes the Op type, which is the base class for Op class definitions,
// as well as number of traits in the OpTrait namespace that provide a
// declarative way to specify properties of Ops.
//
// The purpose of these types are to allow light-weight implementation of
// concrete ops (like DimOp) with very little boilerplate.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_IR_OPDEFINITION_H
#define MLIR_IR_OPDEFINITION_H

#include "mlir/IR/Operation.h"
#include <type_traits>

namespace mlir {
class Builder;

namespace OpTrait {
template <typename ConcreteType> class OneResult;
}

/// This class represents success/failure for operation parsing. It is
/// essentially a simple wrapper class around LogicalResult that allows for
/// explicit conversion to bool. This allows for the parser to chain together
/// parse rules without the clutter of "failed/succeeded".
class ParseResult : public LogicalResult {
public:
  ParseResult(LogicalResult result = success()) : LogicalResult(result) {}

  // Allow diagnostics emitted during parsing to be converted to failure.
  ParseResult(const InFlightDiagnostic &) : LogicalResult(failure()) {}
  ParseResult(const Diagnostic &) : LogicalResult(failure()) {}

  /// Failure is true in a boolean context.
  explicit operator bool() const { return failed(*this); }
};

/// This is the concrete base class that holds the operation pointer and has
/// non-generic methods that only depend on State (to avoid having them
/// instantiated on template types that don't affect them.
///
/// This also has the fallback implementations of customization hooks for when
/// they aren't customized.
class OpState {
public:
  /// Ops are pointer-like, so we allow implicit conversion to bool.
  operator bool() { return getOperation() != nullptr; }

  /// This implicitly converts to Operation*.
  operator Operation *() const { return state; }

  /// Return the operation that this refers to.
  Operation *getOperation() { return state; }

  ///  Return the context this operation belongs to.
  MLIRContext *getContext() { return getOperation()->getContext(); }

  /// The source location the operation was defined or derived from.
  Location getLoc() { return state->getLoc(); }

  /// Return all of the attributes on this operation.
  ArrayRef<NamedAttribute> getAttrs() { return state->getAttrs(); }

  /// Return an attribute with the specified name.
  Attribute getAttr(StringRef name) { return state->getAttr(name); }

  /// If the operation has an attribute of the specified type, return it.
  template <typename AttrClass> AttrClass getAttrOfType(StringRef name) {
    return getAttr(name).dyn_cast_or_null<AttrClass>();
  }

  /// If the an attribute exists with the specified name, change it to the new
  /// value.  Otherwise, add a new attribute with the specified name/value.
  void setAttr(Identifier name, Attribute value) {
    state->setAttr(name, value);
  }
  void setAttr(StringRef name, Attribute value) {
    setAttr(Identifier::get(name, getContext()), value);
  }

  /// Remove the attribute with the specified name if it exists.  The return
  /// value indicates whether the attribute was present or not.
  NamedAttributeList::RemoveResult removeAttr(Identifier name) {
    return state->removeAttr(name);
  }
  NamedAttributeList::RemoveResult removeAttr(StringRef name) {
    return state->removeAttr(Identifier::get(name, getContext()));
  }

  /// Return true if there are no users of any results of this operation.
  bool use_empty() { return state->use_empty(); }

  /// Remove this operation from its parent block and delete it.
  void erase() { state->erase(); }

  /// Emit an error with the op name prefixed, like "'dim' op " which is
  /// convenient for verifiers.
  InFlightDiagnostic emitOpError(const Twine &message = {});

  /// Emit an error about fatal conditions with this operation, reporting up to
  /// any diagnostic handlers that may be listening.
  InFlightDiagnostic emitError(const Twine &message = {});

  /// Emit a warning about this operation, reporting up to any diagnostic
  /// handlers that may be listening.
  InFlightDiagnostic emitWarning(const Twine &message = {});

  /// Emit a remark about this operation, reporting up to any diagnostic
  /// handlers that may be listening.
  InFlightDiagnostic emitRemark(const Twine &message = {});

  // These are default implementations of customization hooks.
public:
  /// This hook returns any canonicalization pattern rewrites that the operation
  /// supports, for use by the canonicalization pass.
  static void getCanonicalizationPatterns(OwningRewritePatternList &results,
                                          MLIRContext *context) {}

protected:
  /// If the concrete type didn't implement a custom verifier hook, just fall
  /// back to this one which accepts everything.
  LogicalResult verify() { return success(); }

  /// Unless overridden, the custom assembly form of an op is always rejected.
  /// Op implementations should implement this to return failure.
  /// On success, they should fill in result with the fields to use.
  static ParseResult parse(OpAsmParser *parser, OperationState *result);

  // The fallback for the printer is to print it the generic assembly form.
  void print(OpAsmPrinter *p);

  /// Mutability management is handled by the OpWrapper/OpConstWrapper classes,
  /// so we can cast it away here.
  explicit OpState(Operation *state) : state(state) {}

private:
  Operation *state;
};

// Allow comparing operators.
inline bool operator==(OpState lhs, OpState rhs) {
  return lhs.getOperation() == rhs.getOperation();
}
inline bool operator!=(OpState lhs, OpState rhs) {
  return lhs.getOperation() != rhs.getOperation();
}

/// This class represents a single result from folding an operation.
class OpFoldResult : public llvm::PointerUnion<Attribute, Value *> {
  using llvm::PointerUnion<Attribute, Value *>::PointerUnion;
};

/// This template defines the foldHook as used by AbstractOperation.
///
/// The default implementation uses a general fold method that can be defined on
/// custom ops which can return multiple results.
template <typename ConcreteType, bool isSingleResult, typename = void>
class FoldingHook {
public:
  /// This is an implementation detail of the constant folder hook for
  /// AbstractOperation.
  static LogicalResult foldHook(Operation *op, ArrayRef<Attribute> operands,
                                SmallVectorImpl<OpFoldResult> &results) {
    return cast<ConcreteType>(op).fold(operands, results);
  }

  /// This hook implements a generalized folder for this operation.  Operations
  /// can implement this to provide simplifications rules that are applied by
  /// the Builder::createOrFold API and the canonicalization pass.
  ///
  /// This is an intentionally limited interface - implementations of this hook
  /// can only perform the following changes to the operation:
  ///
  ///  1. They can leave the operation alone and without changing the IR, and
  ///     return failure.
  ///  2. They can mutate the operation in place, without changing anything else
  ///     in the IR.  In this case, return success.
  ///  3. They can return a list of existing values that can be used instead of
  ///     the operation.  In this case, fill in the results list and return
  ///     success.  The caller will remove the operation and use those results
  ///     instead.
  ///
  /// This allows expression of some simple in-place canonicalizations (e.g.
  /// "x+0 -> x", "min(x,y,x,z) -> min(x,y,z)", "x+y-x -> y", etc), as well as
  /// generalized constant folding.
  ///
  /// If not overridden, this fallback implementation always fails to fold.
  ///
  LogicalResult fold(ArrayRef<Attribute> operands,
                     SmallVectorImpl<OpFoldResult> &results) {
    return failure();
  }
};

/// This template specialization defines the foldHook as used by
/// AbstractOperation for single-result operations.  This gives the hook a nicer
/// signature that is easier to implement.
template <typename ConcreteType, bool isSingleResult>
class FoldingHook<ConcreteType, isSingleResult,
                  typename std::enable_if<isSingleResult>::type> {
public:
  /// If the operation returns a single value, then the Op can be implicitly
  /// converted to an Value*.  This yields the value of the only result.
  operator Value *() {
    return static_cast<ConcreteType *>(this)->getOperation()->getResult(0);
  }

  /// This is an implementation detail of the constant folder hook for
  /// AbstractOperation.
  static LogicalResult foldHook(Operation *op, ArrayRef<Attribute> operands,
                                SmallVectorImpl<OpFoldResult> &results) {
    auto result = cast<ConcreteType>(op).fold(operands);
    if (!result)
      return failure();

    // Check if the operation was folded in place. In this case, the operation
    // returns itself.
    if (result.template dyn_cast<Value *>() != op->getResult(0))
      results.push_back(result);
    return success();
  }

  /// This hook implements a generalized folder for this operation.  Operations
  /// can implement this to provide simplifications rules that are applied by
  /// the Builder::createOrFold API and the canonicalization pass.
  ///
  /// This is an intentionally limited interface - implementations of this hook
  /// can only perform the following changes to the operation:
  ///
  ///  1. They can leave the operation alone and without changing the IR, and
  ///     return nullptr.
  ///  2. They can mutate the operation in place, without changing anything else
  ///     in the IR.  In this case, return the operation itself.
  ///  3. They can return an existing SSA value that can be used instead of
  ///     the operation.  In this case, return that value.  The caller will
  ///     remove the operation and use that result instead.
  ///
  /// This allows expression of some simple in-place canonicalizations (e.g.
  /// "x+0 -> x", "min(x,y,x,z) -> min(x,y,z)", "x+y-x -> y", etc), as well as
  /// generalized constant folding.
  ///
  /// If not overridden, this fallback implementation always fails to fold.
  ///
  OpFoldResult fold(ArrayRef<Attribute> operands) { return {}; }
};

//===----------------------------------------------------------------------===//
// Operation Trait Types
//===----------------------------------------------------------------------===//

namespace OpTrait {

// These functions are out-of-line implementations of the methods in the
// corresponding trait classes.  This avoids them being template
// instantiated/duplicated.
namespace impl {
LogicalResult verifyZeroOperands(Operation *op);
LogicalResult verifyOneOperand(Operation *op);
LogicalResult verifyNOperands(Operation *op, unsigned numOperands);
LogicalResult verifyAtLeastNOperands(Operation *op, unsigned numOperands);
LogicalResult verifyOperandsAreFloatLike(Operation *op);
LogicalResult verifyOperandsAreIntegerLike(Operation *op);
LogicalResult verifySameTypeOperands(Operation *op);
LogicalResult verifyZeroResult(Operation *op);
LogicalResult verifyOneResult(Operation *op);
LogicalResult verifyNResults(Operation *op, unsigned numOperands);
LogicalResult verifyAtLeastNResults(Operation *op, unsigned numOperands);
LogicalResult verifySameOperandsAndResultShape(Operation *op);
LogicalResult verifySameOperandsAndResultElementType(Operation *op);
LogicalResult verifySameOperandsAndResultType(Operation *op);
LogicalResult verifyResultsAreBoolLike(Operation *op);
LogicalResult verifyResultsAreFloatLike(Operation *op);
LogicalResult verifyResultsAreIntegerLike(Operation *op);
LogicalResult verifyIsTerminator(Operation *op);
} // namespace impl

/// Helper class for implementing traits.  Clients are not expected to interact
/// with this directly, so its members are all protected.
template <typename ConcreteType, template <typename> class TraitType>
class TraitBase {
protected:
  /// Return the ultimate Operation being worked on.
  Operation *getOperation() {
    // We have to cast up to the trait type, then to the concrete type, then to
    // the BaseState class in explicit hops because the concrete type will
    // multiply derive from the (content free) TraitBase class, and we need to
    // be able to disambiguate the path for the C++ compiler.
    auto *trait = static_cast<TraitType<ConcreteType> *>(this);
    auto *concrete = static_cast<ConcreteType *>(trait);
    auto *base = static_cast<OpState *>(concrete);
    return base->getOperation();
  }

  /// Provide default implementations of trait hooks.  This allows traits to
  /// provide exactly the overrides they care about.
  static LogicalResult verifyTrait(Operation *op) { return success(); }
  static AbstractOperation::OperationProperties getTraitProperties() {
    return 0;
  }
};

namespace detail {
/// Utility trait base that provides accessors for derived traits that have
/// multiple operands.
template <typename ConcreteType, template <typename> class TraitType>
struct MultiOperandTraitBase : public TraitBase<ConcreteType, TraitType> {
  using operand_iterator = Operation::operand_iterator;
  using operand_range = Operation::operand_range;
  using operand_type_iterator = Operation::operand_type_iterator;
  using operand_type_range = Operation::operand_type_range;

  /// Return the number of operands.
  unsigned getNumOperands() { return this->getOperation()->getNumOperands(); }

  /// Return the operand at index 'i'.
  Value *getOperand(unsigned i) { return this->getOperation()->getOperand(i); }

  /// Set the operand at index 'i' to 'value'.
  void setOperand(unsigned i, Value *value) {
    this->getOperation()->setOperand(i, value);
  }

  /// Operand iterator access.
  operand_iterator operand_begin() {
    return this->getOperation()->operand_begin();
  }
  operand_iterator operand_end() { return this->getOperation()->operand_end(); }
  operand_range getOperands() { return this->getOperation()->getOperands(); }

  /// Operand type access.
  operand_type_iterator operand_type_begin() {
    return this->getOperation()->operand_type_begin();
  }
  operand_type_iterator operand_type_end() {
    return this->getOperation()->operand_type_end();
  }
  operand_type_range getOperandTypes() {
    return this->getOperation()->getOperandTypes();
  }
};
} // end namespace detail

/// This class provides the API for ops that are known to have no
/// SSA operand.
template <typename ConcreteType>
class ZeroOperands : public TraitBase<ConcreteType, ZeroOperands> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyZeroOperands(op);
  }

private:
  // Disable these.
  void getOperand() {}
  void setOperand() {}
};

/// This class provides the API for ops that are known to have exactly one
/// SSA operand.
template <typename ConcreteType>
class OneOperand : public TraitBase<ConcreteType, OneOperand> {
public:
  Value *getOperand() { return this->getOperation()->getOperand(0); }

  void setOperand(Value *value) { this->getOperation()->setOperand(0, value); }

  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyOneOperand(op);
  }
};

/// This class provides the API for ops that are known to have a specified
/// number of operands.  This is used as a trait like this:
///
///   class FooOp : public Op<FooOp, OpTrait::NOperands<2>::Impl> {
///
template <unsigned N> class NOperands {
public:
  static_assert(N > 1, "use ZeroOperands/OneOperand for N < 2");

  template <typename ConcreteType>
  class Impl
      : public detail::MultiOperandTraitBase<ConcreteType, NOperands<N>::Impl> {
  public:
    static LogicalResult verifyTrait(Operation *op) {
      return impl::verifyNOperands(op, N);
    }
  };
};

/// This class provides the API for ops that are known to have a at least a
/// specified number of operands.  This is used as a trait like this:
///
///   class FooOp : public Op<FooOp, OpTrait::AtLeastNOperands<2>::Impl> {
///
template <unsigned N> class AtLeastNOperands {
public:
  template <typename ConcreteType>
  class Impl : public detail::MultiOperandTraitBase<ConcreteType,
                                                    AtLeastNOperands<N>::Impl> {
  public:
    static LogicalResult verifyTrait(Operation *op) {
      return impl::verifyAtLeastNOperands(op, N);
    }
  };
};

/// This class provides the API for ops which have an unknown number of
/// SSA operands.
template <typename ConcreteType>
class VariadicOperands
    : public detail::MultiOperandTraitBase<ConcreteType, VariadicOperands> {};

/// This class provides return value APIs for ops that are known to have
/// zero results.
template <typename ConcreteType>
class ZeroResult : public TraitBase<ConcreteType, ZeroResult> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyZeroResult(op);
  }
};

namespace detail {
/// Utility trait base that provides accessors for derived traits that have
/// multiple results.
template <typename ConcreteType, template <typename> class TraitType>
struct MultiResultTraitBase : public TraitBase<ConcreteType, TraitType> {
  using result_iterator = Operation::result_iterator;
  using result_range = Operation::result_range;
  using result_type_iterator = Operation::result_type_iterator;
  using result_type_range = Operation::result_type_range;

  /// Return the number of results.
  unsigned getNumResults() { return this->getOperation()->getNumResults(); }

  /// Return the result at index 'i'.
  Value *getResult(unsigned i) { return this->getOperation()->getResult(i); }

  /// Set the result at index 'i' to 'value'.
  void setResult(unsigned i, Value *value) {
    this->getOperation()->setResult(i, value);
  }

  /// Return the type of the `i`-th result.
  Type getType(unsigned i) { return getResult(i)->getType(); }

  /// Result iterator access.
  result_iterator result_begin() {
    return this->getOperation()->result_begin();
  }
  result_iterator result_end() { return this->getOperation()->result_end(); }
  result_range getResults() { return this->getOperation()->getResults(); }

  /// Result type access.
  result_type_iterator result_type_begin() {
    return this->getOperation()->result_type_begin();
  }
  result_type_iterator result_type_end() {
    return this->getOperation()->result_type_end();
  }
  result_type_range getResultTypes() {
    return this->getOperation()->getResultTypes();
  }
};
} // end namespace detail

/// This class provides return value APIs for ops that are known to have a
/// single result.
template <typename ConcreteType>
class OneResult : public TraitBase<ConcreteType, OneResult> {
public:
  Value *getResult() { return this->getOperation()->getResult(0); }
  Type getType() { return getResult()->getType(); }

  /// Replace all uses of 'this' value with the new value, updating anything in
  /// the IR that uses 'this' to use the other value instead.  When this returns
  /// there are zero uses of 'this'.
  void replaceAllUsesWith(Value *newValue) {
    getResult()->replaceAllUsesWith(newValue);
  }

  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyOneResult(op);
  }
};

/// This class provides the API for ops that are known to have a specified
/// number of results.  This is used as a trait like this:
///
///   class FooOp : public Op<FooOp, OpTrait::NResults<2>::Impl> {
///
template <unsigned N> class NResults {
public:
  static_assert(N > 1, "use ZeroResult/OneResult for N < 2");

  template <typename ConcreteType>
  class Impl
      : public detail::MultiResultTraitBase<ConcreteType, NResults<N>::Impl> {
  public:
    static LogicalResult verifyTrait(Operation *op) {
      return impl::verifyNResults(op, N);
    }
  };
};

/// This class provides the API for ops that are known to have at least a
/// specified number of results.  This is used as a trait like this:
///
///   class FooOp : public Op<FooOp, OpTrait::AtLeastNResults<2>::Impl> {
///
template <unsigned N> class AtLeastNResults {
public:
  template <typename ConcreteType>
  class Impl : public detail::MultiResultTraitBase<ConcreteType,
                                                   AtLeastNResults<N>::Impl> {
  public:
    static LogicalResult verifyTrait(Operation *op) {
      return impl::verifyAtLeastNResults(op, N);
    }
  };
};

/// This class provides the API for ops which have an unknown number of
/// results.
template <typename ConcreteType>
class VariadicResults
    : public detail::MultiResultTraitBase<ConcreteType, VariadicResults> {};

/// This class provides verification for ops that are known to have the same
/// operand and result shape: both are scalars, vectors/tensors of the same
/// shape.
template <typename ConcreteType>
class SameOperandsAndResultShape
    : public TraitBase<ConcreteType, SameOperandsAndResultShape> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifySameOperandsAndResultShape(op);
  }
};

/// This class provides verification for ops that are known to have the same
/// operand and result element type.
///
template <typename ConcreteType>
class SameOperandsAndResultElementType
    : public TraitBase<ConcreteType, SameOperandsAndResultElementType> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifySameOperandsAndResultElementType(op);
  }
};

/// This class provides verification for ops that are known to have the same
/// operand and result type.
///
/// Note: this trait subsumes the SameOperandsAndResultShape and
/// SameOperandsAndResultElementType traits.
template <typename ConcreteType>
class SameOperandsAndResultType
    : public TraitBase<ConcreteType, SameOperandsAndResultType> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifySameOperandsAndResultType(op);
  }
};

/// This class verifies that any results of the specified op have a boolean
/// type, a vector thereof, or a tensor thereof.
template <typename ConcreteType>
class ResultsAreBoolLike : public TraitBase<ConcreteType, ResultsAreBoolLike> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyResultsAreBoolLike(op);
  }
};

/// This class verifies that any results of the specified op have a floating
/// point type, a vector thereof, or a tensor thereof.
template <typename ConcreteType>
class ResultsAreFloatLike
    : public TraitBase<ConcreteType, ResultsAreFloatLike> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyResultsAreFloatLike(op);
  }
};

/// This class verifies that any results of the specified op have an integer or
/// index type, a vector thereof, or a tensor thereof.
template <typename ConcreteType>
class ResultsAreIntegerLike
    : public TraitBase<ConcreteType, ResultsAreIntegerLike> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyResultsAreIntegerLike(op);
  }
};

/// This class adds property that the operation is commutative.
template <typename ConcreteType>
class IsCommutative : public TraitBase<ConcreteType, IsCommutative> {
public:
  static AbstractOperation::OperationProperties getTraitProperties() {
    return static_cast<AbstractOperation::OperationProperties>(
        OperationProperty::Commutative);
  }
};

/// This class adds property that the operation has no side effects.
template <typename ConcreteType>
class HasNoSideEffect : public TraitBase<ConcreteType, HasNoSideEffect> {
public:
  static AbstractOperation::OperationProperties getTraitProperties() {
    return static_cast<AbstractOperation::OperationProperties>(
        OperationProperty::NoSideEffect);
  }
};

/// This class verifies that all operands of the specified op have a float type,
/// a vector thereof, or a tensor thereof.
template <typename ConcreteType>
class OperandsAreFloatLike
    : public TraitBase<ConcreteType, OperandsAreFloatLike> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyOperandsAreFloatLike(op);
  }
};

/// This class verifies that all operands of the specified op have an integer or
/// index type, a vector thereof, or a tensor thereof.
template <typename ConcreteType>
class OperandsAreIntegerLike
    : public TraitBase<ConcreteType, OperandsAreIntegerLike> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyOperandsAreIntegerLike(op);
  }
};

/// This class verifies that all operands of the specified op have the same
/// type.
template <typename ConcreteType>
class SameTypeOperands : public TraitBase<ConcreteType, SameTypeOperands> {
public:
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifySameTypeOperands(op);
  }
};

/// This class provides the API for ops that are known to be terminators.
template <typename ConcreteType>
class IsTerminator : public TraitBase<ConcreteType, IsTerminator> {
public:
  static AbstractOperation::OperationProperties getTraitProperties() {
    return static_cast<AbstractOperation::OperationProperties>(
        OperationProperty::Terminator);
  }
  static LogicalResult verifyTrait(Operation *op) {
    return impl::verifyIsTerminator(op);
  }

  unsigned getNumSuccessors() {
    return this->getOperation()->getNumSuccessors();
  }
  unsigned getNumSuccessorOperands(unsigned index) {
    return this->getOperation()->getNumSuccessorOperands(index);
  }

  Block *getSuccessor(unsigned index) {
    return this->getOperation()->getSuccessor(index);
  }

  void setSuccessor(Block *block, unsigned index) {
    return this->getOperation()->setSuccessor(block, index);
  }

  void addSuccessorOperand(unsigned index, Value *value) {
    return this->getOperation()->addSuccessorOperand(index, value);
  }
  void addSuccessorOperands(unsigned index, ArrayRef<Value *> values) {
    return this->getOperation()->addSuccessorOperand(index, values);
  }
};

/// This class provides the API for ops that are known to be isolated from
/// above.
template <typename ConcreteType>
class IsIsolatedFromAbove
    : public TraitBase<ConcreteType, IsIsolatedFromAbove> {
public:
  static AbstractOperation::OperationProperties getTraitProperties() {
    return static_cast<AbstractOperation::OperationProperties>(
        OperationProperty::IsolatedFromAbove);
  }
  static LogicalResult verifyTrait(Operation *op) {
    for (auto &region : op->getRegions())
      if (!region.isIsolatedFromAbove(op->getLoc()))
        return failure();
    return success();
  }
};

} // end namespace OpTrait

//===----------------------------------------------------------------------===//
// Operation Definition classes
//===----------------------------------------------------------------------===//

/// This provides public APIs that all operations should have.  The template
/// argument 'ConcreteType' should be the concrete type by CRTP and the others
/// are base classes by the policy pattern.
template <typename ConcreteType, template <typename T> class... Traits>
class Op : public OpState,
           public Traits<ConcreteType>...,
           public FoldingHook<ConcreteType,
                              llvm::is_one_of<OpTrait::OneResult<ConcreteType>,
                                              Traits<ConcreteType>...>::value> {
public:
  /// Return if this operation contains the provided trait.
  template <template <typename T> class Trait>
  static constexpr bool hasTrait() {
    return llvm::is_one_of<Trait<ConcreteType>, Traits<ConcreteType>...>::value;
  }

  /// Return the operation that this refers to.
  Operation *getOperation() { return OpState::getOperation(); }

  /// Return the dialect that this refers to.
  Dialect *getDialect() { return getOperation()->getDialect(); }

  /// Return the Region enclosing this Op.
  Region *getContainingRegion() { return getOperation()->getParentRegion(); }

  /// Return true if this "op class" can match against the specified operation.
  /// This hook can be overridden with a more specific implementation in
  /// the subclass of Base.
  ///
  static bool classof(Operation *op) {
    return op->getName().getStringRef() == ConcreteType::getOperationName();
  }

  /// This is the hook used by the AsmParser to parse the custom form of this
  /// op from an .mlir file.  Op implementations should provide a parse method,
  /// which returns failure.  On success, they should return fill in result with
  /// the fields to use.
  static ParseResult parseAssembly(OpAsmParser *parser,
                                   OperationState *result) {
    return ConcreteType::parse(parser, result);
  }

  /// This is the hook used by the AsmPrinter to emit this to the .mlir file.
  /// Op implementations should provide a print method.
  static void printAssembly(Operation *op, OpAsmPrinter *p) {
    auto opPointer = dyn_cast<ConcreteType>(op);
    assert(opPointer &&
           "op's name does not match name of concrete type instantiated with");
    opPointer.print(p);
  }

  /// This is the hook that checks whether or not this operation is well
  /// formed according to the invariants of its opcode.  It delegates to the
  /// Traits for their policy implementations, and allows the user to specify
  /// their own verify() method.
  ///
  /// On success this returns false; on failure it emits an error to the
  /// diagnostic subsystem and returns true.
  static LogicalResult verifyInvariants(Operation *op) {
    return failure(
        failed(BaseVerifier<Traits<ConcreteType>...>::verifyTrait(op)) ||
        failed(cast<ConcreteType>(op).verify()));
  }

  // Returns the properties of an operation by combining the properties of the
  // traits of the op.
  static AbstractOperation::OperationProperties getOperationProperties() {
    return BaseProperties<Traits<ConcreteType>...>::getTraitProperties();
  }

  // TODO: Provide a dump() method.

  /// Expose the type we are instantiated on to template machinery that may want
  /// to introspect traits on this operation.
  using ConcreteOpType = ConcreteType;

  /// This is a public constructor.  Any op can be initialized to null.
  explicit Op() : OpState(nullptr) {}
  Op(std::nullptr_t) : OpState(nullptr) {}

  /// This is a public constructor to enable access via the llvm::cast family of
  /// methods. This should not be used directly.
  explicit Op(Operation *state) : OpState(state) {
    assert(!state || isa<ConcreteOpType>(state));
  }

private:
  template <typename... Types> struct BaseVerifier;

  template <typename First, typename... Rest>
  struct BaseVerifier<First, Rest...> {
    static LogicalResult verifyTrait(Operation *op) {
      return failure(failed(First::verifyTrait(op)) ||
                     failed(BaseVerifier<Rest...>::verifyTrait(op)));
    }
  };

  template <typename...> struct BaseVerifier {
    static LogicalResult verifyTrait(Operation *op) { return success(); }
  };

  template <typename... Types> struct BaseProperties;

  template <typename First, typename... Rest>
  struct BaseProperties<First, Rest...> {
    static AbstractOperation::OperationProperties getTraitProperties() {
      return First::getTraitProperties() |
             BaseProperties<Rest...>::getTraitProperties();
    }
  };

  template <typename...> struct BaseProperties {
    static AbstractOperation::OperationProperties getTraitProperties() {
      return 0;
    }
  };
};

// These functions are out-of-line implementations of the methods in BinaryOp,
// which avoids them being template instantiated/duplicated.
namespace impl {
void buildBinaryOp(Builder *builder, OperationState *result, Value *lhs,
                   Value *rhs);
ParseResult parseBinaryOp(OpAsmParser *parser, OperationState *result);
// Prints the given binary `op` in custom assembly form if both the two operands
// and the result have the same time. Otherwise, prints the generic assembly
// form.
void printBinaryOp(Operation *op, OpAsmPrinter *p);
} // namespace impl

// These functions are out-of-line implementations of the methods in CastOp,
// which avoids them being template instantiated/duplicated.
namespace impl {
void buildCastOp(Builder *builder, OperationState *result, Value *source,
                 Type destType);
ParseResult parseCastOp(OpAsmParser *parser, OperationState *result);
void printCastOp(Operation *op, OpAsmPrinter *p);
Value *foldCastOp(Operation *op);
} // namespace impl

// These functions are out-of-line utilities, which avoids them being template
// instantiated/duplicated.
namespace impl {
/// Insert an operation, generated by `buildTerminatorOp`, at the end of the
/// region's only block if it does not have a terminator already. If the region
/// is empty, insert a new block first. `buildTerminatorOp` should return the
/// terminator operation to insert.
void ensureRegionTerminator(
    Region &region, Location loc,
    llvm::function_ref<Operation *()> buildTerminatorOp);
/// Templated version that fills the generates the provided operation type.
template <typename OpTy>
void ensureRegionTerminator(Region &region, Builder &builder, Location loc) {
  ensureRegionTerminator(region, loc, [&] {
    OperationState state(loc, OpTy::getOperationName());
    OpTy::build(&builder, &state);
    return Operation::create(state);
  });
}
} // namespace impl
} // end namespace mlir

#endif
