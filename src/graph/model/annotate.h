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
#define NODE_TYPE_ALIAS(T) __attribute__((annotate("node:type-alias:" #T)))
#define NODE_COMPONENT_ALIAS(T)                                                \
    __attribute__((annotate("node:component-alias:" #T)))
#define NODE_EXTERNAL_TYPE(T)                                                  \
    __attribute__((annotate("node:external-type:" #T)))
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
#define NODE_JIT_FAST_MATH __attribute__((annotate("node:jit-fast-math")))
#define NODE_EXEC __attribute__((annotate("node:exec")))
#define NODE_OUT __attribute__((annotate("node:out")))
#define NODE_NAME(NAME) __attribute__((annotate("node:name:" NAME)))
#define NODE_CATEGORY(NAME) __attribute__((annotate("node:category:" NAME)))
#define NODE_KEYWORDS(KEYWORDS)                                                \
    __attribute__((annotate("node:keywords:" KEYWORDS)))
#define NODE_PIN_NAME(NAME) __attribute__((annotate("node:pin-name:" NAME)))
#define NODE_SERVICE(T) __attribute__((annotate("node:service:" #T)))
/* Nominal graph handle stored in a uint64_t component field. */
#define NODE_HANDLE(T) __attribute__((annotate("node:handle:" #T)))
#define NODE_OPERATOR(NAME) __attribute__((annotate("node:operator:" NAME)))
#define NODE_READS(T) __attribute__((annotate("node:reads:" #T)))
#define NODE_WRITES(T) __attribute__((annotate("node:writes:" #T)))
#define NODE_ENTITY_SCOPED __attribute__((annotate("node:entity-scoped")))
#define NODE_STRUCTURAL __attribute__((annotate("node:structural")))
#define NODE_WORLD_READ __attribute__((annotate("node:world-read")))
#define NODE_HOST_SINK __attribute__((annotate("node:host-sink")))

#endif
