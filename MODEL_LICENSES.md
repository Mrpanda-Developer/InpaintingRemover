# Model Licenses

No pretrained models or checkpoints are distributed or loaded by this MVP.
The `OnnxInpainter` class is an interface placeholder and deliberately refuses
to run without a future model integration. No model license or training-data
provenance is therefore applicable, and no model SHA256 values exist.

Before integrating a model, record its exact version/source, code license,
weight license, explicit commercial-use permission, known training-data
provenance, and SHA256 here. Models with unclear commercial-use terms must not
be added.

## Runtime Libraries

The application source remains under the repository's BSD-2-Clause license.
OpenCV is generally Apache-2.0 licensed; FFmpeg licensing depends on the
features and configure flags used to build the linked libraries. Commercial
distributions should use and verify an LGPL-compatible FFmpeg build and audit
all enabled codecs and external libraries. ONNX Runtime is not required by the
default build; optional future integrations must verify the runtime package and
model-weight licenses independently.