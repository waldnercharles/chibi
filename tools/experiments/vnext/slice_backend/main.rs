// SPDX-License-Identifier: GPL-3.0-only
use cranelift_codegen::ir::{types, AbiParam, InstBuilder, Value};
use cranelift_codegen::isa::OwnedTargetIsa;
use cranelift_codegen::settings::{self, Configurable};
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext};
use cranelift_jit::{JITBuilder, JITModule};
use cranelift_module::{default_libcall_names, FuncId, Linkage, Module};
use cranelift_object::{ObjectBuilder, ObjectModule};
use std::error::Error;
use std::io::Write;

#[derive(Clone, Copy, Debug, PartialEq)]
enum Scalar { I64, Bool }

// Indices below are internal to this tiny hand-authored probe, not a wire ABI.
#[derive(Clone, Copy)]
enum Operation { AddOne { result: usize, input: usize } }
#[derive(Clone, Copy)]
enum Terminator {
    Return(usize),
    Branch { condition: usize, yes: usize, no: usize },
    Jump { block: usize, argument: usize },
}
struct Block {
    parameters: &'static [usize],
    operations: &'static [Operation],
    terminator: Terminator,
}
struct TypedBody {
    name: &'static str,
    values: &'static [Scalar],
    result: Scalar,
    blocks: &'static [Block],
}

static BODIES: &[TypedBody] = &[
    TypedBody {
        name: "slice_echo", values: &[Scalar::I64], result: Scalar::I64,
        blocks: &[Block { parameters: &[0], operations: &[], terminator: Terminator::Return(0) }],
    },
    TypedBody {
        name: "slice_join",
        values: &[Scalar::Bool, Scalar::I64, Scalar::I64, Scalar::I64, Scalar::I64],
        result: Scalar::I64,
        blocks: &[
            Block { parameters: &[0, 1, 2], operations: &[],
                    terminator: Terminator::Branch { condition: 0, yes: 1, no: 2 } },
            Block { parameters: &[], operations: &[], terminator: Terminator::Jump { block: 3, argument: 1 } },
            Block { parameters: &[], operations: &[], terminator: Terminator::Jump { block: 3, argument: 2 } },
            Block { parameters: &[3], operations: &[Operation::AddOne { result: 4, input: 3 }],
                    terminator: Terminator::Return(4) },
        ],
    },
];

fn scalar_type(scalar: Scalar) -> cranelift_codegen::ir::Type {
    match scalar { Scalar::I64 => types::I64, Scalar::Bool => types::I8 }
}

fn native_isa() -> Result<OwnedTargetIsa, Box<dyn Error>> {
    if std::env::consts::OS != "linux" || std::env::consts::ARCH != "x86_64" {
        return Err("this probe admits only Linux x86-64 System V".into());
    }
    let mut flags = settings::builder();
    flags.set("opt_level", "none")?;
    flags.set("is_pic", "true")?;
    Ok(cranelift_native::builder().map_err(std::io::Error::other)?
        .finish(settings::Flags::new(flags))?)
}

