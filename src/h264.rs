use crate::error::RtlError;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NalKind {
    Unspecified,
    CodedSlice,
    IdrSlice,
    SupplementalEnhancement,
    SequenceParameterSet,
    PictureParameterSet,
    AccessUnitDelimiter,
    EndOfSequence,
    EndOfStream,
    Other(u8),
}

#[derive(Debug, Clone)]
pub struct NalUnit {
    pub kind: NalKind,
    pub reference_idc: u8,
    pub payload: Vec<u8>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VideoDimensions {
    pub width: u32,
    pub height: u32,
}

#[derive(Debug, Clone, Copy)]
pub struct StreamInfo {
    pub dimensions: VideoDimensions,
    pub length_size: usize,
}

pub fn probe_mp4_avcc(bytes: &[u8]) -> Result<StreamInfo, RtlError> {
    let marker = b"avcC";
    let marker_position = bytes.windows(marker.len()).position(|window| window == marker)
        .ok_or_else(|| RtlError::Format("MP4 has no H.264 avcC configuration".into()))?;
    let config = bytes.get(marker_position + 4..)
        .ok_or_else(|| RtlError::Format("truncated H.264 avcC configuration".into()))?;
    let length_size = usize::from((config.get(4).copied().unwrap_or(0) & 3) + 1);
    let sps_count = usize::from(config.get(5).copied().unwrap_or(0) & 0x1f);
    if sps_count == 0 {
        return Err(RtlError::Format("H.264 avcC contains no SPS".into()));
    }
    let sps_length = usize::from(u16::from_be_bytes([
        *config.get(6).ok_or_else(|| RtlError::Format("truncated H.264 SPS length".into()))?,
        *config.get(7).ok_or_else(|| RtlError::Format("truncated H.264 SPS length".into()))?,
    ]));
    let sps = config.get(8..8 + sps_length)
        .ok_or_else(|| RtlError::Format("truncated H.264 SPS".into()))?;
    let sps = parse_nal(sps)?;
    let dimensions = dimensions_from_sps(&sps)?;
    Ok(StreamInfo { dimensions, length_size })
}

pub fn split_avcc_sample(sample: &[u8], length_size: usize) -> Result<Vec<NalUnit>, RtlError> {
    if !(1..=4).contains(&length_size) {
        return Err(RtlError::Format("H.264 AVCC length size must be 1 to 4".into()));
    }
    let mut units = Vec::new();
    let mut cursor = 0;
    while cursor < sample.len() {
        let end_of_length = cursor.checked_add(length_size)
            .ok_or_else(|| RtlError::Format("H.264 NAL length overflow".into()))?;
        let length = read_be_length(&sample[cursor..end_of_length]);
        cursor = end_of_length;
        let end = cursor.checked_add(length)
            .ok_or_else(|| RtlError::Format("H.264 NAL extends past sample".into()))?;
        let nal = sample.get(cursor..end)
            .ok_or_else(|| RtlError::Format("H.264 NAL extends past sample".into()))?;
        if !nal.is_empty() { units.push(parse_nal(nal)?); }
        cursor = end;
    }
    Ok(units)
}

pub fn split_annex_b(stream: &[u8]) -> Result<Vec<NalUnit>, RtlError> {
    let mut units = Vec::new();
    let mut cursor = 0;
    while let Some(start) = find_start_code(stream, cursor) {
        let payload_start = start + if stream[start + 2] == 1 { 3 } else { 4 };
        let end = find_start_code(stream, payload_start).unwrap_or(stream.len());
        if payload_start < end { units.push(parse_nal(&stream[payload_start..end])?); }
        cursor = end;
    }
    Ok(units)
}

pub fn dimensions_from_sps(nal: &NalUnit) -> Result<VideoDimensions, RtlError> {
    if nal.kind != NalKind::SequenceParameterSet {
        return Err(RtlError::Format("H.264 dimensions require an SPS NAL".into()));
    }
    let rbsp = unescape_rbsp(&nal.payload[1..]);
    let mut bits = BitReader::new(&rbsp);
    let profile_idc = bits.read_bits(8)? as u8;
    bits.skip_bits(8)?;
    bits.skip_bits(8)?;
    let _sps_id = bits.read_ue()?;
    if is_high_profile(profile_idc) {
        let chroma_format_idc = bits.read_ue()?;
        if chroma_format_idc == 3 { bits.skip_bits(1)?; }
        bits.read_ue()?;
        bits.read_ue()?;
        bits.skip_bits(1)?;
        if bits.read_bit()? { skip_scaling_matrix(&mut bits, chroma_format_idc)?; }
    }
    bits.read_ue()?;
    let pic_order_cnt_type = bits.read_ue()?;
    if pic_order_cnt_type == 0 {
        bits.read_ue()?;
    } else if pic_order_cnt_type == 1 {
        bits.skip_bits(1)?;
        bits.read_se()?;
        bits.read_se()?;
        let count = bits.read_ue()?;
        for _ in 0..count { bits.read_se()?; }
    }
    bits.read_ue()?;
    bits.skip_bits(1)?;
    let width = (bits.read_ue()? + 1) * 16;
    let height = (bits.read_ue()? + 1) * 16;
    let frame_mbs_only = bits.read_bit()?;
    if !frame_mbs_only { bits.skip_bits(1)?; }
    bits.skip_bits(1)?;
    if bits.read_bit()? {
        let left = bits.read_ue()?;
        let right = bits.read_ue()?;
        let top = bits.read_ue()?;
        let bottom = bits.read_ue()?;
        let crop_unit_x = 1;
        let crop_unit_y = if frame_mbs_only { 2 } else { 4 };
        let crop_x = (left + right) * crop_unit_x;
        let crop_y = (top + bottom) * crop_unit_y;
        if crop_x >= width || crop_y >= height {
            return Err(RtlError::Format("H.264 SPS crop exceeds frame dimensions".into()));
        }
        return Ok(VideoDimensions { width: width - crop_x, height: height - crop_y });
    }
    Ok(VideoDimensions { width, height: height * if frame_mbs_only { 1 } else { 2 } })
}

fn parse_nal(nal: &[u8]) -> Result<NalUnit, RtlError> {
    let header = *nal.first().ok_or_else(|| RtlError::Format("empty H.264 NAL".into()))?;
    if header & 0x80 != 0 { return Err(RtlError::Format("invalid H.264 NAL header".into())); }
    let kind = match header & 0x1f {
        0 => NalKind::Unspecified,
        1 => NalKind::CodedSlice,
        5 => NalKind::IdrSlice,
        6 => NalKind::SupplementalEnhancement,
        7 => NalKind::SequenceParameterSet,
        8 => NalKind::PictureParameterSet,
        9 => NalKind::AccessUnitDelimiter,
        10 => NalKind::EndOfSequence,
        11 => NalKind::EndOfStream,
        value => NalKind::Other(value),
    };
    Ok(NalUnit { kind, reference_idc: (header >> 5) & 3, payload: nal.to_vec() })
}

fn find_start_code(bytes: &[u8], from: usize) -> Option<usize> {
    let mut index = from;
    while index + 3 <= bytes.len() {
        if bytes[index..].starts_with(&[0, 0, 1]) || bytes[index..].starts_with(&[0, 0, 0, 1]) {
            return Some(index);
        }
        index += 1;
    }
    None
}

fn read_be_length(bytes: &[u8]) -> usize {
    bytes.iter().fold(0usize, |value, byte| (value << 8) | usize::from(*byte))
}

fn unescape_rbsp(bytes: &[u8]) -> Vec<u8> {
    let mut result = Vec::with_capacity(bytes.len());
    let mut zeroes = 0;
    for &byte in bytes {
        if zeroes >= 2 && byte == 3 {
            zeroes = 0;
            continue;
        }
        result.push(byte);
        zeroes = if byte == 0 { zeroes + 1 } else { 0 };
    }
    result
}

fn is_high_profile(profile: u8) -> bool {
    matches!(profile, 100 | 110 | 122 | 244 | 44 | 83 | 86 | 118 | 128 | 138 | 139 | 134)
}

fn skip_scaling_matrix(bits: &mut BitReader<'_>, chroma_format: u32) -> Result<(), RtlError> {
    let count = if chroma_format == 3 { 12 } else { 8 };
    for index in 0..count {
        if bits.read_bit()? {
            let size = if index < 6 { 16 } else { 64 };
            let mut last = 8;
            let mut next = 8;
            for _ in 0..size {
                if next != 0 { next = (last + bits.read_se()? + 256) % 256; }
                last = if next == 0 { last } else { next };
            }
        }
    }
    Ok(())
}

struct BitReader<'a> {
    bytes: &'a [u8],
    position: usize,
}

