use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::error::RtlError;
use crate::frames::FrameList;

pub fn write_frame_directory(input: &Path, frames: &mut FrameList) -> Result<PathBuf, RtlError> {
    let output = output_directory(input);
    fs::create_dir_all(&output)?;
    let mut manifest = File::create(output.join("manifest.txt"))?;
    writeln!(manifest, "source={}", input.display())?;
    writeln!(manifest, "format=encoded ISO-BMFF video samples")?;
    writeln!(manifest, "frames={}", frames.len())?;

    let mut frame_number = 0;
    while let Some(frame) = frames.pop() {
        let frame_name = format!("frame_{frame_number:06}.bin");
        fs::write(output.join(&frame_name), &frame.data)?;
        writeln!(manifest, "{frame_number} {frame_name} {}", frame.data.len())?;
        frame_number += 1;
    }
    Ok(output)
}

fn output_directory(input: &Path) -> PathBuf {
    let stem = input.file_stem().and_then(|value| value.to_str()).unwrap_or("video");
    input.parent().unwrap_or_else(|| Path::new(".")).join(format!("{stem}_frames"))
}