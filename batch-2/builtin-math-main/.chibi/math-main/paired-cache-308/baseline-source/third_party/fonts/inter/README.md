# Inter

Unmodified Inter variable font from [Google Fonts](https://github.com/google/fonts/tree/main/ofl/inter), downloaded September 5, 2026. The upstream filename is `Inter[opsz,wght].ttf`.

Distributed under the SIL Open Font License in OFL.txt. Embedded in the editor so text rendering does not depend on installed system fonts.

The editor embeds static 500, 600, and 700 weight faces for UI backends that
cannot select variable-font axes. `Inter-Medium.ttf`, `Inter-SemiBold.ttf`, and
`Inter-Bold.ttf` are instances of the variable source at optical size 14,
materialized with fontTools 4.66.0. They remain under the same OFL terms.

`Inter-Numeric.ttf` is a static 400-weight, optical-size-14 face for numeric
fields. Its ASCII digit cmap entries point directly to Inter's existing `.tf`
tabular numeral outlines because RmlUi's FreeType backend does not apply the
OpenType `tnum` feature. All ten digits have the same 1328-unit advance.