impl<'a> BitReader<'a> {
    fn new(bytes: &'a [u8]) -> Self { Self { bytes, position: 0 } }

    fn read_bit(&mut self) -> Result<bool, RtlError> { Ok(self.read_bits(1)? != 0) }

    fn read_bits(&mut self, count: usize) -> Result<u32, RtlError> {
        if count > 32 { return Err(RtlError::Format("H.264 bit read is too wide".into())); }
        let mut value = 0;
        for _ in 0..count {
            let byte = self.bytes.get(self.position / 8)
                .ok_or_else(|| RtlError::Format("truncated H.264 SPS".into()))?;
            value = (value << 1) | u32::from((byte >> (7 - self.position % 8)) & 1);
            self.position += 1;
        }
        Ok(value)
    }

    fn skip_bits(&mut self, count: usize) -> Result<(), RtlError> { self.read_bits(count).map(|_| ()) }

    fn read_ue(&mut self) -> Result<u32, RtlError> {
        let mut leading_zeroes = 0;
        while !self.read_bit()? {
            leading_zeroes += 1;
            if leading_zeroes > 31 { return Err(RtlError::Format("invalid H.264 Exp-Golomb code".into())); }
        }
        let suffix = self.read_bits(leading_zeroes)?;
        Ok((1u32 << leading_zeroes) - 1 + suffix)
    }

    fn read_se(&mut self) -> Result<i32, RtlError> {
        let code = self.read_ue()? as i32;
        Ok(if code & 1 == 0 { -(code / 2) } else { (code + 1) / 2 })
    }
}

#[cfg(test)]
mod tests {
    use super::{split_annex_b, NalKind};