// Two real consumers: JITModule and ObjectModule. Both read these same bodies.
fn define_bodies<M: Module>(module: &mut M) -> Result<Vec<FuncId>, Box<dyn Error>> {
    let mut declarations = Vec::with_capacity(BODIES.len());
    for body in BODIES {
        let mut signature = module.make_signature();
        for &parameter in body.blocks[0].parameters {
            let scalar = body.values[parameter];
            let mut abi = AbiParam::new(scalar_type(scalar));
            // Explicit target C bool convention, checked by the linked C caller.
            if scalar == Scalar::Bool { abi = abi.uext(); }
            signature.params.push(abi);
        }
        signature.returns.push(AbiParam::new(scalar_type(body.result)));
        let id = module.declare_function(body.name, Linkage::Export, &signature)?;
        declarations.push((id, signature));
    }
    let mut context = module.make_context();
    let mut builder_context = FunctionBuilderContext::new();
    for (body, (id, signature)) in BODIES.iter().zip(&declarations) {
        context.func.signature = signature.clone();
        let mut builder = FunctionBuilder::new(&mut context.func, &mut builder_context);
        let blocks: Vec<_> = body.blocks.iter().map(|_| builder.create_block()).collect();
        let mut values: Vec<Option<Value>> = vec![None; body.values.len()];
        for (source, &block) in body.blocks.iter().zip(&blocks) {
            for &parameter in source.parameters {
                let value = builder.append_block_param(block, scalar_type(body.values[parameter]));
                let previous = values[parameter].replace(value);
                assert!(previous.is_none(), "duplicate typed value definition");
            }
        }
        for (source, &block) in body.blocks.iter().zip(&blocks) {
            builder.switch_to_block(block);
            for operation in source.operations {
                match *operation {
                    Operation::AddOne { result, input } => {
                        assert_eq!(body.values[result], Scalar::I64);
                        assert_eq!(body.values[input], Scalar::I64);
                        let value = builder.ins().iadd_imm_s(values[input].expect("undefined operand"), 1);
                        let previous = values[result].replace(value);
                        assert!(previous.is_none(), "duplicate typed value definition");
                    }
                }
            }
            match source.terminator {
                Terminator::Return(value) => {
                    assert_eq!(body.values[value], body.result);
                    builder.ins().return_(&[values[value].expect("undefined result")]);
                }
                Terminator::Branch { condition, yes, no } => {
                    assert_eq!(body.values[condition], Scalar::Bool);
                    assert!(body.blocks[yes].parameters.is_empty() && body.blocks[no].parameters.is_empty());
                    builder.ins().brif(values[condition].expect("undefined condition"), blocks[yes], &[], blocks[no], &[]);
                }
                Terminator::Jump { block, argument } => {
                    assert_eq!(body.blocks[block].parameters.len(), 1);
                    assert_eq!(body.values[body.blocks[block].parameters[0]], body.values[argument]);
                    builder.ins().jump(blocks[block], &[values[argument].expect("undefined edge argument").into()]);
                }
            }
        }
        builder.seal_all_blocks();
        builder.finalize(module.target_config());
        cranelift_codegen::verify_function(&context.func, module.isa())
            .map_err(|error| format!("{}: {error}", body.name))?;
        module.define_function(*id, &mut context)?;
        module.clear_context(&mut context);
    }
    Ok(declarations.into_iter().map(|(id, _)| id).collect())
}

fn main() -> Result<(), Box<dyn Error>> {
    let object_path = std::env::args_os().nth(1).ok_or("supply a new output object path")?;
    let mut jit = JITModule::new(JITBuilder::with_isa(native_isa()?, default_libcall_names()));
    let functions = define_bodies(&mut jit)?;
    jit.finalize_definitions()?;
    // Signatures are defined above and all bodies are finalized before any call.
    unsafe {
        let echo: extern "C" fn(i64) -> i64 = std::mem::transmute(jit.get_finalized_function(functions[0]));
        let join: extern "C" fn(bool, i64, i64) -> i64 = std::mem::transmute(jit.get_finalized_function(functions[1]));
        for value in [0, i64::MIN, i64::MAX] { assert_eq!(echo(value), value); }
        assert_eq!(join(true, 8, 20), 9);
        assert_eq!(join(false, 8, 20), 21);
        assert_eq!(join(true, i64::MAX, 0), i64::MIN);
    }
    let mut object = ObjectModule::new(ObjectBuilder::new(native_isa()?, "chibi-slice-probe", default_libcall_names())?);
    define_bodies(&mut object)?;
    let bytes = object.finish().emit()?;
    let mut output = std::fs::OpenOptions::new().write(true).create_new(true).open(&object_path)?;
    output.write_all(&bytes)?;
    // No generated function is executing and no function pointer is used again.
    unsafe { jit.free_memory(); }
    println!("{{\"jit_checks\":6,\"object_bytes\":{},\"status\":\"passed\",\"chibi_frontend\":\"not_run\"}}", bytes.len());
    Ok(())
}
