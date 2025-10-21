use std::{fs::File, io::Read, thread};

use anyhow::Result;
use serde::{Deserialize, Serialize};

mod garbage_schedule;
mod radio;
mod util;

mod prelude {
    pub use crate::util::*;
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
struct Config {
    radio: radio::Config,
    garbage: garbage_schedule::Config,
}

fn load_config(path: &str) -> Result<Config> {
    let mut data = vec![];
    File::open(path)?.read_to_end(&mut data)?;

    let config = serde_json::from_slice(&data)?;
    Ok(config)
}

fn main() -> Result<()> {
    let config_path = std::env::args()
        .skip(1)
        .next()
        .expect("Expected config file name as an argument");

    let config = load_config(&config_path)?;

    let radio_config = config.radio;
    let t1 = thread::spawn(|| radio::run(radio_config).unwrap());

    let garbage_config = config.garbage;
    let t2 = thread::spawn(|| garbage_schedule::run(garbage_config).unwrap());

    t1.join().unwrap();
    t2.join().unwrap();

    Ok(())
}
