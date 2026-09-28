# subtitle-remover

`subtitle-remover` is a C++20 command-line foundation for chunked video subtitle
removal. This first milestone is intentionally a **passthrough-only MVP**: it
decodes video with FFmpeg, buffers bounded temporal chunks with overlap, and
re-encodes each central frame once while remuxing compatible audio. It does not
yet remove subtitles, emit detection events, or run a neural model. The
interfaces for detection, optical flow, and inpainting are in place for those
follow-on stages.

## Features In This Milestone

- FFmpeg C libraries for demuxing, decoding, encoding, and muxing; no FFmpeg CLI
	is used in the processing pipeline.
- OpenCV BGR frame conversion and replaceable detector, optical-flow, and
	inpainter interfaces.
- Default 8-second output chunks with 0.5-second context overlap on each side.
- Only central frames are encoded. Original decoded presentation timestamps
	are passed to the encoder; VFR input is not replaced with generated CFR PTS.
- A three-stage decoder / processing / encoder handoff with bounded queues
	(capacity three). The processing stage currently forwards chunks unchanged.
- Compatible audio streams are packet-copied and timestamp-rescaled. Audio
	codecs unsupported by the output container are omitted with a warning.
- A JSON review report is always written beside the output unless
	`--review-report` selects another path. This MVP reports no detected events.
- SIGINT stops at a chunk boundary and finalizes the partial output/report.

The encoder must be available in the linked FFmpeg build. The default output
codec is H.264; use an LGPL-compatible FFmpeg build and review all codec/library
licenses for commercial distribution. See [MODEL_LICENSES.md](MODEL_LICENSES.md).

## Linux Build

Install a C++20 compiler, CMake, pkg-config, FFmpeg development libraries, and
OpenCV 4.5 or newer. For Debian/Ubuntu, package names are typically:

```bash
sudo apt install build-essential cmake pkg-config \
	libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libopencv-dev
```

Then configure, build, and run the focused timeline tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

ONNX Runtime is not required for this milestone. The build exposes an opt-in
discovery switch (`-DSUBTITLE_REMOVER_WITH_ONNXRUNTIME=ON`) for future use; no
model is included and the current `OnnxInpainter` deliberately reports that it
is not implemented.

## Examples

Run the passthrough pipeline with the requested balanced chunking defaults:

```bash
build/subtitle-remover \
	--input input.mxf \
	--output output.mxf \
	--chunk-duration 8 \
	--overlap 0.5 \
	--subtitle-region bottom-35 \
	--detection-interval 5 \
	--mask-padding 6 \
	--temporal-radius 12 \
	--device cpu \
	--quality balanced
```

The full accepted options are listed by `build/subtitle-remover --help`.
Presets are `fast`, `balanced`, and `broadcast`; explicit option values are
applied after the preset regardless of argument order. Example settings are in
[examples/balanced-config.json](examples/balanced-config.json). That JSON file
documents settings; it is not currently a runtime configuration input.

Run the integration harness against a representative fixture:

```bash
build/passthrough-integration input.mxf output.mxf
```

The output report is `output.mxf.review.json`. The harness intentionally does
not create fixtures or call the FFmpeg command-line executable.

## Module Map

- `video_reader` and `video_writer`: FFmpeg decode/encode/remux with RAII-owned
	format, codec, frame, packet, and scaling resources.
- `chunk_reader`: timestamp-based central windows plus bounded temporal context.
- `processing_interfaces`: replaceable subtitle detector, optical flow,
	OpenCV inpainter, and ONNX inpainter contracts.
- `pipeline`: bounded three-stage event flow and review-report output.
- `cli` and `config`: validation, options, and the fast/balanced/broadcast
	presets.

The next implementation milestone should verify fixture round-trips, packet
timestamp and A/V sync behavior across supported muxers, then add subtitle
events, temporal reconstruction, confidence scoring, and only then an optional
commercially licensed model integration.
