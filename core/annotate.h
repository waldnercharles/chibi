#ifndef CBLUEPRINT_ANNOTATE_H
#define CBLUEPRINT_ANNOTATE_H

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
 * operation; NODE_EXEC is an ordered flow operation. */
#define NODE_FN __attribute__((annotate("node:fn")))
#define NODE_EXEC __attribute__((annotate("node:exec")))
#define NODE_OUT __attribute__((annotate("node:out")))
#define NODE_SERVICE(T) __attribute__((annotate("node:service:" #T)))
#define NODE_OPERATOR(NAME) __attribute__((annotate("node:operator:" NAME)))
#define NODE_READS(T) __attribute__((annotate("node:reads:" #T)))
#define NODE_WRITES(T) __attribute__((annotate("node:writes:" #T)))
#define NODE_ENTITY_SCOPED __attribute__((annotate("node:entity-scoped")))
#define NODE_STRUCTURAL __attribute__((annotate("node:structural")))
#define NODE_WORLD_READ __attribute__((annotate("node:world-read")))
#define NODE_HOST_SINK __attribute__((annotate("node:host-sink")))

#endif
