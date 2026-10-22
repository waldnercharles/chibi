# Distributing Chibi and applications

See the [file license map](../LICENSE.md), [GPLv3](../COPYING), and
[LGPLv3](../COPYING.LESSER). This guide does not change those licenses.

## Source and notices

When distributing covered Chibi binaries, provide the corresponding Chibi
source, including your distributed changes and required build material,
using a method allowed by the applicable license. Providing only a link
to an unrelated or different upstream version is not a substitute for the
source corresponding to the binary you distribute.

Keep copyright notices and license texts. An application combined with
LGPL Chibi components needs the notices and both GNU license texts required
by LGPL section 4. Preserve third-party notices and satisfy their separate
terms as well. Existing vendored code, fonts, and assets are not relicensed
by Chibi's first-party license map.

## Proprietary applications

The LGPL allows an independent application to use the Chibi library under
its own terms, including proprietary terms, provided the LGPL conditions
are met. Distributed modifications to Chibi itself do not become
proprietary merely because the surrounding application is proprietary.

For a combined work, the applicable terms must permit modification of the
Chibi portions and reverse engineering for debugging those modifications.
LGPL section 4 also requires one of its relinking/replacement arrangements:

- With static linking, supply the required Chibi source and Corresponding
  Application Code in a usable form that permits recombining or relinking
  with a modified Chibi version. Application object files can satisfy this
  role without disclosing proprietary application source, but they must
  actually be usable with the required build inputs and instructions.
- Alternatively, use a suitable shared-library mechanism that works with
  a modified interface-compatible library, as described in section 4d1.
  Bundling an unreplaceable library is not automatically sufficient.

Applicable Installation Information requirements also remain in force.
Native, browser, WebAssembly, and other export formats are not categorical
exceptions. A distribution of Chibi libraries without a combined application
has its own corresponding-source obligations under the license.

## Generated code and core packages

Reflection wrappers, generated execution scaffolding, copied headers, and
compiled core packages can contain Chibi implementation code. Preserve the
license of those portions and the source/build material needed to modify
and rebuild them. Do not assume that generated C or Wasm is wholly
application-owned, or that the header permission in LGPL section 3 covers
arbitrary generated implementation code.

Compiling an independently authored application does not automatically
make its own code GPL. A package that also redistributes the compiler or
other GPL tools must satisfy their GPL requirements separately. The GPL
compiler bootstrap is a compiler artifact, not a required game runtime.

## Copied examples

The three explicitly enumerated MIT snippets in [the file map](../LICENSE.md#mit-copy-paste-examples)
may be copied and modified under [the MIT license](../COPYING.EXAMPLES), including
in proprietary code, while retaining its required notices. This does not change
the terms for Chibi runtime components used by an application.

Other first-party examples in the documentation and tests are LGPL under the
file map, not permissively licensed starter projects. If you copy or adapt them,
their Chibi-origin portions retain LGPL obligations, including applicable
source and recombination requirements. Writing an independent application
using the language and library interfaces is a different case. The MIT list is
exhaustive; it does not include tests, assets, third-party material, generated runtime scaffolding, or other templates.

## Export and packaging status

Adding these licenses does not certify every existing SDK, player archive,
or exported application as a compliant distribution. Before distributing:

1. Identify the exact Chibi and third-party versions and local modifications.
2. Include applicable notices and the complete license texts.
3. Provide the matching source and required build material through an allowed
   distribution method.
4. For a combined application, verify a working relink/replacement path and
   any required installation information for the actual native or web layout.
5. Check that application terms do not prohibit the required user activities.

Current export/package helpers do not by themselves establish all of these
conditions. License installation, corresponding-source delivery and verified
relink/replacement bundles need explicit validation before binary release.
Do not infer compliance from a successful build or the presence of a license
file alone. Older artifacts and releases require their own assessment.

## References

- [GNU LGPLv3, sections 3 and 4](https://www.gnu.org/licenses/lgpl-3.0.html)
- [GNU FAQ: static and dynamic LGPL linking](https://www.gnu.org/licenses/gpl-faq.en.html#LGPLStaticVsDynamic)
- [GNU FAQ: compiler output](https://www.gnu.org/licenses/gpl-faq.en.html#WhatCaseIsOutputGPL)
- [GNU FAQ: interpreters and bindings](https://www.gnu.org/licenses/gpl-faq.en.html#IfInterpreterIsGPL)
