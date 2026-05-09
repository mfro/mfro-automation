use std::{fs::File, io::Read, thread};

use anyhow::Result;
use rouille::{Request, Response};
use serde::{Deserialize, Serialize};

use crate::{garbage_schedule::GarbageSchedule, homeassistant::HomeAssistantClient, prelude::*};

mod garbage_schedule;
mod homeassistant;
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

fn auth_check(auth_token: &str, request: &Request) -> bool {
    request
        .header("authorization")
        .is_some_and(|v| v == auth_token)
}

fn try_empty(result: Result<()>) -> Response {
    match result {
        Ok(()) => empty(200),
        Err(e) => {
            eprintln!("{:?}", e);
            empty(500)
        }
    }
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
    let home_assistant = HomeAssistantClient::new();

    let addr = ("0.0.0.0", config.port);
    rouille::start_server(addr, move |request| {
        if request.url() == "/garbage.ics" {
            garbage.serve_ics(request)
        } else if request.url() == "/pc_power"
            && request.method() == "POST"
            && auth_check(&config.authentication_token, request)
        {
            try_empty(home_assistant.trigger_pc_power())
        } else if request.url() == "/garage_door"
            && request.method() == "POST"
            && auth_check(&config.authentication_token, request)
        {
            try_empty(home_assistant.trigger_garage_door())
        } else if request.url() == "/auto_garage_door"
            && request.method() == "POST"
            && auth_check(&config.authentication_token, request)
        {
            try_empty(home_assistant.trigger_auto_garage_door())
        } else {
            Response::empty_404()
        }
    });
}

fn main() -> Result<()> {
    run()
}
