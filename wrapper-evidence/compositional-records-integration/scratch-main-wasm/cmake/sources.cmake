# The root owns only its host build tool. Runtime/compiler/desktop source lists
# belong to sdk/*.cmake; Studio and the controller own their consumer targets.
set(CHIBI_REFLECT_SOURCES
    tools/reflect/reflect.c tools/reflect/collect.c tools/reflect/identity.c
    tools/reflect/emit.c tools/reflect/emit_function.c tools/reflect/emit_component.c
    tools/reflect/emit_catalog.c)
