#ifndef CHIBI_ANNOTATE_H
#define CHIBI_ANNOTATE_H

#define NODE_TYPE(T) struct __attribute__((annotate("node:type"))) T
#define NODE_COMPONENT(T) struct __attribute__((annotate("node:component"))) T
#define NODE_COMPONENT_DENSE(T)                                                \
    struct __attribute__((annotate("node:component:index:dense"))) T
#define NODE_COMPONENT_TYPE(T)                                                 \
    struct __attribute__((annotate("node:type")))                              \
    __attribute__((annotate("node:component"))) T
#define NODE_COMPONENT_TYPE_DENSE(T)                                           \
    struct __attribute__((annotate("node:type")))                              \
    __attribute__((annotate("node:component:index:dense"))) T
/* A dense component that engine systems compute from SOURCE. Adding SOURCE
 * adds it; scripts read it, and authored assets and saves leave it out. */
#define NODE_DERIVED_COMPONENT(T, SOURCE)                                      \
    struct __attribute__((annotate("node:type")))                              \
    __attribute__((annotate("node:component:index:dense")))                    \
    __attribute__((annotate("node:derived-from:" #SOURCE))) T
/* Explicit authored spelling for the generated aggregate constructor. */
#define NODE_CONSTRUCTOR(NAME)                                                 \
    __attribute__((annotate("node:constructor:" NAME)))
#define NODE_TYPE_ALIAS(T) __attribute__((annotate("node:type-alias:" #T)))
#define NODE_COMPONENT_ALIAS(T)                                                \
    __attribute__((annotate("node:component-alias:" #T)))
#define NODE_EXTERNAL_TYPE(T)                                                  \
    __attribute__((annotate("node:external-type:" #T)))
/* The authored name of an external type whose C name scripts don't spell. */
#define NODE_TYPE_NAME(NAME) __attribute__((annotate("node:type-name:" NAME)))
#define NODE_CONVERTS(FROM, TO)                                                \
    __attribute__((annotate("node:conversion-from:" #FROM)))                   \
    __attribute__((annotate("node:conversion-to:" #TO)))
/* REFL-9: the contract is explicit. NODE_FN is a pure pull-evaluated
 * operation; NODE_READ is a fallible pull-evaluated operation; NODE_EXEC is
 * an ordered flow operation. */
#define NODE_FN __attribute__((annotate("node:fn")))
/* Native read functions return an error string and expose data via NODE_OUT. */
#define NODE_READ __attribute__((annotate("node:read")))
#define NODE_JIT(NAME) __attribute__((annotate("node:jit:" NAME)))
#define NODE_EXEC __attribute__((annotate("node:exec")))
#define NODE_OUT __attribute__((annotate("node:out")))
#define NODE_NAME(NAME) __attribute__((annotate("node:name:" NAME)))
#define NODE_CATEGORY(NAME) __attribute__((annotate("node:category:" NAME)))
#define NODE_KEYWORDS(KEYWORDS)                                                \
    __attribute__((annotate("node:keywords:" KEYWORDS)))
#define NODE_DESCRIPTION(TEXT)                                                 \
    __attribute__((annotate("node:description:" TEXT)))
#define NODE_PIN_NAME(NAME) __attribute__((annotate("node:pin-name:" NAME)))
#define NODE_SERVICE(T) __attribute__((annotate("node:service:" #T)))
/* Packed RGBA color stored in an unsigned 32-bit field. */
#define NODE_COLOR __attribute__((annotate("node:color")))

/* Nominal graph handle stored in a uint64_t component field. */
#define NODE_HANDLE(T) __attribute__((annotate("node:handle:" #T)))

/* Unit an editor shows after a numeric field's value, such as "u/s". */
#define NODE_UNIT(TEXT) __attribute__((annotate("node:unit:" TEXT)))
/* Inclusive stored-value contract. Owning writes clamp to these endpoints. */
#define NODE_RANGE(MIN, MAX)                                                   \
    __attribute__((annotate("node:range:" #MIN " " #MAX)))
/* Slider suggestion for fields whose valid domain extends beyond it. */
#define NODE_RANGE_HINT(MIN, MAX)                                              \
    __attribute__((annotate("node:range-hint:" #MIN " " #MAX)))
/* Finite fallback for NaN; omitted means zero clamped into the range. */
#define NODE_RANGE_DEFAULT(VALUE)                                              \
    __attribute__((annotate("node:range-default:" #VALUE)))

#endif
