use crate::bmff;
use crate::decoded::{DecodedFrameList, RgbFrame};
use crate::error::RtlError;
use crate::h264::{self, NalKind, NalUnit, StreamInfo};
use std::io::{Read, BufReader, Write};
use std::fs::{self, File};
use std::path::Path;
use std::process::{Command, Stdio};

#[derive(Debug, Clone)]
struct BaselineConfig {
    stream: StreamInfo,
    sps: NalUnit,
    pps: NalUnit,
}

pub fn decode_mp4(bytes: &[u8]) -> Result<(StreamInfo, DecodedFrameList), RtlError> {
    let config = read_baseline_config(bytes)?;
    let mut samples = bmff::extract_video_samples(bytes)?;
    let mut decoded = DecodedFrameList::new();
    while let Some(sample) = samples.pop() {
        let nals = h264::split_avcc_sample(&sample.data, config.stream.length_size)?;
        decode_access_unit(&config, &nals, &mut decoded)?;
    }
    Ok((config.stream, decoded))
}

fn read_baseline_config(bytes: &[u8]) -> Result<BaselineConfig, RtlError> {
    let stream = h264::probe_mp4_avcc(bytes)?;
    let (sps, pps) = h264::parameter_sets_from_avcc(bytes)?;
    let profile = sps.payload.get(1).copied().unwrap_or(0);
    if profile != 0x42 {
        return Err(RtlError::Unsupported(format!(
            "H.264 profile 0x{profile:02x} is unsupported; only Baseline profile 0x42 is implemented"
        )));
    }
    Ok(BaselineConfig { stream, sps, pps })
}

fn decode_access_unit(
    _config: &BaselineConfig,
    nals: &[NalUnit],
    _decoded: &mut DecodedFrameList,
) -> Result<(), RtlError> {
    for nal in nals {
        match nal.kind {
            NalKind::CodedSlice | NalKind::IdrSlice => {
                return Err(RtlError::Unsupported("H.264 coded-slice reconstruction (CAVLC, prediction, and reference pictures) is not implemented".into()));
            }
            NalKind::AccessUnitDelimiter | NalKind::SupplementalEnhancement => {}
            NalKind::SequenceParameterSet | NalKind::PictureParameterSet => {}
            _ => return Err(RtlError::Unsupported(format!("H.264 NAL type {:?} is unsupported", nal.kind))),
        }
    }
    Err(RtlError::Unsupported("H.264 access unit contains no decodable slice".into()))
}

pub fn yuv420_to_rgb(width: u32, height: u32, y: &[u8], cb: &[u8], cr: &[u8]) -> Result<RgbFrame, RtlError> {
    let width_usize = usize::try_from(width).map_err(|_| RtlError::Format("frame width is too large".into()))?;
    let height_usize = usize::try_from(height).map_err(|_| RtlError::Format("frame height is too large".into()))?;
    let luma_len = width_usize.checked_mul(height_usize).ok_or_else(|| RtlError::Format("frame dimensions overflow".into()))?;
    let chroma_width = width_usize.div_ceil(2);
    let chroma_height = height_usize.div_ceil(2);
    let chroma_len = chroma_width.checked_mul(chroma_height).ok_or_else(|| RtlError::Format("chroma dimensions overflow".into()))?;
    if y.len() != luma_len || cb.len() != chroma_len || cr.len() != chroma_len {
        return Err(RtlError::Format("YUV 4:2:0 planes have invalid sizes".into()));
    }
    let mut pixels = Vec::with_capacity(luma_len * 3);
    for row in 0..height_usize {
        for column in 0..width_usize {
            let y_value = i32::from(y[row * width_usize + column]);
            let chroma_index = (row / 2) * chroma_width + column / 2;
            let cb_value = i32::from(cb[chroma_index]) - 128;
            let cr_value = i32::from(cr[chroma_index]) - 128;
            pixels.push(clamp_byte(y_value + ((359 * cr_value) >> 8)));
            pixels.push(clamp_byte(y_value - ((88 * cb_value + 183 * cr_value) >> 8)));
            pixels.push(clamp_byte(y_value + ((454 * cb_value) >> 8)));
        }
    }
    RgbFrame::new(width, height, pixels)
}

fn clamp_byte(value: i32) -> u8 { value.clamp(0, 255) as u8 }

#[cfg(test)]
mod tests {
    use super::yuv420_to_rgb;

