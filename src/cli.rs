use std::env;
use std::fs;
use std::path::PathBuf;

use crate::decoder;
use crate::error::RtlError;
use crate::h264;

pub fn run() -> Result<(), RtlError> {
    let input = parse_input(env::args().skip(1))?;
    let bytes = fs::read(&input)?;
    let stream = h264::probe_mp4_avcc(&bytes)?;
    println!(
        "H.264 stream: {}x{}, {}-byte NAL lengths",
        stream.dimensions.width, stream.dimensions.height, stream.length_size
    );
    let (total, directory) = decoder::decode_with_ffmpeg_png(&input, stream)?;
    println!("RTL --gc: wrote {total} decoded RGB frames to {}", directory.display());
    Ok(())
}

fn parse_input(mut arguments: impl Iterator<Item = String>) -> Result<PathBuf, RtlError> {
    let command = arguments.next().ok_or_else(|| RtlError::Usage(usage().into()))?;
    let input = arguments.next().ok_or_else(|| RtlError::Usage(usage().into()))?;
    if command != "--gc" || arguments.next().is_some() {
        return Err(RtlError::Usage(usage().into()));
    }
    Ok(PathBuf::from(input))
}

fn usage() -> &'static str { "usage: RTL --gc <localfile.mp4|localfile.mov>" }