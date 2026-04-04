use std::{fs::File, io::Read, thread};

use anyhow::Result;
// use bluer::monitor::{Monitor, MonitorEvent, RssiSamplingPeriod};
// use futures_util::StreamExt;
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

// #[tokio::main]
// #[allow(dead_code)]
// async fn test() -> Result<()> {
//     const APPLE: u16 = 0x004c;
//     const PAYLOAD_REGISTERED: u8 = 0x12;
//     const PAYLOAD_UNREGISTERED: u8 = 0x07;

//     let patterns = vec![
//         bluer::monitor::Pattern {
//             data_type: bluer::monitor::data_type::FLAGS,
//             start_position: 0x00,
//             content: vec![0x06],
//         },
//         bluer::monitor::Pattern {
//             data_type: bluer::monitor::data_type::FLAGS,
//             start_position: 0x00,
//             content: vec![0x1a],
//         },
//     ];

//     let session = bluer::Session::new().await?;
//     let adapter = session.default_adapter().await?;

//     adapter.set_powered(true).await?;

//     let monitor = adapter.monitor().await?;
//     let config = Monitor {
//         monitor_type: bluer::monitor::Type::OrPatterns,
//         patterns: Some(patterns),
//         rssi_sampling_period: Some(RssiSamplingPeriod::All),
//         ..default()
//     };

//     let mut handle = monitor.register(config).await?;

//     println!("begin monitoring");

//     while let Some(event) = &handle.next().await {
//         if let MonitorEvent::DeviceFound(id) = event {
//             println!("{}", id.device);
//             let device = adapter.device(id.device)?;
//             for prop in device.all_properties().await? {
//                 match prop {
//                     bluer::DeviceProperty::ManufacturerData(map) => {
//                         println!("  {:?}", map);
//                         if let Some(v) = map.get(&APPLE) {
//                             if v.len() >= 27
//                                 && v[0] == PAYLOAD_REGISTERED
//                                 && v[1] == 25
//                                 && v[26] == 0x00
//                             {
//                                 let status = v[2];
//                                 println!("  {:02x}", status)
//                             }
//                         }
//                     }
//                     _ => {}
//                 }
//             }
//         }
//     }

//     Ok(())
// }

// watch: f4:cb:e7:d4:43:a2
// airpods: 40:b3:fa:45:85:5d
// iphone 54:09:10:96:8f:0e
// michelle airpods: 40:b3:fa:45:85:5d