    #[test]
    fn converts_neutral_yuv_to_rgb() {
        let frame = yuv420_to_rgb(2, 2, &[100, 100, 100, 100], &[128], &[128]).unwrap();
        assert_eq!(frame.pixels, vec![100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100]);
    }
}

pub fn decode_with_ffmpeg(input: &Path, stream: StreamInfo) -> Result<(usize, std::path::PathBuf), RtlError> {
    let mut child = Command::new("ffmpeg")
        .args(["-v", "error", "-i"])
        .arg(input)
        .args(["-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|error| RtlError::Io(error))?;
    let stdout = child.stdout.take().ok_or_else(|| RtlError::Format("FFmpeg stdout was unavailable".into()))?;
    let mut reader = BufReader::new(stdout);
    let frame_size = usize::try_from(stream.dimensions.width)
        .ok().and_then(|width| usize::try_from(stream.dimensions.height).ok().and_then(|height| width.checked_mul(height)))
        .and_then(|pixels| pixels.checked_mul(3))
        .ok_or_else(|| RtlError::Format("decoded frame dimensions overflow".into()))?;
    let output = input.parent().unwrap_or_else(|| Path::new(".")).join(format!(
        "{}_ai_frames",
        input.file_stem().and_then(|value| value.to_str()).unwrap_or("video")
    ));
    let mut writer = crate::decoded::PpmDirectoryWriter::new(input, output)?;
    let mut frame_data = vec![0; frame_size];
    let mut count = 0;
    loop {
        let mut received = 0;
        while received < frame_size {
            let read = reader.read(&mut frame_data[received..])?;
            if read == 0 { break; }
            received += read;
        }
        if received == 0 { break; }
        if received != frame_size {
            return Err(RtlError::Format("FFmpeg returned a truncated RGB frame".into()));
        }
        writer.write(RgbFrame::new(stream.dimensions.width, stream.dimensions.height, frame_data.clone())?)?;
        count += 1;
    }
    let status = child.wait()?;
    if !status.success() {
        return Err(RtlError::Format(format!("FFmpeg failed with status {status}")));
    }
    Ok((count, writer.finish()?))
}

pub fn decode_with_ffmpeg_png(input: &Path, stream: StreamInfo) -> Result<(usize, std::path::PathBuf), RtlError> {
    let output = input.parent().unwrap_or_else(|| Path::new(".")).join(format!(
        "{}_ai_frames",
        input.file_stem().and_then(|value| value.to_str()).unwrap_or("video")
    ));
    fs::create_dir_all(&output)?;
    for old in fs::read_dir(&output)? {
        let path = old?.path();
        if path.extension().and_then(|value| value.to_str()) == Some("png") { fs::remove_file(path)?; }
    }
    let pattern = output.join("frame_%06d.png");
    let status = Command::new("ffmpeg")
        .args(["-y", "-v", "error", "-i"])
        .arg(input)
        .arg(&pattern)
        .status()
        .map_err(RtlError::Io)?;
    if !status.success() { return Err(RtlError::Format(format!("FFmpeg failed with status {status}"))); }
    let mut frames: Vec<_> = fs::read_dir(&output)?.filter_map(Result::ok)
        .map(|entry| entry.path())
        .filter(|path| path.extension().and_then(|value| value.to_str()) == Some("png"))
        .collect();
    frames.sort();
    let mut linked_frames = crate::decoded::ImageFrameList::new();
    for (index, path) in frames.into_iter().enumerate() {
        linked_frames.push(crate::decoded::ImageFrameRecord {
            index,
            path,
            width: stream.dimensions.width,
            height: stream.dimensions.height,
        });
    }
    let mut manifest = File::create(output.join("manifest.txt"))?;
    writeln!(manifest, "source={}", input.display())?;
    writeln!(manifest, "format=PNG, decoded RGB video frames")?;
    writeln!(manifest, "dimensions={}x{}", stream.dimensions.width, stream.dimensions.height)?;
    writeln!(manifest, "frames={}", linked_frames.len())?;
    let mut ordered = Vec::with_capacity(linked_frames.len());
    while let Some(frame) = linked_frames.pop() {
        ordered.push(frame);
    }
    ordered.reverse();
    let total = ordered.len();
    for frame in ordered {
        writeln!(manifest, "{} {} {}x{}", frame.index, frame.path.file_name().unwrap().to_string_lossy(), frame.width, frame.height)?;
    }
    Ok((total, output))
}