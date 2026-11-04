// SPDX-License-Identifier: GPL-3.0-only
//! Same-process typed-frame backend. See README.md for the C caller contract.
//! Metadata borrows end during compile_both; imported code stays caller-owned.
//! Only complete output is published.
#![deny(unsafe_op_in_unsafe_fn)]

#[cfg(all(not(test), not(panic = "abort")))]
compile_error!("the C backend library requires panic=abort");

#[path = "../frontend/bridge/generated/arena.rs"]
mod schema;

use cranelift_codegen::ir::{self, condcodes::IntCC, types, AbiParam, BlockArg, FuncRef, InstBuilder, Value};
use cranelift_codegen::isa::OwnedTargetIsa;
use cranelift_codegen::packed_option::PackedOption;
use cranelift_codegen::settings::{self, Configurable};
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext};
use cranelift_jit::{JITBuilder, JITModule};
use cranelift_module::{default_libcall_names, FuncId, Linkage, Module, ModuleError};
use cranelift_object::{ObjectBuilder, ObjectModule};
use schema::*;
use std::ffi::c_void;
use std::ops::Range;
use std::{mem, ptr, slice, str};

// Dense adapter maps use one Cranelift entity ID including its absent sentinel.
const _: () = assert!(mem::size_of::<PackedOption<Value>>() == 4);
const _: () = assert!(mem::size_of::<PackedOption<FuncRef>>() == 4);

// The descriptor is a process-local C ABI, never a serialized frame. Metadata
// borrows end at compile_both; code must remain callable until owner destruction.
#[derive(Clone, Copy, Debug)]
#[repr(C)]
pub struct ForeignBinding {
    pub library: *const u8,
    pub library_length: u64,
    pub symbol: *const u8,
    pub symbol_length: u64,
    pub parameter_types: *const u32,
    pub parameter_count: u64,
    pub result_type: u32,
    pub address: *const c_void,
}

// Linux x86-64 System V: 4 bytes of ABI padding precede the code pointer.
#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
const _: () = {
    assert!(mem::size_of::<ForeignBinding>() == 64);
    assert!(mem::align_of::<ForeignBinding>() == 8);
    assert!(mem::offset_of!(ForeignBinding, library) == 0);
    assert!(mem::offset_of!(ForeignBinding, library_length) == 8);
    assert!(mem::offset_of!(ForeignBinding, symbol) == 16);
    assert!(mem::offset_of!(ForeignBinding, symbol_length) == 24);
    assert!(mem::offset_of!(ForeignBinding, parameter_types) == 32);
    assert!(mem::offset_of!(ForeignBinding, parameter_count) == 40);
    assert!(mem::offset_of!(ForeignBinding, result_type) == 48);
    assert!(mem::offset_of!(ForeignBinding, address) == 56);
};

// Validated views avoid repeating UTF-8/type checks during bounded resolution.
#[derive(Debug)]
struct Binding<'a> {
    library: &'a str,
    symbol: &'a str,
    parameter_types: &'a [u32],
    result_type: u32,
    address: *const u8,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
enum Status {
    Ready = 0,
    InvalidArgument = 1,
    InvalidFrame = 2,
    UnsupportedTarget = 3,
    BackendFailure = 4,
}

#[derive(Debug)]
struct Failure {
    status: Status,
    message: String,
}

// Cranelift's default provider intentionally leaks mappings on ordinary Drop.
// No caller may execute or retain a usable code address when this guard drops.
struct JitOwner(Option<JITModule>);
impl JitOwner {
    fn module(&mut self) -> &mut JITModule {
        self.0.as_mut().expect("JIT owner has not been consumed")
    }
}
impl Drop for JitOwner {
    fn drop(&mut self) {
        if let Some(module) = self.0.take() {
            // SAFETY: before publication no address escaped. After publication
            // the C destroy contract requires every generated call to finish.
            unsafe { module.free_memory() };
        }
    }
}

struct Compiled {
    _jit: JitOwner,
    // Source function order; nonexports have a null entry, not a second flag.
    entries: Vec<*const u8>,
    object: Vec<u8>,
}

enum Owner {
    Compiled(Compiled),
    Failed(Failure),
}

fn index(id: u32, count: usize) -> usize {
    assert!(id != 0 && (id as usize) <= count, "typed model ID out of range");
    id as usize - 1
}

fn model_span(first: u32, count: u32, total: usize) -> Range<usize> {
    schema::span(first, count, total).expect("typed model span out of range")
}

fn scalar(type_id: u32) -> ScalarType {
    ScalarType::decode(type_id).expect("typed model scalar type")
}

fn opcode(instruction: &[u32; INSTRUCTION_WORDS]) -> Opcode {
    Opcode::decode(instruction[INSTRUCTION_OP]).expect("typed model opcode")
}

// The producer already ran typed.Verify. Adapt names once, retaining only
// borrowed UTF-8 views for this call. This is not an external IR validator.
fn borrow_names<'a>(a: &Arena<'a>) -> Vec<&'a str> {
    a.names.iter().map(|name| {
        let range = model_span(name[NAME_START], name[NAME_COUNT], a.name_bytes.len());
        let bytes = &a.name_bytes[range];
        assert!(!bytes.is_empty() && !bytes.contains(&0), "typed symbol name");
        str::from_utf8(bytes).expect("typed identifier UTF-8")
    }).collect()
}

fn clif_type(type_id: u32) -> ir::Type {
    match scalar(type_id) {
        ScalarType::Bool => types::I8,
        ScalarType::I64 => types::I64,
    }
}

fn abi_parameter(type_id: u32) -> AbiParam {
    let parameter = AbiParam::new(clif_type(type_id));
    match scalar(type_id) {
        ScalarType::Bool => parameter.uext(),
        ScalarType::I64 => parameter,
    }
}

fn value(values: &[PackedOption<Value>], id: u32) -> Value {
    values[index(id, values.len())].expect("typed operand has a CLIF definition")
}

fn edge_arguments(a: &Arena<'_>, edge: &[u32; EDGE_WORDS], values: &[PackedOption<Value>],
                  output: &mut Vec<BlockArg>) {
    assert!(edge[EDGE_ARGUMENT_COUNT] <= MAX_LOCALS as u32, "typed edge argument admission");
    output.clear();
    let arguments = model_span(edge[EDGE_FIRST_ARGUMENT], edge[EDGE_ARGUMENT_COUNT], a.arguments.len());
    for &id in &a.arguments[arguments] { output.push(value(values, id).into()); }
}

