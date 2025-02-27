//===- Operation.h - MLIR Operation Class -----------------------*- C++ -*-===//
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
// This file defines the Operation class.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_IR_OPERATION_H
#define MLIR_IR_OPERATION_H

#include "mlir/IR/Block.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/Region.h"
#include "llvm/ADT/Twine.h"

namespace mlir {
class BlockAndValueMapping;
class Location;
class MLIRContext;
class OperandIterator;
class OperandTypeIterator;
struct OperationState;
class ResultIterator;
class ResultTypeIterator;

/// Terminator operations can have Block operands to represent successors.
using BlockOperand = IROperandImpl<Block>;

/// Operation is a basic unit of execution within a function. Operations can
/// be nested within other operations effectively forming a tree. Child
/// operations are organized into operation blocks represented by a 'Block'
/// class.
class Operation final
    : public llvm::ilist_node_with_parent<Operation, Block>,
      private llvm::TrailingObjects<Operation, OpResult, BlockOperand, unsigned,
                                    Region, detail::OperandStorage> {
public:
  /// Create a new Operation with the specific fields.
  static Operation *create(Location location, OperationName name,
                           ArrayRef<Value *> operands,
                           ArrayRef<Type> resultTypes,
                           ArrayRef<NamedAttribute> attributes,
                           ArrayRef<Block *> successors, unsigned numRegions,
                           bool resizableOperandList, MLIRContext *context);

  /// Overload of create that takes an existing NamedAttributeList to avoid
  /// unnecessarily uniquing a list of attributes.
  static Operation *create(Location location, OperationName name,
                           ArrayRef<Value *> operands,
                           ArrayRef<Type> resultTypes,
                           const NamedAttributeList &attributes,
                           ArrayRef<Block *> successors, unsigned numRegions,
                           bool resizableOperandList, MLIRContext *context);

  /// Create a new Operation from the fields stored in `state`.
  static Operation *create(const OperationState &state);

  /// The name of an operation is the key identifier for it.
  OperationName getName() { return name; }

  /// If this operation has a registered operation description, return it.
  /// Otherwise return null.
  const AbstractOperation *getAbstractOperation() {
    return getName().getAbstractOperation();
  }

  /// Returns true if this operation has a registered operation description,
  /// otherwise false.
  bool isRegistered() { return getAbstractOperation(); }

  /// Remove this operation from its parent block and delete it.
  void erase();

  /// Create a deep copy of this operation, remapping any operands that use
  /// values outside of the operation using the map that is provided (leaving
  /// them alone if no entry is present).  Replaces references to cloned
  /// sub-operations to the corresponding operation that is copied, and adds
  /// those mappings to the map.
  Operation *clone(BlockAndValueMapping &mapper);
  Operation *clone();

  /// Create a deep copy of this operation but keep the operation regions empty.
  /// Operands are remapped using `mapper` (if present), and `mapper` is updated
  /// to contain the results.
  Operation *cloneWithoutRegions(BlockAndValueMapping &mapper);
  Operation *cloneWithoutRegions();

  /// Returns the operation block that contains this operation.
  Block *getBlock() { return block; }

  /// Return the context this operation is associated with.
  MLIRContext *getContext();

  /// Return the dialact this operation is associated with, or nullptr if the
  /// associated dialect is not registered.
  Dialect *getDialect();

  /// The source location the operation was defined or derived from.
  Location getLoc() { return location; }

  /// Set the source location the operation was defined or derived from.
  void setLoc(Location loc) { location = loc; }

  /// Returns the region to which the instruction belongs, which can be a
  /// function body region or a region that belongs to another operation.
  /// Returns nullptr if the instruction is unlinked.
  Region *getContainingRegion() const;

  /// Returns the closest surrounding operation that contains this operation
  /// or nullptr if this is a top-level operation.
  Operation *getParentOp();

  /// Returns the function that this operation is part of.
  /// The function is determined by traversing the chain of parent operations.
  /// Returns nullptr if the operation is unlinked.
  Function *getFunction();

  /// Replace any uses of 'from' with 'to' within this operation.
  void replaceUsesOfWith(Value *from, Value *to);

  /// Destroys this operation and its subclass data.
  void destroy();

  /// This drops all operand uses from this operation, which is an essential
  /// step in breaking cyclic dependences between references when they are to
  /// be deleted.
  void dropAllReferences();

  /// Drop uses of all values defined by this operation or its nested regions.
  void dropAllDefinedValueUses();

  /// Unlink this operation from its current block and insert it right before
  /// `existingInst` which may be in the same or another block in the same
  /// function.
  void moveBefore(Operation *existingInst);

  /// Unlink this operation from its current block and insert it right before
  /// `iterator` in the specified block.
  void moveBefore(Block *block, llvm::iplist<Operation>::iterator iterator);

  /// Given an operation 'other' that is within the same parent block, return
  /// whether the current operation is before 'other' in the operation list
  /// of the parent block.
  /// Note: This function has an average complexity of O(1), but worst case may
  /// take O(N) where N is the number of operations within the parent block.
  bool isBeforeInBlock(Operation *other);

  /// Perform (potentially expensive) checks of invariants, used to detect
  /// compiler bugs.  On error, this reports the error through the MLIRContext
  /// and returns failure.
  LogicalResult verify();

  void print(raw_ostream &os);
  void dump();

  //===--------------------------------------------------------------------===//
  // Operands
  //===--------------------------------------------------------------------===//

  /// Returns if the operation has a resizable operation list, i.e. operands can
  /// be added.
  bool hasResizableOperandsList() { return getOperandStorage().isResizable(); }

  /// Replace the current operands of this operation with the ones provided in
  /// 'operands'. If the operands list is not resizable, the size of 'operands'
  /// must be less than or equal to the current number of operands.
  void setOperands(ArrayRef<Value *> operands) {
    getOperandStorage().setOperands(this, operands);
  }

  unsigned getNumOperands() { return getOperandStorage().size(); }

  Value *getOperand(unsigned idx) { return getOpOperand(idx).get(); }
  void setOperand(unsigned idx, Value *value) {
    return getOpOperand(idx).set(value);
  }

  // Support operand iteration.
  using operand_iterator = OperandIterator;
  using operand_range = llvm::iterator_range<operand_iterator>;

  operand_iterator operand_begin();
  operand_iterator operand_end();

  /// Returns an iterator on the underlying Value's (Value *).
  operand_range getOperands();

  MutableArrayRef<OpOperand> getOpOperands() {
    return getOperandStorage().getOperands();
  }

  OpOperand &getOpOperand(unsigned idx) { return getOpOperands()[idx]; }

  // Support operand type iteration.
  using operand_type_iterator = OperandTypeIterator;
  using operand_type_range = llvm::iterator_range<operand_type_iterator>;
  operand_type_iterator operand_type_begin();
  operand_type_iterator operand_type_end();
  operand_type_range getOperandTypes();

  //===--------------------------------------------------------------------===//
  // Results
  //===--------------------------------------------------------------------===//

  /// Return true if there are no users of any results of this operation.
  bool use_empty();

  unsigned getNumResults() { return numResults; }

  Value *getResult(unsigned idx) { return &getOpResult(idx); }

  // Support result iteration.
  using result_iterator = ResultIterator;
  using result_range = llvm::iterator_range<result_iterator>;

  result_iterator result_begin();
  result_iterator result_end();

  result_range getResults();

  MutableArrayRef<OpResult> getOpResults() {
    return {getTrailingObjects<OpResult>(), numResults};
  }

  OpResult &getOpResult(unsigned idx) { return getOpResults()[idx]; }

  // Support result type iteration.
  using result_type_iterator = ResultTypeIterator;
  using result_type_range = llvm::iterator_range<result_type_iterator>;
  result_type_iterator result_type_begin();
  result_type_iterator result_type_end();
  result_type_range getResultTypes();

  //===--------------------------------------------------------------------===//
  // Attributes
  //===--------------------------------------------------------------------===//

  // Operations may optionally carry a list of attributes that associate
  // constants to names.  Attributes may be dynamically added and removed over
  // the lifetime of an operation.

  /// Return all of the attributes on this operation.
  ArrayRef<NamedAttribute> getAttrs() { return attrs.getAttrs(); }

  /// Return the internal attribute list on this operation.
  NamedAttributeList &getAttrList() { return attrs; }

  /// Set the attribute list on this operation.
  /// Using a NamedAttributeList is more efficient as it does not require new
  /// uniquing in the MLIRContext.
  void setAttrList(NamedAttributeList newAttrs) { attrs = newAttrs; }

  /// Return the specified attribute if present, null otherwise.
  Attribute getAttr(Identifier name) { return attrs.get(name); }
  Attribute getAttr(StringRef name) { return attrs.get(name); }

  template <typename AttrClass> AttrClass getAttrOfType(Identifier name) {
    return getAttr(name).dyn_cast_or_null<AttrClass>();
  }

  template <typename AttrClass> AttrClass getAttrOfType(StringRef name) {
    return getAttr(name).dyn_cast_or_null<AttrClass>();
  }

  /// If the an attribute exists with the specified name, change it to the new
  /// value.  Otherwise, add a new attribute with the specified name/value.
  void setAttr(Identifier name, Attribute value) { attrs.set(name, value); }
  void setAttr(StringRef name, Attribute value) {
    setAttr(Identifier::get(name, getContext()), value);
  }

  /// Remove the attribute with the specified name if it exists.  The return
  /// value indicates whether the attribute was present or not.
  NamedAttributeList::RemoveResult removeAttr(Identifier name) {
    return attrs.remove(name);
  }

  //===--------------------------------------------------------------------===//
  // Blocks
  //===--------------------------------------------------------------------===//

  /// Returns the number of regions held by this operation.
  unsigned getNumRegions() { return numRegions; }

  /// Returns the regions held by this operation.
  MutableArrayRef<Region> getRegions() {
    auto *regions = getTrailingObjects<Region>();
    return {regions, numRegions};
  }

  /// Returns the region held by this operation at position 'index'.
  Region &getRegion(unsigned index) {
    assert(index < numRegions && "invalid region index");
    return getRegions()[index];
  }

  //===--------------------------------------------------------------------===//
  // Terminators
  //===--------------------------------------------------------------------===//

  MutableArrayRef<BlockOperand> getBlockOperands() {
    return {getTrailingObjects<BlockOperand>(), numSuccs};
  }

  /// Return the operands of this operation that are *not* successor arguments.
  operand_range getNonSuccessorOperands();

  operand_range getSuccessorOperands(unsigned index);

  Value *getSuccessorOperand(unsigned succIndex, unsigned opIndex) {
    assert(!isKnownNonTerminator() && "only terminators may have successors");
    assert(opIndex < getNumSuccessorOperands(succIndex));
    return getOperand(getSuccessorOperandIndex(succIndex) + opIndex);
  }

  bool hasSuccessors() { return numSuccs != 0; }
  unsigned getNumSuccessors() { return numSuccs; }
  unsigned getNumSuccessorOperands(unsigned index) {
    assert(!isKnownNonTerminator() && "only terminators may have successors");
    assert(index < getNumSuccessors());
    return getTrailingObjects<unsigned>()[index];
  }

  Block *getSuccessor(unsigned index) {
    assert(index < getNumSuccessors());
    return getBlockOperands()[index].get();
  }
  void setSuccessor(Block *block, unsigned index);

  /// Erase a specific operand from the operand list of the successor at
  /// 'index'.
  void eraseSuccessorOperand(unsigned succIndex, unsigned opIndex) {
    assert(succIndex < getNumSuccessors());
    assert(opIndex < getNumSuccessorOperands(succIndex));
    getOperandStorage().eraseOperand(getSuccessorOperandIndex(succIndex) +
                                     opIndex);
    --getTrailingObjects<unsigned>()[succIndex];
  }

  /// Get the index of the first operand of the successor at the provided
  /// index.
  unsigned getSuccessorOperandIndex(unsigned index);

  //===--------------------------------------------------------------------===//
  // Accessors for various properties of operations
  //===--------------------------------------------------------------------===//

  /// Returns whether the operation is commutative.
  bool isCommutative() {
    if (auto *absOp = getAbstractOperation())
      return absOp->hasProperty(OperationProperty::Commutative);
    return false;
  }

  /// Returns whether the operation has side-effects.
  bool hasNoSideEffect() {
    if (auto *absOp = getAbstractOperation())
      return absOp->hasProperty(OperationProperty::NoSideEffect);
    return false;
  }

  /// Represents the status of whether an operation is a terminator. We
  /// represent an 'unknown' status because we want to support unregistered
  /// terminators.
  enum class TerminatorStatus { Terminator, NonTerminator, Unknown };

  /// Returns the status of whether this operation is a terminator or not.
  TerminatorStatus getTerminatorStatus() {
    if (auto *absOp = getAbstractOperation()) {
      return absOp->hasProperty(OperationProperty::Terminator)
                 ? TerminatorStatus::Terminator
                 : TerminatorStatus::NonTerminator;
    }
    return TerminatorStatus::Unknown;
  }

  /// Returns if the operation is known to be a terminator.
  bool isKnownTerminator() {
    return getTerminatorStatus() == TerminatorStatus::Terminator;
  }

  /// Returns if the operation is known to *not* be a terminator.
  bool isKnownNonTerminator() {
    return getTerminatorStatus() == TerminatorStatus::NonTerminator;
  }

  /// Returns if the operation is known to be completely isolated from enclosing
  /// regions, i.e. no internal regions reference values defined above this
  /// operation.
  bool isKnownIsolatedFromAbove() {
    if (auto *absOp = getAbstractOperation())
      return absOp->hasProperty(OperationProperty::IsolatedFromAbove);
    return false;
  }

  /// Attempt to fold this operation with the specified constant operand values
  /// - the elements in "operands" will correspond directly to the operands of
  /// the operation, but may be null if non-constant. If folding is successful,
  /// this fills in the `results` vector. If not, `results` is unspecified.
  LogicalResult fold(ArrayRef<Attribute> operands,
                     SmallVectorImpl<OpFoldResult> &results);

  //===--------------------------------------------------------------------===//
  // Operation Walkers
  //===--------------------------------------------------------------------===//

  /// Walk this operation in postorder, calling the callback for each operation
  /// including this one.
  void walk(const std::function<void(Operation *)> &callback);

  /// Specialization of walk to only visit operations of 'T'.
  template <typename T> void walk(std::function<void(T)> callback) {
    walk([&](Operation *op) {
      if (auto derivedOp = dyn_cast<T>(op))
        callback(derivedOp);
    });
  }

  //===--------------------------------------------------------------------===//
  // Other
  //===--------------------------------------------------------------------===//

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

private:
  Operation(Location location, OperationName name, unsigned numResults,
            unsigned numSuccessors, unsigned numRegions,
            const NamedAttributeList &attributes, MLIRContext *context);

  // Operations are deleted through the destroy() member because they are
  // allocated with malloc.
  ~Operation();

  /// Returns the operand storage object.
  detail::OperandStorage &getOperandStorage() {
    return *getTrailingObjects<detail::OperandStorage>();
  }

  /// Provide a 'getParent' method for ilist_node_with_parent methods.
  /// We mark it as const function because ilist_node_with_parent specifically
  /// requires a 'getParent() const' method. Once ilist_node removes this
  /// constraint, we should drop the const to fit the rest of the MLIR const
  /// model.
  Block *getParent() const { return block; }

  /// The operation block that containts this operation.
  Block *block = nullptr;

  /// This holds information about the source location the operation was defined
  /// or derived from.
  Location location;

  /// Relative order of this operation in its parent block. Used for
  /// O(1) local dominance checks between operations.
  mutable unsigned orderIndex = 0;

  const unsigned numResults, numSuccs, numRegions;

  /// This holds the name of the operation.
  OperationName name;

  /// This holds general named attributes for the operation.
  NamedAttributeList attrs;

  // allow ilist_traits access to 'block' field.
  friend struct llvm::ilist_traits<Operation>;

  // allow block to access the 'orderIndex' field.
  friend class Block;

  // allow ilist_node_with_parent to access the 'getParent' method.
  friend class llvm::ilist_node_with_parent<Operation, Block>;

  // This stuff is used by the TrailingObjects template.
  friend llvm::TrailingObjects<Operation, OpResult, BlockOperand, unsigned,
                               Region, detail::OperandStorage>;
  size_t numTrailingObjects(OverloadToken<OpResult>) const {
    return numResults;
  }
  size_t numTrailingObjects(OverloadToken<BlockOperand>) const {
    return numSuccs;
  }
  size_t numTrailingObjects(OverloadToken<Region>) const { return numRegions; }
  size_t numTrailingObjects(OverloadToken<unsigned>) const { return numSuccs; }
};

inline raw_ostream &operator<<(raw_ostream &os, Operation &op) {
  op.print(os);
  return os;
}

/// This class implements the const/non-const operand iterators for the
/// Operation class in terms of getOperand(idx).
class OperandIterator final
    : public indexed_accessor_iterator<OperandIterator, Operation *, Value *,
                                       Value *, Value *> {
public:
  /// Initializes the operand iterator to the specified operand index.
  OperandIterator(Operation *object, unsigned index)
      : indexed_accessor_iterator<OperandIterator, Operation *, Value *,
                                  Value *, Value *>(object, index) {}

  Value *operator*() const { return this->object->getOperand(this->index); }
};

/// This class implements the operand type iterators for the Operation
/// class in terms of operand_iterator->getType().
class OperandTypeIterator final
    : public llvm::mapped_iterator<OperandIterator, Type (*)(Value *)> {
  static Type unwrap(Value *value) { return value->getType(); }

public:
  using reference = Type;

  /// Initializes the operand type iterator to the specified operand iterator.
  OperandTypeIterator(OperandIterator it)
      : llvm::mapped_iterator<OperandIterator, Type (*)(Value *)>(it, &unwrap) {
  }
};

// Implement the inline operand iterator methods.
inline auto Operation::operand_begin() -> operand_iterator {
  return operand_iterator(this, 0);
}

inline auto Operation::operand_end() -> operand_iterator {
  return operand_iterator(this, getNumOperands());
}

inline auto Operation::getOperands() -> operand_range {
  return {operand_begin(), operand_end()};
}

inline auto Operation::operand_type_begin() -> operand_type_iterator {
  return operand_type_iterator(operand_begin());
}

inline auto Operation::operand_type_end() -> operand_type_iterator {
  return operand_type_iterator(operand_end());
}

inline auto Operation::getOperandTypes() -> operand_type_range {
  return {operand_type_begin(), operand_type_end()};
}

/// This class implements the result iterators for the Operation class
/// in terms of getResult(idx).
class ResultIterator final
    : public indexed_accessor_iterator<ResultIterator, Operation *, Value *,
                                       Value *, Value *> {
public:
  /// Initializes the result iterator to the specified index.
  ResultIterator(Operation *object, unsigned index)
      : indexed_accessor_iterator<ResultIterator, Operation *, Value *, Value *,
                                  Value *>(object, index) {}

  Value *operator*() const { return this->object->getResult(this->index); }
};

/// This class implements the result type iterators for the Operation
/// class in terms of result_iterator->getType().
class ResultTypeIterator final
    : public llvm::mapped_iterator<ResultIterator, Type (*)(Value *)> {
  static Type unwrap(Value *value) { return value->getType(); }

public:
  using reference = Type;

  /// Initializes the result type iterator to the specified result iterator.
  ResultTypeIterator(ResultIterator it)
      : llvm::mapped_iterator<ResultIterator, Type (*)(Value *)>(it, &unwrap) {}
};

// Implement the inline result iterator methods.
inline auto Operation::result_begin() -> result_iterator {
  return result_iterator(this, 0);
}

inline auto Operation::result_end() -> result_iterator {
  return result_iterator(this, getNumResults());
}

inline auto Operation::getResults() -> llvm::iterator_range<result_iterator> {
  return {result_begin(), result_end()};
}

inline auto Operation::result_type_begin() -> result_type_iterator {
  return result_type_iterator(result_begin());
}

inline auto Operation::result_type_end() -> result_type_iterator {
  return result_type_iterator(result_end());
}

inline auto Operation::getResultTypes() -> result_type_range {
  return {result_type_begin(), result_type_end()};
}

} // end namespace mlir

namespace llvm {
/// Provide isa functionality for operation casts.
template <typename T> struct isa_impl<T, ::mlir::Operation> {
  static inline bool doit(const ::mlir::Operation &op) {
    return T::classof(const_cast<::mlir::Operation *>(&op));
  }
};

/// Provide specializations for operation casts as the resulting T is value
/// typed.
template <typename T> struct cast_retty_impl<T, ::mlir::Operation *> {
  using ret_type = T;
};
template <typename T> struct cast_retty_impl<T, ::mlir::Operation> {
  using ret_type = T;
};
template <class T>
struct cast_convert_val<T, ::mlir::Operation, ::mlir::Operation> {
  static T doit(::mlir::Operation &val) { return T(&val); }
};
template <class T>
struct cast_convert_val<T, ::mlir::Operation *, ::mlir::Operation *> {
  static T doit(::mlir::Operation *val) { return T(val); }
};
} // end namespace llvm

#endif // MLIR_IR_OPERATION_H
