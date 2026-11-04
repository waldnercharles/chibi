// Standalone future check: rustc --edition=2024 --test tests/arena_tests.rs.
// This is framing/view coverage, not source-to-native compiler acceptance.
#[path = "../generated/arena.rs"]
#[allow(dead_code)]
mod bindings;
use bindings::*;

#[test]
fn empty_frame_and_independent_empty_side_arrays() {
    let mut words = [0; HEADER_WORDS];
    words[0] = VERSION;
    let arena = Arena::from_parts(&words, &[], &[]).unwrap();
    assert!(arena.functions.is_empty());
    assert!(arena.values.is_empty());
    assert!(arena.literals.is_empty());
    assert!(arena.name_bytes.is_empty());
    // Side buffers have their own lengths, not hidden counts in word framing.
    let literals = [i64::MIN, i64::MAX];
    let arena = Arena::from_parts(&words, &literals, b"abc").unwrap();
    assert_eq!(arena.literals.as_ptr(), literals.as_ptr());
    assert_eq!(arena.name_bytes, b"abc");
}

#[test]
fn echo_join_layout_borrows_every_table_without_copy() {
    let counts = [2, 2, 4, 4, 5, 7, 6, 3, 2, 7, 2];
    let mut words = vec![VERSION];
    words.extend(counts);
    words.extend(0..129);
    let literals = [i64::MAX];
    let name_bytes = *b"EchoJoin";
    let arena = Arena::from_parts(&words, &literals, &name_bytes).unwrap();
    assert_eq!(words.len() * 4 + literals.len() * 8 + name_bytes.len(), 580);
    assert_eq!(arena.functions.len(), 2);
    assert_eq!(arena.instructions.len(), 6);
    assert_eq!(arena.functions[0], [0, 1, 2, 3, 4, 5]);
    assert_eq!(arena.values[0], [43, 44]);
    assert_eq!(arena.instructions[0], [57, 58, 59, 60, 61, 62]);
    assert_eq!(arena.names[1], [127, 128]);
    for (actual, offset) in [
        (arena.functions.as_ptr().cast::<u32>(), 12),
        (arena.signatures.as_ptr().cast::<u32>(), 24),
        (arena.parameter_types.as_ptr(), 30),
        (arena.blocks.as_ptr().cast::<u32>(), 34),
        (arena.block_parameters.as_ptr(), 50),
        (arena.values.as_ptr().cast::<u32>(), 55),
        (arena.instructions.as_ptr().cast::<u32>(), 69),
        (arena.edges.as_ptr().cast::<u32>(), 105),
        (arena.arguments.as_ptr(), 114),
        (arena.origins.as_ptr().cast::<u32>(), 116),
        (arena.names.as_ptr().cast::<u32>(), 137),
    ] {
        assert_eq!(actual, words[offset..].as_ptr());
    }
    assert_eq!(arena.literals.as_ptr(), literals.as_ptr());
    assert_eq!(arena.name_bytes.as_ptr(), name_bytes.as_ptr());
}

#[test]
fn rejects_missing_header_and_unknown_version() {
    assert_eq!(Arena::from_parts(&[], &[], &[]).unwrap_err(), FramingError::HeaderTooShort);
    let words = [0; HEADER_WORDS];
    assert_eq!(Arena::from_parts(&words, &[], &[]).unwrap_err(), FramingError::Version { actual: 0 });
}

#[test]
fn bounded_malformed_replay_covers_every_count_position() {
    // This v1 layout oracle is test-only. Large counts use a real short header,
    // never fabricated slices or allocations proportional to rejected counts.
    let tables = [
        ("functions", FUNCTION_WORDS), ("signatures", SIGNATURE_WORDS),
        ("parameter_types", 1), ("blocks", BLOCK_WORDS),
        ("block_parameters", 1), ("values", VALUE_WORDS),
        ("instructions", INSTRUCTION_WORDS), ("edges", EDGE_WORDS),
        ("arguments", 1), ("origins", ORIGIN_WORDS), ("names", NAME_WORDS),
    ];
    assert_eq!(tables.len() + 1, HEADER_WORDS);
    for (position, (buffer, width)) in tables.into_iter().enumerate() {
        for count in [0, 1, MAX_ELEMENTS as u32, MAX_ELEMENTS as u32 + 1] {
            let mut words = vec![0; HEADER_WORDS];
            words[0] = VERSION;
            words[position + 1] = count;
            if count > MAX_ELEMENTS as u32 {
                assert_eq!(Arena::from_parts(&words, &[], &[]).unwrap_err(),
                           FramingError::CountTooLarge { buffer });
                continue;
            }
            let expected = (count as usize).checked_mul(width)
                .and_then(|payload| HEADER_WORDS.checked_add(payload));
            if count > 1 {
                let error = Arena::from_parts(&words, &[], &[]).unwrap_err();
                match expected {
                    Some(expected) => assert_eq!(error, FramingError::WordCount {
                        expected, actual: HEADER_WORDS,
                    }),
                    None => assert_eq!(error, FramingError::SizeOverflow),
                }
                continue;
            }
            let expected = expected.unwrap();
            words.resize(expected, 0);
            // Framing success says nothing about these zero rows' semantics.
            assert!(Arena::from_parts(&words, &[], &[]).is_ok());
            for actual in 0..words.len() {
                let error = Arena::from_parts(&words[..actual], &[], &[]).unwrap_err();
                let expected_error = if actual < HEADER_WORDS { FramingError::HeaderTooShort }
                    else { FramingError::WordCount { expected, actual } };
                assert_eq!(error, expected_error);
            }
            words.push(0);
            assert_eq!(Arena::from_parts(&words, &[], &[]).unwrap_err(),
                       FramingError::WordCount { expected, actual: expected + 1 });
        }
    }
}

#[test]
fn canonical_empty_and_checked_nonempty_spans() {
    assert_eq!(span(0, 0, 0), Ok(0..0));
    assert_eq!(span(0, 0, 10), Ok(0..0));
    assert_eq!(span(1, 0, 10), Err(FramingError::InvalidSpan));
    assert_eq!(span(10, 0, 10), Err(FramingError::InvalidSpan));
    assert_eq!(span(0, 10, 10), Ok(0..10));
    assert_eq!(span(9, 1, 10), Ok(9..10));
    assert_eq!(span(10, 1, 10), Err(FramingError::InvalidSpan));
    assert_eq!(span(u32::MAX, u32::MAX, 10), Err(FramingError::InvalidSpan));
}

#[test]
fn generated_kinds_have_checked_exhaustive_decoding() {
    for value in [BOOL, I64] {
        match ScalarType::decode(value).unwrap() {
            ScalarType::Bool => assert_eq!(value, BOOL),
            ScalarType::I64 => assert_eq!(value, I64),
        }
    }
    for value in [CONSTANT_I64, ADD_I64, JUMP, BRANCH, RETURN] {
        match Opcode::decode(value).unwrap() {
            Opcode::ConstantI64 => assert_eq!(value, CONSTANT_I64),
            Opcode::AddI64 => assert_eq!(value, ADD_I64),
            Opcode::Jump => assert_eq!(value, JUMP),
            Opcode::Branch => assert_eq!(value, BRANCH),
            Opcode::Return => assert_eq!(value, RETURN),
        }
    }
    assert_eq!(ScalarType::decode(0), None);
    assert_eq!(Opcode::decode(0), None);
    assert_eq!(Opcode::decode(BLOCK_DEFINITION), None);
}
