# Sound fixtures

`Tone.ogg` is 10 ms of a 330 Hz sine, mono at 44.1 kHz, the smallest real Ogg
Vorbis file players decode. `validate_project` damages copies of it to check
that a scenario, like preparation, refuses Vorbis audio players cannot decode.
It was made with:

    ffmpeg -f lavfi -i sine=frequency=330:duration=0.01:sample_rate=44100 -ac 1 \
        -c:a libvorbis -q:a -1 -fflags +bitexact -flags:a +bitexact Tone.ogg
