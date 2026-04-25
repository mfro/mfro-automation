use std::{fs::File, io::Read, thread};

use anyhow::Result;
use rouille::Response;
use serde::{Deserialize, Serialize};

use crate::{garbage_schedule::GarbageSchedule, gateway::GatewayClient, prelude::*};

mod garbage_schedule;
mod gateway;
mod radio;
mod util;

mod prelude {
    pub use crate::util::*;
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
struct Config {
    port: u16,
    authentication_token: String,

    radio: radio::Config,
    garbage: garbage_schedule::Config,
}

fn load_config(path: &str) -> Result<Config> {
    let mut data = vec![];
    File::open(path)?.read_to_end(&mut data)?;

    let config = serde_json::from_slice(&data)?;
    Ok(config)
}

fn run() -> Result<()> {
    let config_path = std::env::args()
        .skip(1)
        .next()
        .expect("Expected config file name as an argument");

    let config = load_config(&config_path)?;

    let radio_config = config.radio;
    thread::spawn(|| radio::run(radio_config).unwrap());

    let garbage = GarbageSchedule::new(config.garbage);
    let gateway = GatewayClient::new();

    let addr = ("0.0.0.0", config.port);
    rouille::start_server(addr, move |request| {
        if request.url() == "/garbage.ics" {
            garbage.serve_ics(request)
        } else if request.url() == "/pc_power"
            && request.method() == "POST"
            && request
                .header("authorization")
                .is_some_and(|v| v == config.authentication_token)
        {
            match gateway.trigger_pc_power() {
                Ok(()) => empty(200),
                Err(e) => {
                    eprintln!("{:?}", e);
                    empty(500)
                }
            }
        } else if request.url() == "/garage_door"
            && request.method() == "POST"
            && request
                .header("authorization")
                .is_some_and(|v| v == config.authentication_token)
        {
            match gateway.trigger_garage_door() {
                Ok(()) => empty(200),
                Err(e) => {
                    eprintln!("{:?}", e);
                    empty(500)
                }
            }
        } else {
            Response::empty_404()
        }
    });
}

fn main() -> Result<()> {
    run()
}
