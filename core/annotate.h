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
/* REFL-9: the contract is explicit. NODE_FN is a pure pull-evaluated
 * operation; NODE_EXEC is an ordered flow operation. */
#define NODE_FN __attribute__((annotate("node:fn")))
#define NODE_EXEC __attribute__((annotate("node:exec")))
#define NODE_OUT __attribute__((annotate("node:out")))
#define NODE_SERVICE(T) __attribute__((annotate("node:service:" #T)))
#define NODE_OPERATOR(NAME) __attribute__((annotate("node:operator:" NAME)))

#endif
