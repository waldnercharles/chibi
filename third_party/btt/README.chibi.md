# Beat and Tempo Tracking

Upstream: https://github.com/michaelkrzyzaniak/Beat-and-Tempo-Tracking

Imported revision: `c039090f1af771092d95c3ffc402e557940f7384`

License: MIT, retained in `LICENSE`.

This directory contains the public header and active C core only. Demos, audio files, obsolete DFT sources, and platform integrations are excluded. The upstream FFT and its sine table remain the sole FFT implementation used by this library. The STFT window retains the upstream coefficients: the function named `dft_init_blackman_window` actually produces a periodic Hann window.

## Local patches

- Track consumed samples in STFT, timestamp callbacks at the exact end of each hop, and expose the complete consumed count through `btt_get_num_audio_samples_processed`. Processing block size no longer changes event timestamps. The count includes a partial hop after `btt_process` returns.
- Calculate latency-adjusted event times with signed arithmetic and clamp negative startup times to zero. Preserve 64-bit count-in intervals and beat-suppression deadlines instead of narrowing them to `int`.
- Make `btt_clear` clear STFT input, FIR and threshold histories, previous spectrum, correlation and prediction buffers, indices, clocks, and suppression state. It does not allocate or change configuration or callbacks. `btt_init_tempo` also resets prediction indices and suppression state.
- Correct the packed imaginary-bin index in generalized autocorrelation. Raise absolute DC and Nyquist magnitudes to the configured exponent so negative real endpoints cannot produce NaN with the default fractional exponent.
- Make the constructor header argument order match its implementation: onset-threshold length precedes CBSS length. Add assertions for the constructor's rate, transform sizes, overlap, and buffer-size preconditions.
- Return callback context before returning from the callback getters. Allow metronome processing without a registered callback and report its configured tempo without requiring an estimated tempo first.
- Add `btt_set_sample_rate`, retaining requested tempo limits and metronome BPM while recomputing derived lag, tempo-weight, and filter coefficients, then clearing stream state without allocating. Clamp derived lags to valid buffer indices. Mark BTT's internal processing functions private.
- Add an optional read-only magnitude-spectrum observer after the existing FFT and magnitude conversion, before DC removal, compression, noise cancellation, and amplitude normalization. It reuses the current transform without allocating. DC and Nyquist are made nonnegative during the callback and restored before tracking continues.

## Host contract

The host supplies mono float PCM and serializes access to each tracker. Callbacks run synchronously on the processing thread. They receive an estimated sample position in the stream since its last clear, with onset latency and beat prediction applied. A predicted beat may lie beyond the currently consumed sample count. The host owns playback-clock conversion and event delivery to another thread.

`btt_set_sample_rate(BTT *self, double sample_rate)` accepts finite rates from `BTT_MIN_SAMPLE_RATE` through `BTT_MAX_SAMPLE_RATE`, currently 8000 through 192000 Hz. It resets even when the rate is unchanged. It retains each setting in that setting's documented units. In particular, explicit audio-sample and OSS-sample latency/prediction controls remain sample counts; the host must set rate-appropriate latency offsets. Upstream requires empirical calibration for custom rates and configurations. Sample-based history sizes remain fixed, so the available tempo range and warmup duration vary with the rate. With the default 1024-frame history and 128-sample hop, the minimum representable tempo at 192 kHz is about 87.9 BPM. Returning to a lower rate restores the requested minimum tempo rather than the previously clamped value.

`unsigned long long btt_get_num_audio_samples_processed(BTT *self)` returns the consumed mono-sample count since clear. During a callback, this is the current hop's frame-end count. After processing returns, it includes every sample in that call, including the unfinished hop.

The tempo certainty getter remains the upstream histogram score. It is not a calibrated probability and can retain its last value through silence. The host must track signal activity and estimate validity separately.

`btt_set_spectrum_callback(BTT *self, btt_spectrum_callback_t callback, void *context)` registers an optional observer. It receives `(context, const dft_sample_t *magnitudes, int bins, unsigned long long sample_time)` synchronously for each STFT hop. The array contains `N/2+1` nonnegative, unnormalized magnitudes from DC through Nyquist. Bin `i` represents `i * sample_rate / N` Hz. These values use the upstream periodic Hann window and precede BTT's spectral compression, thresholding, and normalization. The timestamp is the exact frame-end count since clear, with no onset latency or beat prediction applied.

The observer may read the array only for the duration of the callback. It must not modify the array, retain its pointer, process more audio, or change tracker state. Read-only getters are safe during the callback. Register a null callback to disable it. `btt_clear` and `btt_set_sample_rate` retain the observer and its context; `btt_init` removes it along with the other callbacks.

## Upstream references

- [Public API and defaults](https://github.com/michaelkrzyzaniak/Beat-and-Tempo-Tracking/blob/c039090f1af771092d95c3ffc402e557940f7384/BTT.h)
- [Tracker implementation](https://github.com/michaelkrzyzaniak/Beat-and-Tempo-Tracking/blob/c039090f1af771092d95c3ffc402e557940f7384/src/BTT.c)
- [STFT implementation](https://github.com/michaelkrzyzaniak/Beat-and-Tempo-Tracking/blob/c039090f1af771092d95c3ffc402e557940f7384/src/STFT.c)
- [FFT, autocorrelation, and window implementation](https://github.com/michaelkrzyzaniak/Beat-and-Tempo-Tracking/blob/c039090f1af771092d95c3ffc402e557940f7384/src/DFT.c)