// Two concrete consumers instantiate this one lowering. There is no mode
// dispatch, name lookup or source access per instruction. Adapter scratch is
// reserved per module; Cranelift still owns/allocates its actual IR and code.
fn define_bodies<M: Module>(module: &mut M, a: &Arena<'_>, names: &[&str])
    -> Result<Vec<FuncId>, Failure>
{
    assert!(a.functions.len() <= MAX_FUNCTIONS as usize, "typed function admission");
    let mut declarations = Vec::with_capacity(a.functions.len());
    let mut context = module.make_context();
    // Declare the whole module before any body, preserving dense source IDs.
    // Forward, self and mutual calls share this same eager compilation path.
    let mut foreigns = a.foreigns.iter().peekable();
    for (at, function) in a.functions.iter().enumerate() {
        let imported = foreigns.peek().is_some_and(|row| row[FOREIGN_FUNCTION] == at as u32 + 1);
        if imported { foreigns.next(); }
        let source = &a.signatures[index(function[FUNCTION_SIGNATURE], a.signatures.len())];
        assert!(source[SIGNATURE_PARAMETER_COUNT] <= MAX_PARAMETERS as u32,
                "typed signature parameter admission");
        let signature = &mut context.func.signature;
        let parameters = model_span(source[SIGNATURE_FIRST_PARAMETER], source[SIGNATURE_PARAMETER_COUNT],
                                    a.parameter_types.len());
        signature.params.reserve_exact(parameters.len());
        signature.returns.reserve_exact(1);
        for &type_id in &a.parameter_types[parameters] { signature.params.push(abi_parameter(type_id)); }
        signature.returns.push(abi_parameter(source[SIGNATURE_RESULT_TYPE]));
        assert!(function[FUNCTION_EXPORTED] <= 1, "typed export membership");
        let linkage = if imported { Linkage::Import }
            else if function[FUNCTION_EXPORTED] == 1 { Linkage::Export }
            else { Linkage::Local };
        let name = names[index(function[FUNCTION_NAME], names.len())];
        let id = module.declare_function(name, linkage, signature)
            .expect("verified typed function declaration");
        declarations.push(id);
        module.clear_signature(&mut context.func.signature);
    }

    assert!(foreigns.next().is_none(), "typed foreign function order");

    let mut builder_context = FunctionBuilderContext::new();
    let mut blocks = Vec::with_capacity(a.blocks.len());
    let mut values = vec![PackedOption::<Value>::default(); a.values.len()];
    let mut yes_arguments = Vec::with_capacity(MAX_LOCALS as usize);
    let mut no_arguments = Vec::with_capacity(MAX_LOCALS as usize);
    let mut call_arguments = Vec::with_capacity(MAX_PARAMETERS as usize);
    let mut callees = vec![PackedOption::<FuncRef>::default(); a.functions.len()];
    for (&id, function) in declarations.iter().zip(a.functions) {
        // Reuse the declaration's ABI facts and the context's signature storage.
        // Signature::clone would allocate fresh parameter/return buffers here.
        let declaration = module.declarations().get_function_decl(id);
        if declaration.linkage == Linkage::Import { continue; }
        let signature = &declaration.signature;
        context.func.signature.params.extend_from_slice(&signature.params);
        context.func.signature.returns.extend_from_slice(&signature.returns);
        context.func.signature.call_conv = signature.call_conv;
        context.func.name = ir::UserFuncName::user(0, id.as_u32());
        // FuncRefs belong to one CLIF body. Clearing at most 32 dense slots per
        // body avoids repeated signature/name imports for repeated calls.
        callees.fill(PackedOption::default());
        let mut builder = FunctionBuilder::new(&mut context.func, &mut builder_context);
        assert!(function[FUNCTION_BLOCK_COUNT] != 0
                && function[FUNCTION_BLOCK_COUNT] <= MAX_BLOCKS as u32, "typed block admission");
        let function_blocks = model_span(function[FUNCTION_FIRST_BLOCK], function[FUNCTION_BLOCK_COUNT], a.blocks.len());
        assert_eq!(function_blocks.start, blocks.len(), "function block spans must partition their table");
        for _ in function_blocks.clone() { blocks.push(builder.create_block()); }
        for at in function_blocks.clone() {
            let source = &a.blocks[at];
            assert!(source[BLOCK_PARAMETER_COUNT] <= MAX_LOCALS as u32, "typed block parameter admission");
            let parameters = model_span(source[BLOCK_FIRST_PARAMETER], source[BLOCK_PARAMETER_COUNT], a.block_parameters.len());
            for &parameter in &a.block_parameters[parameters] {
                let slot = index(parameter, a.values.len());
                assert_eq!(a.values[slot][VALUE_DEFINITION], BLOCK_DEFINITION | (at as u32 + 1),
                           "block parameter definition mismatch");
                let defined = builder.append_block_param(blocks[at], clif_type(a.values[slot][VALUE_TYPE]));
                let previous = mem::replace(&mut values[slot], defined.into());
                assert!(previous.is_none(), "duplicate CLIF parameter definition");
            }
        }
        for at in function_blocks {
            let source = &a.blocks[at];
            builder.switch_to_block(blocks[at]);
            let instructions = model_span(source[BLOCK_FIRST_INSTRUCTION], source[BLOCK_INSTRUCTION_COUNT], a.instructions.len());
            for instruction_index in instructions {
                let instruction = &a.instructions[instruction_index];
                let result = match opcode(instruction) {
                    Opcode::ConstantI64 => {
                        let literal = a.literals[index(instruction[INSTRUCTION_PAYLOAD], a.literals.len())];
                        Some((builder.ins().iconst(types::I64, literal), I64))
                    }
                    Opcode::ConstantBool => {
                        let literal = instruction[INSTRUCTION_PAYLOAD];
                        assert!(literal <= 1, "typed bool literal is canonical");
                        Some((builder.ins().iconst(types::I8, i64::from(literal)), BOOL))
                    }
                    Opcode::AddI64 => Some((builder.ins().iadd(value(&values, instruction[INSTRUCTION_LEFT]),
                                                              value(&values, instruction[INSTRUCTION_RIGHT])), I64)),
                    Opcode::MultiplyI64 => Some((builder.ins().imul(value(&values, instruction[INSTRUCTION_LEFT]),
                                                                   value(&values, instruction[INSTRUCTION_RIGHT])), I64)),
                    Opcode::GreaterI64 => Some((builder.ins().icmp(IntCC::SignedGreaterThan,
                        value(&values, instruction[INSTRUCTION_LEFT]),
                        value(&values, instruction[INSTRUCTION_RIGHT])), BOOL)),
                    Opcode::EqualI64 => Some((builder.ins().icmp(IntCC::Equal,
                        value(&values, instruction[INSTRUCTION_LEFT]),
                        value(&values, instruction[INSTRUCTION_RIGHT])), BOOL)),
                    Opcode::NotBool => Some((builder.ins().icmp_imm(IntCC::Equal,
                        value(&values, instruction[INSTRUCTION_LEFT]), 0), BOOL)),
                    Opcode::DirectCall => {
                        let call = &a.calls[index(instruction[INSTRUCTION_PAYLOAD], a.calls.len())];
                        let callee = index(call[CALL_CALLEE], declarations.len());
                        let source = &a.signatures[index(a.functions[callee][FUNCTION_SIGNATURE], a.signatures.len())];
                        let reference = match callees[callee].expand() {
                            Some(reference) => reference,
                            None => {
                                let reference = module.declare_func_in_func(declarations[callee], builder.func);
                                callees[callee] = reference.into();
                                reference
                            }
                        };
                        call_arguments.clear();
                        let arguments = model_span(call[CALL_FIRST_ARGUMENT], source[SIGNATURE_PARAMETER_COUNT],
                                                   a.arguments.len());
                        for &argument in &a.arguments[arguments] {
                            call_arguments.push(value(&values, argument));
                        }
                        let emitted = builder.ins().call(reference, &call_arguments);
                        let results = builder.inst_results(emitted);
                        assert_eq!(results.len(), 1, "typed call has one scalar result");
                        Some((results[0], source[SIGNATURE_RESULT_TYPE]))
                    }
                    Opcode::Jump => {
                        let edge = &a.edges[index(instruction[INSTRUCTION_PAYLOAD], a.edges.len())];
                        edge_arguments(a, edge, &values, &mut yes_arguments);
                        builder.ins().jump(blocks[index(edge[EDGE_TARGET], blocks.len())], &yes_arguments);
                        None
                    }
                    Opcode::Branch => {
                        let first = index(instruction[INSTRUCTION_PAYLOAD], a.edges.len());
                        let edges = a.edges.get(first..first + 2).expect("typed branch edge pair");
                        let yes = &edges[0];
                        let no = &edges[1];
                        edge_arguments(a, yes, &values, &mut yes_arguments);
                        edge_arguments(a, no, &values, &mut no_arguments);
                        builder.ins().brif(value(&values, instruction[INSTRUCTION_LEFT]),
                            blocks[index(yes[EDGE_TARGET], blocks.len())], &yes_arguments,
                            blocks[index(no[EDGE_TARGET], blocks.len())], &no_arguments);
                        None
                    }
                    Opcode::Return => {
                        builder.ins().return_(&[value(&values, instruction[INSTRUCTION_LEFT])]);
                        None
                    }
                };
                if let Some((defined, result_type)) = result {
                    let slot = index(instruction[INSTRUCTION_RESULT], values.len());
                    assert_eq!(a.values[slot][VALUE_TYPE], result_type, "typed instruction result type");
                    assert_eq!(builder.func.dfg.value_type(defined), clif_type(result_type),
                               "CLIF instruction result type");
                    assert_eq!(a.values[slot][VALUE_DEFINITION], instruction_index as u32 + 1,
                               "instruction result definition mismatch");
                    let previous = mem::replace(&mut values[slot], defined.into());
                    assert!(previous.is_none(), "duplicate CLIF instruction definition");
                }
            }
        }
        builder.seal_all_blocks();
        builder.finalize(module.target_config());
        // Pinned Context::compile verifies before optimizing; keep that
        // mandatory check enabled rather than repeating it in the adapter.
        module.define_function(id, &mut context).map_err(|error| {
            module_failure(error, names[index(function[FUNCTION_NAME], names.len())])
        })?;
        module.clear_context(&mut context);
    }
    assert_eq!(blocks.len(), a.blocks.len(), "unowned typed blocks");
    Ok(declarations)
}

// Both native consumers use the same failure classification. Backend capacity
// and OS errors are diagnostics; invalid declarations/verified IR are bugs.
fn module_failure(error: ModuleError, stage: &str) -> Failure {
    use cranelift_codegen::CodegenError;
    match &error {
        ModuleError::Undeclared(_) | ModuleError::IncompatibleDeclaration(_)
        | ModuleError::IncompatibleSignature(..) | ModuleError::IncompatibleDataTls(..)
        | ModuleError::DuplicateDefinition(_) | ModuleError::InvalidImportDefinition(_)
        | ModuleError::Flag(_) => panic!("{stage}: internal module invariant: {error}"),
        ModuleError::Compilation(codegen) => match codegen {
            CodegenError::Verifier(_) | CodegenError::Regalloc(_)
            | CodegenError::RegisterMappingError(_) => panic!("{stage}: internal codegen invariant: {error}"),
            CodegenError::ImplLimitExceeded | CodegenError::CodeTooLarge
            | CodegenError::Unsupported(_) => {},
        },
        ModuleError::Allocation { err } => {
            if err.kind() == std::io::ErrorKind::OutOfMemory { std::process::abort(); }
        }
        ModuleError::Backend(_) => {},
    }
    Failure { status: Status::BackendFailure, message: format!("{stage}: {error}") }
}

fn native_isa() -> Result<OwnedTargetIsa, Failure> {
    if !cfg!(all(target_os = "linux", target_arch = "x86_64")) {
        return Err(Failure { status: Status::UnsupportedTarget,
                             message: "only Linux x86-64 System V is admitted".into() });
    }
    let mut flags = settings::builder();
    flags.set("opt_level", "none").expect("pinned Cranelift optimization setting");
    flags.set("is_pic", "true").expect("pinned Cranelift PIC setting");
    flags.set("enable_verifier", "true").expect("pinned Cranelift verifier setting");
    let builder = cranelift_native::builder().map_err(|error| Failure {
        status: Status::UnsupportedTarget, message: error.into(),
    })?;
    builder.finish(settings::Flags::new(flags)).map_err(|error| Failure {
        status: Status::UnsupportedTarget, message: error.to_string(),
    })
}

// At most 32 imports by 32 bindings, once before either native consumer.
// Source verification owns semantic validity; these assertions protect adapter
// invariants, while absent/mismatched external bindings are ordinary failures.
fn resolve_imports(a: &Arena<'_>, names: &[&str], bindings: &[Binding<'_>])
    -> Result<Vec<*const u8>, Failure>
{
    assert!(a.foreigns.len() <= MAX_FUNCTIONS as usize, "typed foreign admission");
    let mut addresses = Vec::with_capacity(a.foreigns.len());
    let mut previous = 0;
    for foreign in a.foreigns {
        let function_id = foreign[FOREIGN_FUNCTION];
        assert!(function_id > previous, "typed foreign function order");
        previous = function_id;
        let function = &a.functions[index(function_id, a.functions.len())];
        assert_eq!(function[FUNCTION_FIRST_BLOCK], 0, "imported body starts at zero");
        assert_eq!(function[FUNCTION_BLOCK_COUNT], 0, "imported function has no body");
        assert_eq!(function[FUNCTION_EXPORTED], 0, "imported function is not exported");
        let symbol = names[index(function[FUNCTION_NAME], names.len())];
        let library = names[index(foreign[FOREIGN_LIBRARY], names.len())];
        let signature = &a.signatures[index(function[FUNCTION_SIGNATURE], a.signatures.len())];
        let parameters = model_span(signature[SIGNATURE_FIRST_PARAMETER],
                                    signature[SIGNATURE_PARAMETER_COUNT], a.parameter_types.len());
        let binding = bindings.iter().find(|binding| binding.library == library && binding.symbol == symbol);
        let reason = match binding {
            Some(binding) if binding.parameter_types == &a.parameter_types[parameters]
                && binding.result_type == signature[SIGNATURE_RESULT_TYPE] => {
                    addresses.push(binding.address);
                    continue;
                }
            Some(_) => "signature mismatch",
            None => "no matching library and symbol binding",
        };
        let origin = &a.origins[index(function[FUNCTION_ORIGIN], a.origins.len())];
        return Err(Failure { status: Status::BackendFailure, message: format!(
            "unresolved foreign symbol {symbol:?} in library {library:?} at document {} span {}..{}: {reason}",
            origin[ORIGIN_DOCUMENT], origin[ORIGIN_START], origin[ORIGIN_END]) });
    }
    Ok(addresses)
}

