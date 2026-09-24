use std::fmt;
use std::io;

#[derive(Debug)]
pub enum RtlError {
    Usage(String),
    Io(io::Error),
    Format(String),
    Unsupported(String),
}

impl From<io::Error> for RtlError {
    fn from(error: io::Error) -> Self { Self::Io(error) }
}

impl fmt::Display for RtlError {
    fn fmt(&self, output: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Usage(message) | Self::Format(message) | Self::Unsupported(message) => output.write_str(message),
            Self::Io(error) => write!(output, "I/O error: {error}"),
        }
    }
}