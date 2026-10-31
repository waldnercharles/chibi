# Component nodes

Behavior graphs read, write, add and remove reflected components with **Get Component**, **Set Component**, **Has Component**, **Add Component** and **Remove Component**, and build or split reflected structs with **Make Struct** and **Break Struct**. These seven nodes replace the generated per-component and per-struct nodes in the add-node catalog. The generated descriptors remain, and every component node lowers to them before anything evaluates, compiles or debugs the graph.

The implementation is in `graph/nodes/component_nodes.{h,c}`.

## The nodes

| Node | Inputs | Outputs |
| --- | --- | --- |
| Get Component | Component, Entity | Value, plus the fields it exposes |
| Set Component | Exec, Component, Entity, Value or field inputs | Then, Value |
| Has Component | Component, Entity | Value (bool) |
| Add Component, Remove Component | Exec, Component | Then, Value (bool) |
| Make Struct | the struct's fields, Type | Value, plus the fields it exposes |
| Break Struct | Value, Type | the struct's fields |

- **Component** is a setting (`PIN_FLAG_SETTING`, `PIN_NAME`) that holds the component's stable key. Setting it with `graph_set_pin_default` retypes the Value pins. It also drops field pins, plus any wires the new type can't carry. Tools may pass the component name; the stored value is always the key.
- **Type** is the same kind of setting for Make and Break Struct. It holds a reflected struct's name and gives the node the pins of the generated `Make_<T>` or `Break_<T>`, followed by the setting. Add and Remove Component have the same pins for every component, so changing their Component keeps their wires.
- **Entity** on Get, Set and Has defaults to Self (`PIN_FLAG_DEFAULT_SELF`). A wire, a binding or a literal handle makes the node address that entity instead.
- **Get** shows only the fields it uses as exposed outputs beside Value (the existing Show/Hide unused fields).
- **Set** writes its whole Value, or only the field inputs it shows. `component_node_show_field` adds or removes one field input. Showing a field replaces Value, and hiding the last field restores it.
  - A field can be shown when it's top-level and has a generated setter. That excludes strings and structs that contain strings.
  - `graph_split_pin` and `graph_recombine_pin` on a component node show every settable field, or restore Value.

## Lowering

`graph_lower_component_nodes` replaces each node in place and keeps node and pin IDs. Breakpoints, watches, flow traces and preview values therefore map back to the authored node.

| Component node | Entity | Lowers to |
| --- | --- | --- |
| Get | Self | `Get_<C>` |
| Get | another entity | `GetEntity_<C>` |
| Has | Self | `Has_<C>` |
| Has | another entity | `HasEntity_<C>` |
| Set, Value | Self | `Set_<C>` |
| Set, Value | another entity | `SetEntity_<C>` |
| Set, fields f1..fn | either | `Set_<C>_f1` → … → `Set_<C>_fn` |
| Add, Remove | Self | `Add_<C>`, `Remove_<C>` |
| Make, Break Struct | — | `Make_<T>`, `Break_<T>` |

Add, Remove, Make and Break keep every pin except their setting, which is always their last input.

Field setters are chained in field order:
- The first keeps the node ID and Exec. The last keeps Then, Value and the exposed Value fields.
- Each field input keeps its pin ID on the setter that writes it.
- An Entity wire fans out to every Target.

A Set with one field is exactly the old field setter. With several fields, the setters after the first have new node IDs. Breakpoints and incoming wires map to the first setter, and outputs belong to the last. Watches find a pin by its ID when its node moved, so a multi-field Set's output watches still show.

Lowering runs where authored assets become runnable:
- At the end of `behavior_asset_link_libraries`. Game loads, scene apply, native previews and the editor's compile command all go through it.
- In editor dispatch previews, on the preview copy.
- In `eval_view`, which links an asset that holds component nodes. The graph's event status icons read the program `eval_view_graph` returns.

`behavior_asset_compile` validates a lowered copy, so authoring tools report real diagnostics, and a lowering failure such as "Choose a component for Get Component" or "Choose a type for Make Struct" becomes a compile error. `behavior_program_publish` refuses an asset that still holds component nodes, and so does instance creation on the interpreter backend.

