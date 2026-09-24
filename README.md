# InpaintingRemover

## RTL frame extractor

Build the dependency-free Rust command-line prototype with `cargo build --release`,
then run:

```text
target/release/RTL --gc localfile.mp4
target/release/RTL --gc localfile.mov
```

The command is intended to create `localfile_ai_frames/` beside the input. Each
`frame_000000.ppm` file is an 8-bit RGB frame, and `manifest.txt` records its
dimensions and order.

This implementation parses the MP4/MOV ISO-BMFF container without external
encoder or decoder libraries. Unsupported H.264 syntax returns an error and
never produces placeholder pixels.

The first pure-Rust H.264 stage is now in `src/h264.rs`. It parses AVCC and
Annex-B NAL units, removes emulation-prevention bytes, and reads SPS dimensions.
For `test.mp4`, this reports a `464x832` H.264 stream with 4-byte NAL lengths.
The decoder integration validates Baseline `avcC` parameter sets and contains
the integer YUV 4:2:0 to RGB conversion used by the reconstruction stage.
Full coded-slice reconstruction (CAVLC, intra/inter prediction, inverse
transform, and reference-picture management) remains unsupported; the provided
`test.mp4` therefore exits explicitly at its first type-1 coded slice.

`src/decoded.rs` defines the next-stage AI contract: validated 8-bit RGB frames
stored in a linked list and written as standard PPM (`P6`) images. PPM needs no
image library and can be loaded directly by common computer-vision tooling. The
H.264 slice decoder must populate `RgbFrame` before `*_ai_frames/` can be
generated from an H.264 input.

For production decoding, `RTL --gc` now uses the installed FFmpeg decoder and
writes compressed PNG images to `test_ai_frames/` (or `<input>_ai_frames/`).
Install FFmpeg on Windows with `winget install Gyan.FFmpeg.Shared`, then open a
new terminal so `ffmpeg` is available on `PATH`.

The PNG decoder builds an `ImageFrameList` linked list in `src/decoded.rs`.
Each node stores the frame index, image path, and dimensions; pixel bytes stay
in the PNG file so a long video does not require all decoded images in memory.

### Module layout

- `src/cli.rs` parses commands and coordinates the application.
- `src/bmff.rs` reads MP4/MOV container boxes and video sample tables.
- `src/frames.rs` owns the linked-list frame data structure.
- `src/decoded.rs` owns validated RGB frames and AI-ready PPM output.
- `src/output.rs` retains the legacy encoded-sample writer.
- `src/error.rs` contains shared application errors.

Future readers, decoders, or output formats can be added as modules without
changing the linked-list or command entry point.

## workflow
Please use the following conventions when submitting your code. First clone the repository, Then create a new branch with the following convention:
git checkout -b <Yourname/*/**>
* = could be bugfix, feature or refactor.<br>
** = here the implementation of whatever you doing.