    #[test]
    fn splits_annex_b_nals() {
        let stream = [0, 0, 0, 1, 0x09, 0xf0, 0, 0, 1, 0x67, 0x42];
        let units = split_annex_b(&stream).unwrap();
        assert_eq!(units.len(), 2);
        assert_eq!(units[0].kind, NalKind::AccessUnitDelimiter);
        assert_eq!(units[1].kind, NalKind::SequenceParameterSet);
    }
}

pub fn parameter_sets_from_avcc(bytes: &[u8]) -> Result<(NalUnit, NalUnit), RtlError> {
    let marker = b"avcC";
    let marker_position = bytes.windows(marker.len()).position(|window| window == marker)
        .ok_or_else(|| RtlError::Format("MP4 has no H.264 avcC configuration".into()))?;
    let config = bytes.get(marker_position + 4..)
        .ok_or_else(|| RtlError::Format("truncated H.264 avcC configuration".into()))?;
    let sps_count = usize::from(config.get(5).copied().ok_or_else(|| RtlError::Format("truncated H.264 avcC configuration".into()))? & 0x1f);
    if sps_count == 0 { return Err(RtlError::Format("H.264 avcC contains no SPS".into())); }
    let sps_length = usize::from(u16::from_be_bytes([
        *config.get(6).ok_or_else(|| RtlError::Format("truncated H.264 SPS length".into()))?,
        *config.get(7).ok_or_else(|| RtlError::Format("truncated H.264 SPS length".into()))?,
    ]));
    let sps_end = 8usize.checked_add(sps_length).ok_or_else(|| RtlError::Format("H.264 SPS length overflow".into()))?;
    let sps = parse_nal(config.get(8..sps_end).ok_or_else(|| RtlError::Format("truncated H.264 SPS".into()))?)?;
    let pps_count_position = sps_end;
    let pps_count = usize::from(*config.get(pps_count_position).ok_or_else(|| RtlError::Format("truncated H.264 PPS count".into()))?);
    if pps_count == 0 { return Err(RtlError::Format("H.264 avcC contains no PPS".into())); }
    let pps_length_position = pps_count_position + 1;
    let pps_length = usize::from(u16::from_be_bytes([
        *config.get(pps_length_position).ok_or_else(|| RtlError::Format("truncated H.264 PPS length".into()))?,
        *config.get(pps_length_position + 1).ok_or_else(|| RtlError::Format("truncated H.264 PPS length".into()))?,
    ]));
    let pps_start = pps_length_position + 2;
    let pps = parse_nal(config.get(pps_start..pps_start + pps_length).ok_or_else(|| RtlError::Format("truncated H.264 PPS".into()))?)?;
    if sps.kind != NalKind::SequenceParameterSet || pps.kind != NalKind::PictureParameterSet {
        return Err(RtlError::Format("H.264 avcC parameter-set types are invalid".into()));
    }
    Ok((sps, pps))
}