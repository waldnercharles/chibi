// SPDX-License-Identifier: GPL-3.0-only
//! Same-process typed-frame backend. See README.md for the C caller contract.
//! No input borrow survives compile_both; only complete output is published.
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
    for function in a.functions {
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
        let linkage = if function[FUNCTION_EXPORTED] == 1 { Linkage::Export } else { Linkage::Local };
        let name = names[index(function[FUNCTION_NAME], names.len())];
        let id = module.declare_function(name, linkage, signature)
            .expect("verified typed function declaration");
        declarations.push(id);
        module.clear_signature(&mut context.func.signature);
    }

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
        let signature = &module.declarations().get_function_decl(id).signature;
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

fn compile(a: &Arena<'_>) -> Result<Compiled, Failure> {
    // Upstream perf-map creation uses unwrap. This bounded compiler admits
    // no perf-map side effects; return an owned environment error before JIT.
    if std::env::var_os("PERF_BUILDID_DIR").is_some() {
        return Err(Failure { status: Status::BackendFailure,
            message: "PERF_BUILDID_DIR profiling is unsupported by this backend".into() });
    }
    let names = borrow_names(a);
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
    let mut jit = JitOwner(Some(JITModule::new(JITBuilder::with_isa(isa, default_libcall_names()))));
    let functions = define_bodies(jit.module(), a, &names)?;
    jit.module().finalize_definitions().map_err(|error| module_failure(error, "JIT finalization"))?;
    let entries = functions.iter().zip(a.functions).map(|(&id, source)| {
        if source[FUNCTION_EXPORTED] == 1 { jit.module().get_finalized_function(id) }
        else { ptr::null() }
    }).collect();
    Ok(Compiled { _jit: jit, entries, object })
}

// Called for three scalar input types. This checks only what raw pointers can
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

#[unsafe(no_mangle)]
pub unsafe extern "C" fn chibi_vnext_compile_both(
    words: *const u32, word_count: u64, literals: *const i64, literal_count: u64,
    names: *const u8, name_byte_count: u64,
) -> *mut c_void {
    // Check every pointer/count pair before constructing any input slice.
    let result = (|| {
        let word_count = input_length(words, word_count)?;
        let literal_count = input_length(literals, literal_count)?;
        let name_byte_count = input_length(names, name_byte_count)?;
        // SAFETY: counts were checked; remaining validity is the caller contract.
        let (words, literals, names) = unsafe {
            (input_slice(words, word_count), input_slice(literals, literal_count), input_slice(names, name_byte_count))
        };
        let arena = Arena::from_parts(words, literals, names).map_err(|error| Failure {
            status: Status::InvalidFrame, message: format!("typed frame: {error:?}"),
        })?;
        compile(&arena)
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
                chibi_vnext_compile_both(words, word_count, literals, literal_count, names, name_count)
            };
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
                assert!(str::from_utf8(&destination).is_ok());
                let closed = chibi_vnext_destroy(handle);
                assert_eq!(closed, 0);
            }
        }
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
            functions: &functions, signatures: &signatures, parameter_types: &[],
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
