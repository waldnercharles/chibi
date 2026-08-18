/* ne_abi.h - hand-rolled C ABI over imnodes (C++).
 *
 * imnodes is compiled against cute_framework's vendored ImGui; this shim
 * exposes the parts of its API the C23 app needs, as extern "C". Minimal
 * surface for now: a proof-of-life spike. Grows as the bridge needs it.
 */
#ifndef NE_ABI_H
#define NE_ABI_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* Draws two nodes (const_f32 -> add_floats) joined by a link, in a window.
     * Proof of life for imnodes behind this ABI. */
    void ne_spike_draw(void);

#ifdef __cplusplus
}
#endif

#endif /* NE_ABI_H */