The optimizer treats each component node like the node it lowers to:
- A Get Component that reads Self folds into bindings exactly like `Get_<C>`; a read of another entity stays a node.
- A Break Struct over such a read becomes nested field bindings, like `Break_<C>`.
- An unused Get or Has Component stays, as a generated reader does.

A library accepts a component node when it accepts the node it lowers to. Get, Set and Has also need an explicit entity there.

## Existing assets: rendered, not migrated

Old graphs keep their generated nodes, and the editor presents them in the new vocabulary. Loading doesn't migrate them. The reasons:
- A generated node is exactly what the corresponding component node lowers to, so it's valid and runs unchanged. Migrating on load would have pushed every loaded asset, test fixture and authored graph through lowering for no runtime difference.
- Migration would have rewritten 53 assets and every test that builds a generated node.
- The presentation is shared. `ed_node_title` names both forms `Get Transform`, `Set Velocity` or `Has Health`. A generated field setter labels its Value and Target pins with the field name and `Entity`, which is how Set Component shows a field.
- Search only creates component nodes, and so does "Convert to getter" on a binding: Get Component showing that field, instead of `Get_<C>` plus `Break_<C>`.

`component_node_raise` converts one generated node into its component node. A node whose inputs differ from its generated template, such as a renamed input, stays generated. `test_suite --component-nodes` proves the two forms interchangeable by raising every generated access, structural, Make and Break node in project content, saving and loading the result, lowering it, and comparing it with the original. `SpaceCadet/BumperKick` and `arena2d/MaterialImpactHealth` were converted this way and use the new nodes.

## Documents

The setting implies each pin's type, so documents write only the key or type, the field inputs a Set shows, and defaults:

```kdl
(array)nodes {
    - {
        id 4
        type "Get Component"
        inputs Component="Velocity"
        outputs {
            (object)Value.x
        }
    }
    - {
        id 10
        type "Set Component"
        inputs {
            Component "Transform"
            x id=18
        }
    }
    - {
        id 19
        type "Set Component"
        inputs {
            Component "Health"
            Value {
                default current=5 maximum=9
            }
        }
    }
    - {
        id 24
        type "Make Struct"
        inputs current=5 maximum=9 Type="Health"
    }
}
```

Loading prepares a component node from its key and field names before it applies pin entries, and writing compares pins with the same shapes. An unknown key fails to load, like an unknown node type.

## Search

`ed_search_matches` lists the seven nodes and leaves out the generated nodes they lower to, except in System graphs, whose compact nodes still use them. A non-empty query also offers presets:
- Get, Set, Has, Add and Remove for each component.
- Set for each settable field, such as `Set Velocity X`.
- Make and Break for each reflected struct, such as `Make Transform`.

A preset spawns the component node already configured. Its `ed_search_match.setting` and `.field` carry the preset, and wire compatibility uses the configured pin types. `component_node_presets` lists them for the editor and native `search_nodes` inspection, which reports their reflected types and configured inputs.

Browsing the catalog of a behavior graph owned by an entity, measured by `test_suite --editor-graph`:

| | Before | After |
| --- | --- | --- |
| Entries with an empty query | 1,112 | 178 |
| Whole-component Get/Set/Has variants | 350 | 3 component nodes |
| Field setters | 346 | 0 (presets on query) |
| Add/Remove variants | 116 | 2 component nodes |
| Make/Break | 129 | 2 struct nodes |

This is the test registry, which adds a few test-only nodes to the application's. For the 58 game components and 63 game structs, queries can find 761 presets: 5 per component, 345 for fields and 2 per struct.

## Editors and tools

- **Studio graph editor:**
  - It renders component nodes through `ed_search_matches`, `ed_spawn_match_at`, `ed_node_title` and the graph model.
  - Its Properties panel can offer the Component or Type setting through `ed_pin_choices`. The choices are a name catalog, so the selected item compares `editor_choice.name` with the pin's string, and the choice commits with `graph_set_pin_default`.
  - Write toggles call `component_node_show_field` and `component_node_split_value`.
- **Saved source:** edit component access in `.chibi` behavior/system source,
  or declarative KDL where the asset uses a graph. The former MCP `add_node`,
  `split_pin`, and `remove_pin` transport is retired.

Inline vector constructors in the editor still create `Make_vec2` and its relatives directly, as does Behavior Expression lowering; both render like Make Struct.
