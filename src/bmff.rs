use crate::error::RtlError;
use crate::frames::{EncodedFrame, FrameList};

#[derive(Clone, Copy)]
struct BoxInfo {
    kind: [u8; 4],
    payload_start: usize,
    end: usize,
}

#[derive(Clone, Copy)]
struct StscEntry {
    first_chunk: u32,
    samples_per_chunk: u32,
}

pub fn extract_video_samples(bytes: &[u8]) -> Result<FrameList, RtlError> {
    let top_level = boxes_in(bytes, 0, bytes.len())?;
    if !top_level.iter().any(|item| &item.kind == b"ftyp") {
        return Err(RtlError::Format("input is not an ISO-BMFF MP4/MOV file".into()));
    }
    let moov = top_level.iter().find(|item| &item.kind == b"moov")
        .ok_or_else(|| RtlError::Format("video has no moov box".into()))?;
    let tracks = boxes_in(bytes, moov.payload_start, moov.end)?;
    for track in tracks.iter().filter(|item| &item.kind == b"trak") {
        if let Some(samples) = samples_for_track(bytes, *track)? { return Ok(samples); }
    }
    Err(RtlError::Format("video has no supported vide track".into()))
}

fn samples_for_track(bytes: &[u8], track: BoxInfo) -> Result<Option<FrameList>, RtlError> {
    let children = boxes_in(bytes, track.payload_start, track.end)?;
    let mdia = match children.iter().find(|item| &item.kind == b"mdia") {
        Some(value) => *value,
        None => return Ok(None),
    };
    let mdia_children = boxes_in(bytes, mdia.payload_start, mdia.end)?;
    let handler = match mdia_children.iter().find(|item| &item.kind == b"hdlr") {
        Some(value) => *value,
        None => return Ok(None),
    };
    if bytes.get(handler.payload_start + 8..handler.payload_start + 12) != Some(b"vide") {
        return Ok(None);
    }
    let minf = mdia_children.iter().find(|item| &item.kind == b"minf")
        .ok_or_else(|| RtlError::Format("video track has no minf box".into()))?;
    let stbl = boxes_in(bytes, minf.payload_start, minf.end)?.into_iter()
        .find(|item| &item.kind == b"stbl")
        .ok_or_else(|| RtlError::Format("video track has no stbl box".into()))?;
    let tables = boxes_in(bytes, stbl.payload_start, stbl.end)?;
    let sizes_box = find_box(&tables, b"stsz")
        .ok_or_else(|| RtlError::Format("video track has no stsz sample table".into()))?;
    let chunk_offsets = find_box(&tables, b"stco").or_else(|| find_box(&tables, b"co64"))
        .ok_or_else(|| RtlError::Format("video track has no chunk offset table".into()))?;
    let stsc = find_box(&tables, b"stsc")
        .ok_or_else(|| RtlError::Format("video track has no stsc sample table".into()))?;

    let sizes = parse_sample_sizes(bytes, sizes_box)?;
    let offsets = parse_chunk_offsets(bytes, chunk_offsets)?;
    let mappings = parse_stsc(bytes, stsc)?;
    let sample_offsets = build_sample_offsets(&offsets, &mappings, &sizes)?;
    let mut frames = FrameList::new();
    for (offset, size) in sample_offsets.into_iter().zip(sizes) {
        let end = offset.checked_add(size)
            .ok_or_else(|| RtlError::Format("sample offset overflow".into()))?;
        let data = bytes.get(offset..end)
            .ok_or_else(|| RtlError::Format("sample points outside the input file".into()))?;
        frames.push(EncodedFrame { data: data.to_vec() });
    }
    Ok(Some(frames))
}

fn boxes_in(bytes: &[u8], start: usize, end: usize) -> Result<Vec<BoxInfo>, RtlError> {
    let mut result = Vec::new();
    let mut cursor = start;
    while cursor < end {
        if end - cursor < 8 { return Err(RtlError::Format("truncated box header".into())); }
        let size32 = read_u32(bytes, cursor)? as u64;
        let kind = read_fourcc(bytes, cursor + 4)?;
        let (header_size, box_size) = if size32 == 1 {
            (16, read_u64(bytes, cursor + 8)?)
        } else if size32 == 0 { (8, (end - cursor) as u64) } else { (8, size32) };
        let box_size = usize::try_from(box_size).map_err(|_| RtlError::Format("box is too large".into()))?;
        let box_end = cursor.checked_add(box_size)
            .ok_or_else(|| RtlError::Format("box size overflow".into()))?;
        if box_size < header_size || box_end > end {
            return Err(RtlError::Format("box extends outside its parent".into()));
        }
        result.push(BoxInfo { kind, payload_start: cursor + header_size, end: box_end });
        cursor = box_end;
    }
    Ok(result)
}

