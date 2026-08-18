#ifndef CBLUEPRINT_ANNOTATE_H
#define CBLUEPRINT_ANNOTATE_H

#define NODE_TYPE(T) struct __attribute__((annotate("node:type"))) T
#define NODE_FN __attribute__((annotate("node:fn")))

#endif
