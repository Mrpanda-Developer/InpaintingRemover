use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::error::RtlError;

pub struct RgbFrame {
    pub width: u32,
    pub height: u32,
    pub pixels: Vec<u8>,
}

impl RgbFrame {
    pub fn new(width: u32, height: u32, pixels: Vec<u8>) -> Result<Self, RtlError> {
        let expected = usize::try_from(width)
            .ok()
            .and_then(|width| usize::try_from(height).ok().and_then(|height| width.checked_mul(height)))
            .and_then(|pixels| pixels.checked_mul(3))
            .ok_or_else(|| RtlError::Format("decoded frame dimensions overflow".into()))?;
        if pixels.len() != expected {
            return Err(RtlError::Format(format!(
                "decoded RGB frame has {} bytes, expected {expected}",
                pixels.len()
            )));
        }
        Ok(Self { width, height, pixels })
    }
}

struct Node {
    frame: RgbFrame,
    next: Option<Box<Node>>,
}

pub struct DecodedFrameList {
    head: Option<Box<Node>>,
    len: usize,
}

impl DecodedFrameList {
    pub fn new() -> Self { Self { head: None, len: 0 } }

    pub fn push(&mut self, frame: RgbFrame) {
        self.head = Some(Box::new(Node { frame, next: self.head.take() }));
        self.len += 1;
    }

    pub fn len(&self) -> usize { self.len }

    pub fn pop(&mut self) -> Option<RgbFrame> {
        self.head.take().map(|node| {
            self.head = node.next;
            self.len -= 1;
            node.frame
        })
    }
}

pub fn write_ppm_directory(input: &Path, frames: &mut DecodedFrameList) -> Result<PathBuf, RtlError> {
    let output = ai_output_directory(input);
    let mut writer = PpmDirectoryWriter::new(input, output)?;
    while let Some(frame) = frames.pop() {
        writer.write(frame)?;
    }
    writer.finish()
}

pub struct PpmDirectoryWriter {
    output: PathBuf,
    manifest: File,
    frame_number: usize,
}

impl PpmDirectoryWriter {
    pub fn new(input: &Path, output: PathBuf) -> Result<Self, RtlError> {
        fs::create_dir_all(&output)?;
        let mut manifest = File::create(output.join("manifest.txt"))?;
        writeln!(manifest, "source={}", input.display())?;
        writeln!(manifest, "format=RGB PPM (P6), 8-bit, 3 channels")?;
        writeln!(manifest, "frames=streamed")?;
        Ok(Self { output, manifest, frame_number: 0 })
    }

    pub fn write(&mut self, frame: RgbFrame) -> Result<(), RtlError> {
        let filename = format!("frame_{:06}.ppm", self.frame_number);
        let mut file = File::create(self.output.join(&filename))?;
        writeln!(file, "P6\n{} {}\n255", frame.width, frame.height)?;
        file.write_all(&frame.pixels)?;
        writeln!(self.manifest, "{} {filename} {}x{}", self.frame_number, frame.width, frame.height)?;
        self.frame_number += 1;
        Ok(())
    }

    pub fn finish(mut self) -> Result<PathBuf, RtlError> {
        self.manifest.flush()?;
        Ok(self.output)
    }
}

fn ai_output_directory(input: &Path) -> PathBuf {
    input.parent().unwrap_or_else(|| Path::new(".")).join(format!(
        "{}_ai_frames",
        input.file_stem().and_then(|value| value.to_str()).unwrap_or("video")
    ))
}

#[cfg(test)]
mod tests {
    use super::RgbFrame;

    #[test]
    fn validates_rgb_buffer_size() {
        assert!(RgbFrame::new(2, 2, vec![0; 12]).is_ok());
        assert!(RgbFrame::new(2, 2, vec![0; 11]).is_err());
    }
}

impl Drop for DecodedFrameList {
    fn drop(&mut self) {
        let mut node = self.head.take();
        while let Some(mut current) = node {
            node = current.next.take();
        }
    }
}

pub struct ImageFrameRecord {
    pub index: usize,
    pub path: PathBuf,
    pub width: u32,
    pub height: u32,
}

struct ImageNode {
    frame: ImageFrameRecord,
    next: Option<Box<ImageNode>>,
}

pub struct ImageFrameList {
    head: Option<Box<ImageNode>>,
    len: usize,
}

impl ImageFrameList {
    pub fn new() -> Self { Self { head: None, len: 0 } }

    pub fn push(&mut self, frame: ImageFrameRecord) {
        self.head = Some(Box::new(ImageNode { frame, next: self.head.take() }));
        self.len += 1;
    }

    pub fn len(&self) -> usize { self.len }

    pub fn pop(&mut self) -> Option<ImageFrameRecord> {
        self.head.take().map(|node| {
            self.head = node.next;
            self.len -= 1;
            node.frame
        })
    }
}