fn find_box(boxes: &[BoxInfo], kind: &[u8; 4]) -> Option<BoxInfo> {
    boxes.iter().find(|item| &item.kind == kind).copied()
}

fn parse_sample_sizes(bytes: &[u8], info: BoxInfo) -> Result<Vec<usize>, RtlError> {
    let payload = info.payload_start;
    let fixed_size = read_u32(bytes, payload + 4)?;
    let count = usize::try_from(read_u32(bytes, payload + 8)?)
        .map_err(|_| RtlError::Format("too many samples".into()))?;
    if fixed_size != 0 { return Ok(vec![fixed_size as usize; count]); }
    (0..count).map(|index| read_u32(bytes, payload + 12 + index * 4).map(|size| size as usize)).collect()
}

fn parse_chunk_offsets(bytes: &[u8], info: BoxInfo) -> Result<Vec<u64>, RtlError> {
    let count = read_u32(bytes, info.payload_start + 4)? as usize;
    (0..count).map(|index| {
        let width = if &info.kind == b"co64" { 8 } else { 4 };
        let position = info.payload_start + 8 + index * width;
        if &info.kind == b"co64" { read_u64(bytes, position) } else { read_u32(bytes, position).map(u64::from) }
    }).collect()
}

fn parse_stsc(bytes: &[u8], info: BoxInfo) -> Result<Vec<StscEntry>, RtlError> {
    let count = read_u32(bytes, info.payload_start + 4)? as usize;
    (0..count).map(|index| {
        let position = info.payload_start + 8 + index * 12;
        Ok(StscEntry { first_chunk: read_u32(bytes, position)?, samples_per_chunk: read_u32(bytes, position + 4)? })
    }).collect()
}

fn build_sample_offsets(chunks: &[u64], mappings: &[StscEntry], sizes: &[usize]) -> Result<Vec<usize>, RtlError> {
    let mut result = Vec::with_capacity(sizes.len());
    let mut sample_index = 0;
    for chunk_number in 1..=chunks.len() as u32 {
        let mapping = mappings.iter().rev().find(|entry| entry.first_chunk <= chunk_number)
            .ok_or_else(|| RtlError::Format("stsc table does not cover all chunks".into()))?;
        let chunk_end = sample_index + mapping.samples_per_chunk as usize;
        let mut offset = usize::try_from(chunks[chunk_number as usize - 1])
            .map_err(|_| RtlError::Format("chunk offset is too large".into()))?;
        for size in sizes.get(sample_index..chunk_end).ok_or_else(|| {
            RtlError::Format("stsc table references more samples than stsz".into())
        })? {
            result.push(offset);
            offset = offset.checked_add(*size)
                .ok_or_else(|| RtlError::Format("sample offset overflow".into()))?;
        }
        sample_index = chunk_end;
    }
    if sample_index != sizes.len() {
        return Err(RtlError::Format("stsc table does not describe every sample".into()));
    }
    Ok(result)
}

fn read_fourcc(bytes: &[u8], position: usize) -> Result<[u8; 4], RtlError> {
    bytes.get(position..position + 4).and_then(|value| value.try_into().ok())
        .ok_or_else(|| RtlError::Format("truncated fourcc".into()))
}

fn read_u32(bytes: &[u8], position: usize) -> Result<u32, RtlError> {
    let value = bytes.get(position..position + 4)
        .ok_or_else(|| RtlError::Format("truncated integer".into()))?;
    Ok(u32::from_be_bytes(value.try_into().unwrap()))
}

fn read_u64(bytes: &[u8], position: usize) -> Result<u64, RtlError> {
    let value = bytes.get(position..position + 8)
        .ok_or_else(|| RtlError::Format("truncated integer".into()))?;
    Ok(u64::from_be_bytes(value.try_into().unwrap()))
}