fn jit_builder(isa: OwnedTargetIsa, a: &Arena<'_>, names: &[&str], addresses: &[*const u8]) -> JITBuilder {
    assert_eq!(a.foreigns.len(), addresses.len(), "every import was resolved");
    let mut builder = JITBuilder::with_isa(isa, default_libcall_names());
    for (foreign, &address) in a.foreigns.iter().zip(addresses) {
        assert!(!address.is_null(), "resolved code address is nonnull");
        let function = &a.functions[index(foreign[FOREIGN_FUNCTION], a.functions.len())];
        builder.symbol(names[index(function[FUNCTION_NAME], names.len())], address);
    }
    // with_isa installs dlsym; callbacks run newest-first and None falls through.
    // All admitted symbols are registered above. Unexpected helper/import lookup
    // is a compiler bug, never permission to resolve process-global symbols.
    builder.symbol_lookup_fn(Box::new(|name| panic!("unexpected JIT symbol lookup: {name}")));
    builder
}

fn compile(a: &Arena<'_>, bindings: &[Binding<'_>]) -> Result<Compiled, Failure> {
    // Upstream perf-map creation uses unwrap. This bounded compiler admits
    // no perf-map side effects; return an owned environment error before JIT.
    if std::env::var_os("PERF_BUILDID_DIR").is_some() {
        return Err(Failure { status: Status::BackendFailure,
            message: "PERF_BUILDID_DIR profiling is unsupported by this backend".into() });
    }
    let names = borrow_names(a);
    let addresses = resolve_imports(a, &names, bindings)?;
    let isa = native_isa()?;
    // Finish object bytes first; a later JIT error drops them without ever
    // publishing a partial result. No semantic body is copied or retained.
    let object_builder = ObjectBuilder::new(isa.clone(), "chibi-vnext", default_libcall_names())
        .map_err(|error| Failure { status: Status::BackendFailure, message: error.to_string() })?;
    let mut object = ObjectModule::new(object_builder);
    define_bodies(&mut object, a, &names)?;
    let object = object.finish().emit().map_err(|error| Failure {
        status: Status::BackendFailure, message: format!("object emission: {error}"),
    })?;
    let mut jit = JitOwner(Some(JITModule::new(jit_builder(isa, a, &names, &addresses))));
    let functions = define_bodies(jit.module(), a, &names)?;
    jit.module().finalize_definitions().map_err(|error| module_failure(error, "JIT finalization"))?;
    let entries = functions.iter().zip(a.functions).map(|(&id, source)| {
        if source[FUNCTION_EXPORTED] == 1 { jit.module().get_finalized_function(id) }
        else { ptr::null() }
    }).collect();
    Ok(Compiled { _jit: jit, entries, object })
}

