//===- Module.h - MLIR Module Class -----------------------------*- C++ -*-===//
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
// Module is the top-level container for code in an MLIR program.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_IR_MODULE_H
#define MLIR_IR_MODULE_H

#include "mlir/IR/Function.h"
#include "mlir/IR/SymbolTable.h"
#include "llvm/ADT/ilist.h"

namespace mlir {

class Module {
public:
  explicit Module(MLIRContext *context) : symbolTable(context) {}

  MLIRContext *getContext() { return symbolTable.getContext(); }

  /// This is the list of functions in the module.
  using FunctionListType = llvm::iplist<Function>;
  FunctionListType &getFunctions() { return functions; }

  // Iteration over the functions in the module.
  using iterator = FunctionListType::iterator;
  using reverse_iterator = FunctionListType::reverse_iterator;

  iterator begin() { return functions.begin(); }
  iterator end() { return functions.end(); }
  reverse_iterator rbegin() { return functions.rbegin(); }
  reverse_iterator rend() { return functions.rend(); }

  // Interfaces for working with the symbol table.

  /// Look up a function with the specified name, returning null if no such
  /// name exists.  Function names never include the @ on them.
  Function *getNamedFunction(StringRef name) {
    return symbolTable.lookup(name);
  }

  /// Look up a function with the specified name, returning null if no such
  /// name exists.  Function names never include the @ on them.
  Function *getNamedFunction(Identifier name) {
    return symbolTable.lookup(name);
  }

  /// Perform (potentially expensive) checks of invariants, used to detect
  /// compiler bugs.  On error, this reports the error through the MLIRContext
  /// and returns failure.
  LogicalResult verify();

  void print(raw_ostream &os);
  void dump();

private:
  friend struct llvm::ilist_traits<Function>;
  friend class Function;

  /// getSublistAccess() - Returns pointer to member of function list
  static FunctionListType Module::*getSublistAccess(Function *) {
    return &Module::functions;
  }

  /// The symbol table used for functions.
  SymbolTable symbolTable;

  /// This is the actual list of functions the module contains.
  FunctionListType functions;
};

//===--------------------------------------------------------------------===//
// Module Operation.
//===--------------------------------------------------------------------===//

/// ModuleOp represents a module, or an operation containing one region with a
/// single block containing opaque operations. A ModuleOp contains a symbol
/// table for operations, like FuncOp, held within its region. The region of a
/// module is not allowed to implicitly capture global values, and all external
/// references must use attributes.
class ModuleOp : public Op<ModuleOp, OpTrait::ZeroOperands, OpTrait::ZeroResult,
                           OpTrait::IsIsolatedFromAbove> {
public:
  using Op::Op;
  static StringRef getOperationName() { return "module"; }

  static void build(Builder *builder, OperationState *result);

  /// Operation hooks.
  static ParseResult parse(OpAsmParser *parser, OperationState *result);
  void print(OpAsmPrinter *p);
  LogicalResult verify();

  /// Return body of this module.
  Block *getBody();
};

/// The ModuleTerminatorOp is a special terminator operation for the body of a
/// ModuleOp, it has no semantic meaning beyond keeping the body of a ModuleOp
/// well-formed.
///
/// This operation does _not_ have a custom syntax. However, ModuleOp will omit
/// the terminator in their custom syntax for brevity.
class ModuleTerminatorOp
    : public Op<ModuleTerminatorOp, OpTrait::ZeroOperands, OpTrait::ZeroResult,
                OpTrait::IsTerminator> {
public:
  using Op::Op;
  static StringRef getOperationName() { return "module_terminator"; }

  static void build(Builder *, OperationState *) {}
  LogicalResult verify();
};

} // end namespace mlir

#endif // MLIR_IR_MODULE_H
