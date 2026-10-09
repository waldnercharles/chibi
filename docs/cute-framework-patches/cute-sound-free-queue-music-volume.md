# Free queued sounds safely and apply the music volume once

`cute-sound-free-queue-music-volume.patch` fixes two mixer bugs in `cute_sound.h`. Chibi's playback host in `src/render/audio.c` plays sample sounds, synthesized tones and looping music, and scripts set the music bus volume.

## What changes

- `cs_free_audio_source` queues a source that still has playing voices. When that queue fills, it grows to `new_capacity` bytes instead of `new_capacity` pointers, then copies the pointers into it. More than 32 queued sources overflow the heap. The patch allocates pointers.
- The mixer already scales each music instance by `music_volume`. The fade states and `cs_music_set_volume` also wrote `music_volume` into the instance volume, so a set volume was squared. Music started without a fade kept an instance volume of 1, so the same setting played at a different level depending on how the track started. After the patch, the instance volume is only the fade envelope, from 0 to 1.

## Upstream status

Both bugs remain at pinned upstream commit `96e6034aa6bc44665856ee480962a9ddab0b87cc`. See [the free queue](https://github.com/RandyGaul/cute_framework/blob/96e6034aa6bc44665856ee480962a9ddab0b87cc/libraries/cute/cute_sound.h#L1792-L1800), [the fade states](https://github.com/RandyGaul/cute_framework/blob/96e6034aa6bc44665856ee480962a9ddab0b87cc/libraries/cute/cute_sound.h#L1091-L1150), [the mixer's music scale](https://github.com/RandyGaul/cute_framework/blob/96e6034aa6bc44665856ee480962a9ddab0b87cc/libraries/cute/cute_sound.h#L1184-L1186) and [`cs_music_set_volume`](https://github.com/RandyGaul/cute_framework/blob/96e6034aa6bc44665856ee480962a9ddab0b87cc/libraries/cute/cute_sound.h#L2127-L2133).

## Example

A reset stops every voice and destroys each tone's source in the same frame. The mixer has not yet removed those voices, so every destroy is queued:

```c
for (int i = 0; i < 64; i++) {
    cf_sound_stop(voices[i]);
    cf_audio_destroy(tones[i]); /* the 33rd queued source overflowed */
}
```

`cf_music_set_volume(0.5f)` now plays music at half volume rather than a quarter.

## Limits and validation

A one-off manual check, not an admitted test, played a 220 Hz OGG loop with `set_music_volume(0.5)` through SDL's disk audio driver. The recorded peak matched half the source amplitude times the centered pan gain. The overflow case was found by reading the code and was not reproduced under a sanitizer.

## Apply the patch

This diff targets Cute Framework commit [`96e6034aa6bc44665856ee480962a9ddab0b87cc`](https://github.com/RandyGaul/cute_framework/commit/96e6034aa6bc44665856ee480962a9ddab0b87cc) and applies independently, or after the other retained patches.

```sh
git apply --check cute-sound-free-queue-music-volume.patch
git apply cute-sound-free-queue-music-volume.patch
```

Patch SHA-256: `e958d593f3bbdbd90987f3cd6421df099fc867ec77acf5a11aebc089d60ad2ab`. It has no gist yet.