// Used for all public input arrays. This checks only what raw pointers can
// prove; valid readable storage and no concurrent mutation remain obligations.
fn input_length<T>(pointer: *const T, count: u64) -> Result<usize, Failure> {
    let count = usize::try_from(count).ok().filter(|count| *count <= MAX_ELEMENTS)
        .filter(|count| count.checked_mul(mem::size_of::<T>())
            .is_some_and(|bytes| bytes <= isize::MAX as usize))
        .ok_or_else(|| Failure { status: Status::InvalidArgument, message: "input element count/extent out of range".into() })?;
    if count != 0 && (pointer.is_null() || !pointer.is_aligned()) {
        return Err(Failure { status: Status::InvalidArgument, message: "nonnull aligned input required for nonempty array".into() });
    }
    Ok(count)
}

// SAFETY: nonempty pointers name a readable allocation of the checked extent.
unsafe fn input_slice<'a, T>(pointer: *const T, count: usize) -> &'a [T] {
    if count == 0 { &[] } else { unsafe { slice::from_raw_parts(pointer, count) } }
}

// SAFETY: each admitted nonempty metadata pointer names a readable allocation
// for this call. Null/count, alignment and extents are checked before borrowing.
unsafe fn borrow_bindings<'a>(descriptors: &[ForeignBinding]) -> Result<Vec<Binding<'a>>, Failure> {
    let mut bindings: Vec<Binding<'a>> = Vec::with_capacity(descriptors.len());
    for (at, descriptor) in descriptors.iter().enumerate() {
        let invalid = |reason: &str| Failure { status: Status::InvalidArgument,
            message: format!("foreign binding {}: {reason}", at + 1) };
        let field_error = |field: &str, failure: Failure| Failure { status: failure.status,
            message: format!("foreign binding {} {field}: {}", at + 1, failure.message) };
        let library_count = input_length(descriptor.library, descriptor.library_length)
            .map_err(|failure| field_error("library", failure))?;
        let symbol_count = input_length(descriptor.symbol, descriptor.symbol_length)
            .map_err(|failure| field_error("symbol", failure))?;
        let parameter_count = input_length(descriptor.parameter_types, descriptor.parameter_count)
            .map_err(|failure| field_error("parameter types", failure))?;
        if library_count == 0 || library_count > MAX_LIBRARY_BYTES as usize {
            return Err(invalid("library length must be 1..256 bytes"));
        }
        if symbol_count == 0 || symbol_count > MAX_SOURCE_BYTES as usize {
            return Err(invalid("symbol length exceeds source-name admission"));
        }
        if parameter_count > MAX_PARAMETERS as usize {
            return Err(invalid("parameter count exceeds scalar signature admission"));
        }
        if descriptor.address.is_null() { return Err(invalid("nonnull code address required")); }
        if ScalarType::decode(descriptor.result_type).is_none() {
            return Err(invalid("result type must be bool or i64"));
        }
        let (library, symbol, parameter_types) = unsafe {
            (input_slice(descriptor.library, library_count), input_slice(descriptor.symbol, symbol_count),
             input_slice(descriptor.parameter_types, parameter_count))
        };
        if library.contains(&0) || symbol.contains(&0) {
            return Err(invalid("library and symbol must not contain NUL"));
        }
        let library = str::from_utf8(library).map_err(|_| invalid("library must be UTF-8"))?;
        let symbol = str::from_utf8(symbol).map_err(|_| invalid("symbol must be UTF-8"))?;
        if parameter_types.iter().any(|&type_id| ScalarType::decode(type_id).is_none()) {
            return Err(invalid("parameter types must be bool or i64"));
        }
        // AOT link names are bare C symbols: even different libraries cannot
        // disambiguate duplicate symbols. Bound: at most 32 descriptors.
        if bindings.iter().any(|binding| binding.symbol == symbol) {
            return Err(invalid(&format!("duplicate native symbol {symbol:?}")));
        }
        bindings.push(Binding { library, symbol, parameter_types,
            result_type: descriptor.result_type, address: descriptor.address.cast() });
    }
    Ok(bindings)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_compile_both(
    words: *const u32, word_count: u64, literals: *const i64, literal_count: u64,
    names: *const u8, name_byte_count: u64,
    bindings: *const ForeignBinding, binding_count: u64,
) -> *mut c_void {
    // Check every pointer/count pair before constructing any input slice.
    let result = (|| {
        let word_count = input_length(words, word_count)?;
        let literal_count = input_length(literals, literal_count)?;
        let name_byte_count = input_length(names, name_byte_count)?;
        let binding_count = input_length(bindings, binding_count)?;
        if binding_count > MAX_FUNCTIONS as usize {
            return Err(Failure { status: Status::InvalidArgument,
                message: "foreign binding count exceeds 32".into() });
        }
        // SAFETY: counts were checked; remaining validity is the caller contract.
        let (words, literals, names, descriptors) = unsafe {
            (input_slice(words, word_count), input_slice(literals, literal_count),
             input_slice(names, name_byte_count), input_slice(bindings, binding_count))
        };
        let bindings = unsafe { borrow_bindings(descriptors) }?;
        let arena = Arena::from_parts(words, literals, names).map_err(|error| Failure {
            status: Status::InvalidFrame, message: format!("typed frame: {error:?}"),
        })?;
        compile(&arena, &bindings)
    })();
    let owner = match result { Ok(compiled) => Owner::Compiled(compiled), Err(error) => Owner::Failed(error) };
    Box::into_raw(Box::new(owner)).cast()
}

// SAFETY: any nonnull handle is a live result from compile_both, used without
// concurrent destruction. Null is an ordinary public argument error.
unsafe fn owner<'a>(handle: *const c_void) -> Option<&'a Owner> {
    if handle.is_null() { None } else { Some(unsafe { &*handle.cast::<Owner>() }) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_status(handle: *const c_void) -> u32 {
    match unsafe { owner(handle) } {
        Some(Owner::Compiled(_)) => Status::Ready as u32,
        Some(Owner::Failed(error)) => error.status as u32,
        None => Status::InvalidArgument as u32,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_entry(handle: *const c_void, function_id: u32) -> *const c_void {
    let Some(Owner::Compiled(compiled)) = (unsafe { owner(handle) }) else { return ptr::null(); };
    function_id.checked_sub(1).and_then(|at| compiled.entries.get(at as usize))
        .copied().unwrap_or(ptr::null()).cast()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_diagnostic_length(handle: *const c_void) -> u64 {
    match unsafe { owner(handle) } { Some(Owner::Failed(error)) => error.message.len() as u64, _ => 0 }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_object_length(handle: *const c_void) -> u64 {
    match unsafe { owner(handle) } { Some(Owner::Compiled(compiled)) => compiled.object.len() as u64, _ => 0 }
}

// Both artifact consumers require all-or-nothing copy. No invalid destination
// is touched; nonempty writable allocation/nonoverlap remains the C contract.
unsafe fn copy_bytes(bytes: &[u8], destination: *mut u8, capacity: u64) -> i32 {
    let Ok(capacity) = usize::try_from(capacity) else { return 3; };
    if capacity > isize::MAX as usize { return 3; }
    if capacity < bytes.len() { return 2; }
    if !bytes.is_empty() {
        if destination.is_null() { return 3; }
        unsafe { ptr::copy_nonoverlapping(bytes.as_ptr(), destination, bytes.len()) };
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_diagnostic_copy(
    handle: *const c_void, destination: *mut u8, capacity: u64,
) -> i32 {
    let bytes = match unsafe { owner(handle) } {
        Some(Owner::Failed(error)) => error.message.as_bytes(),
        Some(Owner::Compiled(_)) => &[],
        None => return 1,
    };
    unsafe { copy_bytes(bytes, destination, capacity) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_object_copy(
    handle: *const c_void, destination: *mut u8, capacity: u64,
) -> i32 {
    match unsafe { owner(handle) } {
        Some(Owner::Compiled(compiled)) => unsafe { copy_bytes(&compiled.object, destination, capacity) },
        Some(Owner::Failed(_)) => 4,
        None => 1,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_destroy(handle: *mut c_void) -> i32 {
    if handle.is_null() { return 1; }
    // SAFETY: exactly one caller owns the handle and every generated call has
    // finished. Dropping Compiled invokes the explicit JitOwner cleanup.
    unsafe { drop(Box::from_raw(handle.cast::<Owner>())) };
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_jit::{BranchProtection, JITMemoryKind, JITMemoryProvider, SystemMemoryProvider};
    use cranelift_module::{ModuleError, ModuleResult};
    use std::io;
    use std::sync::{Arc, Mutex};

    #[test]
    fn public_framing_failures_have_owned_diagnostics_and_no_outputs() {
        // Actual null/zero and live stack buffers only. Never construct a
        // deliberately misaligned, stale or fabricated nonnull pointer.
        let word = [0u32];
        let literal = [0i64];
        let name = [0u8];
        let wrong_version = [VERSION + 1; HEADER_WORDS];
        let oversized = MAX_ELEMENTS as u64 + 1;
        let cases = [
            (ptr::null(), 0, ptr::null(), 0, ptr::null(), 0, Status::InvalidFrame),
            (ptr::null(), 1, ptr::null(), 0, ptr::null(), 0, Status::InvalidArgument),
            (ptr::null(), 0, ptr::null(), 1, ptr::null(), 0, Status::InvalidArgument),
            (ptr::null(), 0, ptr::null(), 0, ptr::null(), 1, Status::InvalidArgument),
            (word.as_ptr(), oversized, ptr::null(), 0, ptr::null(), 0, Status::InvalidArgument),
            (ptr::null(), 0, literal.as_ptr(), oversized, ptr::null(), 0, Status::InvalidArgument),
            (ptr::null(), 0, ptr::null(), 0, name.as_ptr(), oversized, Status::InvalidArgument),
            (wrong_version.as_ptr(), wrong_version.len() as u64,
             ptr::null(), 0, ptr::null(), 0, Status::InvalidFrame),
        ];
        for (words, word_count, literals, literal_count, names, name_count, expected) in cases {
            let handle = unsafe {
                chibi_vnext_compile_both(words, word_count, literals, literal_count, names, name_count, ptr::null(), 0)
            };
            assert!(!failed_owner(handle, expected).is_empty());
        }
    }

    // Exercise the public failure owner, byte-copy and destruction contract for
    // both old framing failures and the foreign-input/resolution failures.
    fn failed_owner(handle: *mut c_void, expected: Status) -> String {
        assert!(!handle.is_null());
        unsafe {
            assert_eq!(chibi_vnext_status(handle), expected as u32);
            assert!(chibi_vnext_entry(handle, 1).is_null());
            assert_eq!(chibi_vnext_object_length(handle), 0);
            assert_eq!(chibi_vnext_object_copy(handle, ptr::null_mut(), 0), 4);
            let count = chibi_vnext_diagnostic_length(handle) as usize;
            assert!(count > 0);
            let mut destination = vec![0x7f; count];
            let too_small = chibi_vnext_diagnostic_copy(handle, destination.as_mut_ptr(), count as u64 - 1);
            assert_eq!(too_small, 2);
            assert!(destination.iter().all(|byte| *byte == 0x7f));
            let copied = chibi_vnext_diagnostic_copy(handle, destination.as_mut_ptr(), count as u64);
            assert_eq!(copied, 0);
            let diagnostic = String::from_utf8(destination).unwrap();
            let closed = chibi_vnext_destroy(handle);
            assert_eq!(closed, 0);
            diagnostic
        }
    }

    extern "C" fn foreign_echo(value: i64) -> i64 { value }
    extern "C" fn foreign_flag() -> bool { true }

    fn descriptor(library: &[u8], symbol: &[u8], parameter_types: &[u32], result_type: u32) -> ForeignBinding {
        ForeignBinding {
            library: library.as_ptr(), library_length: library.len() as u64,
            symbol: symbol.as_ptr(), symbol_length: symbol.len() as u64,
            parameter_types: parameter_types.as_ptr(), parameter_count: parameter_types.len() as u64,
            result_type, address: foreign_echo as *const () as *const c_void,
        }
    }

    // One import-only model checks binding admission without compiling or
    // executing native code. Original-source acceptance is a separate gate.
    fn import_frame() -> (Vec<u32>, Vec<u8>) {
        let symbol = b"ForeignEcho";
        let library = b"test:binding";
        let mut words = vec![VERSION, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 2];
        assert_eq!(words.len(), HEADER_WORDS);
        words.extend_from_slice(&[1, 1, 0, 0, 1, 0]); // function
        words.extend_from_slice(&[1, 2]); // foreign
        words.extend_from_slice(&[0, 1, I64]); // signature
        words.push(I64);
        words.extend_from_slice(&[17, 23, 47]); // original document/span
        words.extend_from_slice(&[0, symbol.len() as u32]);
        words.extend_from_slice(&[symbol.len() as u32, library.len() as u32]);
        let mut names = symbol.to_vec();
        names.extend_from_slice(library);
        (words, names)
    }

    #[test]
    fn public_foreign_arguments_reject_before_output_construction() {
        let parameter = [I64];
        let invalid_parameter = [0];
        let base = descriptor(b"test:binding", b"ForeignEcho", &parameter, I64);
        let cases = [
            ForeignBinding { library: ptr::null(), ..base },
            ForeignBinding { library_length: 0, ..base },
            ForeignBinding { library_length: MAX_LIBRARY_BYTES as u64 + 1, ..base },
            ForeignBinding { library_length: u64::MAX, ..base },
            ForeignBinding { symbol: ptr::null(), ..base },
            ForeignBinding { symbol_length: 0, ..base },
            ForeignBinding { symbol_length: MAX_SOURCE_BYTES as u64 + 1, ..base },
            ForeignBinding { symbol_length: u64::MAX, ..base },
            ForeignBinding { parameter_types: ptr::null(), ..base },
            ForeignBinding { parameter_count: MAX_PARAMETERS as u64 + 1, ..base },
            ForeignBinding { parameter_count: u64::MAX, ..base },
            ForeignBinding { parameter_types: invalid_parameter.as_ptr(), ..base },
            ForeignBinding { result_type: 0, ..base },
            ForeignBinding { address: ptr::null(), ..base },
            descriptor(b"a\0b", b"ForeignEcho", &parameter, I64),
            descriptor(b"test:binding", b"a\0b", &parameter, I64),
            descriptor(b"\xff", b"ForeignEcho", &parameter, I64),
            descriptor(b"test:binding", b"\xff", &parameter, I64),
        ];
        // Every pointer is null or comes from actual live storage. Oversized
        // counts must reject before constructing a slice beyond that storage.
        for binding in &cases {
            let handle = unsafe { chibi_vnext_compile_both(ptr::null(), 0, ptr::null(), 0,
                ptr::null(), 0, binding, 1) };
            failed_owner(handle, Status::InvalidArgument);
        }
        // A later descriptor must retain its one-based index and exact field
        // through the common pointer/count checks, before constructing outputs.
        let pointer_reason = "nonnull aligned input required for nonempty array";
        let count_reason = "input element count/extent out of range";
        let contextual_cases = [
            (ForeignBinding { library: ptr::null(), ..base }, "library", pointer_reason),
            (ForeignBinding { symbol: ptr::null(), ..base }, "symbol", pointer_reason),
            (ForeignBinding { parameter_types: ptr::null(), ..base }, "parameter types", pointer_reason),
            (ForeignBinding { library_length: u64::MAX, ..base }, "library", count_reason),
            (ForeignBinding { symbol_length: u64::MAX, ..base }, "symbol", count_reason),
            (ForeignBinding { parameter_count: u64::MAX, ..base }, "parameter types", count_reason),
            (ForeignBinding { parameter_types: parameter.as_ptr().cast::<u8>().wrapping_add(1).cast(), ..base },
                "parameter types", pointer_reason),
        ];
        for (binding, field, reason) in contextual_cases {
            let descriptors = [base, binding];
            let handle = unsafe { chibi_vnext_compile_both(ptr::null(), 0, ptr::null(), 0,
                ptr::null(), 0, descriptors.as_ptr(), descriptors.len() as u64) };
            assert_eq!(failed_owner(handle, Status::InvalidArgument),
                format!("foreign binding 2 {field}: {reason}"));
        }
        for (bindings, count) in [(ptr::null(), 1), (&base as *const _, 33), (&base as *const _, u64::MAX)] {
            let handle = unsafe { chibi_vnext_compile_both(ptr::null(), 0, ptr::null(), 0,
                ptr::null(), 0, bindings, count) };
            failed_owner(handle, Status::InvalidArgument);
        }
        for library in [b"test:binding".as_slice(), b"another:library".as_slice()] {
            let duplicates = [base, descriptor(library, b"ForeignEcho", &parameter, I64)];
            let handle = unsafe { chibi_vnext_compile_both(ptr::null(), 0, ptr::null(), 0,
                ptr::null(), 0, duplicates.as_ptr(), 2) };
            assert!(failed_owner(handle, Status::InvalidArgument).contains("duplicate native symbol"));
        }
        let valid = [base, ForeignBinding {
            address: foreign_flag as *const () as *const c_void,
            ..descriptor(b"test:binding", b"ZeroArity", &[], BOOL)
        }];
        let views = unsafe { borrow_bindings(&valid) }.unwrap();
        assert_eq!(views.len(), 2);
        assert_eq!(views[0].parameter_types, &[I64]);
        assert!(views[1].parameter_types.is_empty());
    }

    #[test]
    fn foreign_resolution_requires_exact_library_symbol_and_signature() {
        let (words, names) = import_frame();
        let arena = Arena::from_parts(&words, &[], &names).unwrap();
        let parameter = [I64];
        let base = descriptor(b"test:binding", b"ForeignEcho", &parameter, I64);
        let valid = [base];
        let bindings = unsafe { borrow_bindings(&valid) }.unwrap();
        let resolved = resolve_imports(&arena, &borrow_names(&arena), &bindings).unwrap();
        assert_eq!(resolved, vec![base.address.cast::<u8>()]);
        assert!(std::env::var_os("PERF_BUILDID_DIR").is_none(), "test requires profiling disabled");
        let absent = unsafe { chibi_vnext_compile_both(words.as_ptr(), words.len() as u64,
            ptr::null(), 0, names.as_ptr(), names.len() as u64, ptr::null(), 0) };
        let message = failed_owner(absent, Status::BackendFailure);
        assert!(message.contains("ForeignEcho") && message.contains("test:binding"));
        assert!(message.contains("document 17 span 23..47"));
        let wrong_parameter = [BOOL];
        let too_many_parameters = [I64, I64];
        let cases = [
            descriptor(b"other:binding", b"ForeignEcho", &parameter, I64),
            descriptor(b"test:binding", b"OtherSymbol", &parameter, I64),
            descriptor(b"test:binding", b"ForeignEcho", &wrong_parameter, I64),
            descriptor(b"test:binding", b"ForeignEcho", &[], I64),
            descriptor(b"test:binding", b"ForeignEcho", &too_many_parameters, I64),
            ForeignBinding { result_type: BOOL, ..base },
        ];
        for (at, binding) in cases.iter().enumerate() {
            let handle = unsafe { chibi_vnext_compile_both(words.as_ptr(), words.len() as u64,
                ptr::null(), 0, names.as_ptr(), names.len() as u64, binding, 1) };
            let message = failed_owner(handle, Status::BackendFailure);
            assert!(message.contains("ForeignEcho") && message.contains("document 17 span 23..47"));
            assert!(message.contains(if at < 2 { "no matching" } else { "signature mismatch" }));
        }
    }

    #[test]
    fn compiled_owner_outlives_all_borrowed_metadata() {
        // A generic import plus a two-parameter equality wrapper exercises
        // DirectCall/EqualI64 in both consumers. It is not a source substitute.
        let symbol = b"ForeignEcho";
        let library = b"test:binding";
        let exported = b"CompareForeign";
        let mut words = vec![VERSION, 2, 1, 2, 3, 1, 2, 4, 3, 0, 1, 1, 3, 3];
        words.extend_from_slice(&[1, 1, 0, 0, 1, 0, 3, 2, 0, 1, 2, 1]); // functions
        words.extend_from_slice(&[1, 2]); // foreign
        words.extend_from_slice(&[0, 1, I64, 1, 2, BOOL]); // signatures
        words.extend_from_slice(&[I64, I64, I64]);
        words.extend_from_slice(&[0, 2, 0, 3]); // block
        words.extend_from_slice(&[1, 2]); // block parameters
        words.extend_from_slice(&[I64, BLOCK_DEFINITION | 1, I64, BLOCK_DEFINITION | 1, I64, 1, BOOL, 2]);
        words.extend_from_slice(&[DIRECT_CALL, 3, 0, 0, 1, 3,
                                 EQUAL_I64, 4, 3, 2, 0, 3, RETURN, 0, 4, 0, 0, 3]);
        words.extend_from_slice(&[1, 0]); // call
        words.push(1); // call argument
        words.extend_from_slice(&[17, 23, 47, 17, 50, 80, 17, 60, 75]); // origins
        words.extend_from_slice(&[0, symbol.len() as u32, symbol.len() as u32, library.len() as u32,
            (symbol.len() + library.len()) as u32, exported.len() as u32]);
        let mut names = [symbol.as_slice(), library.as_slice(), exported.as_slice()].concat();
        let mut parameters = vec![I64];
        let mut binding_library = library.to_vec();
        let mut binding_symbol = symbol.to_vec();
        let mut descriptors = vec![descriptor(&binding_library, &binding_symbol, &parameters, I64)];
        let handle = unsafe { chibi_vnext_compile_both(words.as_ptr(), words.len() as u64,
            ptr::null(), 0, names.as_ptr(), names.len() as u64, descriptors.as_ptr(), descriptors.len() as u64) };
        assert!(!handle.is_null());
        // Poison the actual borrowed storage, then free it. The real function's
        // code remains loaded for the test process; there is no global state.
        words.fill(0);
        names.fill(0);
        parameters.fill(0);
        binding_library.fill(0);
        binding_symbol.fill(0);
        for descriptor in &mut descriptors { descriptor.address = ptr::null(); }
        drop((words, names, parameters, binding_library, binding_symbol, descriptors));
        // Keep an owned Rust guard even if an assertion fails. No generated
        // address escapes this scope or survives destruction of the owner.
        let owner = unsafe { Box::from_raw(handle.cast::<Owner>()) };
        assert_eq!(unsafe { chibi_vnext_status(handle) }, Status::Ready as u32);
        assert!(unsafe { chibi_vnext_entry(handle, 1) }.is_null());
        let entry = unsafe { chibi_vnext_entry(handle, 2) };
        assert!(!entry.is_null());
        let compare: extern "C" fn(i64, i64) -> bool = unsafe { mem::transmute(entry) };
        for (left, right) in [(0, 0), (0, 1), (i64::MIN, i64::MIN), (i64::MIN, i64::MAX),
                              (i64::MAX, i64::MAX), (1_i64 << 40, 0)] {
            assert_eq!(compare(left, right), left == right);
        }
        let object_length = unsafe { chibi_vnext_object_length(handle) };
        assert!(object_length > 0);
        let mut object = vec![0; object_length as usize];
        assert_eq!(unsafe { chibi_vnext_object_copy(handle, object.as_mut_ptr(), object_length) }, 0);
        assert!(object.starts_with(b"\x7fELF"));
        assert_eq!(unsafe { chibi_vnext_destroy(Box::into_raw(owner).cast()) }, 0);
    }

    #[test]
    #[should_panic(expected = "unexpected JIT symbol lookup: malloc")]
    fn jit_lookup_guard_cannot_fall_through_to_process_symbols() {
        let (words, names) = import_frame();
        let arena = Arena::from_parts(&words, &[], &names).unwrap();
        let parameters = [I64];
        let descriptors = [descriptor(b"test:binding", b"ForeignEcho", &parameters, I64)];
        let bindings = unsafe { borrow_bindings(&descriptors) }.unwrap();
        let names = borrow_names(&arena);
        let addresses = resolve_imports(&arena, &names, &bindings).unwrap();
        let mut jit = JitOwner(Some(JITModule::new(jit_builder(native_isa().unwrap(), &arena, &names, &addresses))));
        let signature = jit.module().make_signature();
        let unexpected = jit.module().declare_function("malloc", Linkage::Import, &signature).unwrap();
        jit.module().get_address(&unexpected.into());
    }

    #[test]
    fn copy_is_exact_or_leaves_destination_unchanged() {
        let source = [0u8, 128, 255];
        let mut destination = [9u8; 5];
        let small = unsafe { copy_bytes(&source, destination.as_mut_ptr(), 2) };
        assert_eq!(small, 2);
        assert_eq!(destination, [9; 5]);
        let copied = unsafe { copy_bytes(&source, destination.as_mut_ptr(), 5) };
        assert_eq!(copied, 0);
        assert_eq!(destination, [0, 128, 255, 9, 9]);
        let empty = unsafe { copy_bytes(&[], ptr::null_mut(), 0) };
        assert_eq!(empty, 0);
        let null = unsafe { copy_bytes(&source, ptr::null_mut(), 3) };
        assert_eq!(null, 3);
    }

    #[derive(Default)]
    struct AllocationTrace {
        code: usize,
        current: usize,
        code_was_executable: bool,
        current_was_writable: bool,
        explicit_frees: usize,
        code_released: bool,
        current_waited_for_drop: bool,
    }

    // Addresses came from actual mappings. Inspect kernel metadata only;
    // never dereference released code or fabricate a pointer for this check.
    fn permissions(maps: &str, address: usize) -> Option<&str> {
        maps.lines().find_map(|line| {
            let mut fields = line.split_whitespace();
            let (start, end) = fields.next().unwrap().split_once('-').unwrap();
            let range = usize::from_str_radix(start, 16).unwrap()..usize::from_str_radix(end, 16).unwrap();
            let permissions = fields.next().unwrap();
            range.contains(&address).then_some(permissions)
        })
    }

    // Test-only provider: first compile/finalize real code into the provider's
    // leak-on-ordinary-Drop allocations, then allocate a new current mapping
    // and fail. This is not a production mode or a source-acceptance recipe.
    struct FailAfterAllocation {
        inner: SystemMemoryProvider,
        trace: Arc<Mutex<AllocationTrace>>,
    }
    impl JITMemoryProvider for FailAfterAllocation {
        fn allocate(&mut self, size: usize, align: u64, kind: JITMemoryKind) -> io::Result<*mut u8> {
            let executable = matches!(&kind, JITMemoryKind::Executable);
            let pointer = self.inner.allocate(size, align, kind)?;
            if executable {
                let mut trace = self.trace.lock().unwrap();
                assert_eq!(trace.code, 0, "tiny fixture makes one code allocation");
                trace.code = pointer.addr();
            }
            Ok(pointer)
        }
        unsafe fn free_memory(&mut self) {
            unsafe { self.inner.free_memory() };
            let maps = std::fs::read_to_string("/proc/self/maps").unwrap();
            let mut trace = self.trace.lock().unwrap();
            trace.explicit_frees += 1;
            trace.code_released = permissions(&maps, trace.code).is_none();
            trace.current_waited_for_drop = permissions(&maps, trace.current).is_some();
        }
        fn finalize(&mut self, protection: BranchProtection) -> ModuleResult<()> {
            self.inner.finalize(protection)?;
            let current = self.inner.allocate(64, 16, JITMemoryKind::Executable)
                .map_err(|err| ModuleError::Allocation { err })?;
            let maps = std::fs::read_to_string("/proc/self/maps").unwrap();
            let mut trace = self.trace.lock().unwrap();
            trace.current = current.addr();
            trace.code_was_executable = permissions(&maps, trace.code)
                .is_some_and(|value| value.contains('x'));
            trace.current_was_writable = permissions(&maps, trace.current)
                .is_some_and(|value| value.contains('w') && !value.contains('x'));
            Err(ModuleError::Allocation { err: io::Error::other("injected finalization failure") })
        }
    }

    #[test]
    fn failed_finalization_releases_finalized_and_current_mappings() {
        // A valid tiny typed fixture tests lowering/cleanup only. Real
        // source-produced JIT and linked-AOT acceptance remain separate gates.
        let mut functions = [[0; FUNCTION_WORDS]];
        functions[0][FUNCTION_NAME] = 1;
        functions[0][FUNCTION_SIGNATURE] = 1;
        functions[0][FUNCTION_BLOCK_COUNT] = 1;
        functions[0][FUNCTION_ORIGIN] = 1;
        functions[0][FUNCTION_EXPORTED] = 1;
        let mut signatures = [[0; SIGNATURE_WORDS]];
        signatures[0][SIGNATURE_RESULT_TYPE] = I64;
        let mut blocks = [[0; BLOCK_WORDS]];
        blocks[0][BLOCK_INSTRUCTION_COUNT] = 2;
        let mut values = [[0; VALUE_WORDS]];
        values[0][VALUE_TYPE] = I64;
        values[0][VALUE_DEFINITION] = 1;
        let mut instructions = [[0; INSTRUCTION_WORDS]; 2];
        instructions[0][INSTRUCTION_OP] = CONSTANT_I64;
        instructions[0][INSTRUCTION_RESULT] = 1;
        instructions[0][INSTRUCTION_PAYLOAD] = 1;
        instructions[0][INSTRUCTION_ORIGIN] = 1;
        instructions[1][INSTRUCTION_OP] = RETURN;
        instructions[1][INSTRUCTION_LEFT] = 1;
        instructions[1][INSTRUCTION_ORIGIN] = 1;
        let mut origins = [[0; ORIGIN_WORDS]];
        origins[0][ORIGIN_END] = 1;
        let name = b"CleanupProbe";
        let mut names = [[0; NAME_WORDS]];
        names[0][NAME_COUNT] = name.len() as u32;
        let arena = Arena {
            functions: &functions, foreigns: &[], signatures: &signatures, parameter_types: &[],
            blocks: &blocks, block_parameters: &[], values: &values, instructions: &instructions,
            edges: &[], calls: &[], arguments: &[], origins: &origins, names: &names,
            literals: &[7], name_bytes: name,
        };
        assert!(std::env::var_os("PERF_BUILDID_DIR").is_none(), "test requires profiling disabled");
        let trace = Arc::new(Mutex::new(AllocationTrace::default()));
        let mut builder = JITBuilder::with_isa(native_isa().unwrap(), default_libcall_names());
        builder.memory_provider(Box::new(FailAfterAllocation {
            inner: SystemMemoryProvider::new(), trace: trace.clone(),
        }));
        let result: Result<(), Failure> = (|| {
            let mut owner = JitOwner(Some(JITModule::new(builder)));
            define_bodies(owner.module(), &arena, &borrow_names(&arena))?;
            owner.module().finalize_definitions().map_err(|error| module_failure(error, "test finalization"))?;
            Ok(())
        })();
        assert!(matches!(result, Err(Failure { status: Status::BackendFailure, .. })));
        let maps = std::fs::read_to_string("/proc/self/maps").unwrap();
        let trace = trace.lock().unwrap();
        assert!(trace.code != 0 && trace.current != 0 && trace.code != trace.current);
        assert!(trace.code_was_executable && trace.current_was_writable);
        assert_eq!(trace.explicit_frees, 1);
        assert!(trace.code_released && trace.current_waited_for_drop);
        assert!(permissions(&maps, trace.code).is_none());
        assert!(permissions(&maps, trace.current).is_none());
    }
}
