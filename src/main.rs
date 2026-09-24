mod bmff;
mod cli;
mod decoded;
mod decoder;
mod error;
mod frames;
mod h264;
mod output;

fn main() {
    if let Err(error) = cli::run() {
        eprintln!("RTL: {error}");
        std::process::exit(1);
    }
}