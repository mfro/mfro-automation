use std::{
    fs::{self, File},
    io::{BufReader, ErrorKind, prelude::*},
    process::{Command, Stdio},
    time::Duration,
};

use anyhow::Result;
use rumqttc::{Client, MqttOptions};
use serde::{Deserialize, Serialize};

pub mod dsp;
mod parse;
mod scan;

pub use parse::*;
pub use scan::*;

use crate::radio::dsp::write_signal;

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Config {
    sample_rate: f32,

    mqtt_id: String,
    mqtt_host: String,
    mqtt_port: u16,
}

pub fn run(config: Config) -> Result<()> {
    let mqtt = MqttOptions::new(config.mqtt_id, config.mqtt_host, config.mqtt_port);

    let (mqtt, mut eventloop) = Client::new(mqtt, 10);

    std::thread::spawn(move || {
        radio_thread(config.sample_rate, |event| {
            let topic = format!("mfro/honeywell5816/{}", event.device_id);
            let payload = serde_json::to_vec(&event.state).unwrap();

            mqtt.publish(topic, rumqttc::QoS::AtLeastOnce, true, payload)
                .expect("failed to publish to mosquitto");
        })
    });

    for event in eventloop.iter() {
        event?;
    }

    Ok(())
}

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeviceEvent {
    pub device_id: u32,
    pub state: DeviceState,
}

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq, Serialize, Deserialize)]
pub struct DeviceState {
    pub open: bool,
    pub motion: bool,
    pub low_battery: bool,
}

pub fn radio_thread(sample_rate: f32, mut handler: impl FnMut(DeviceEvent)) {
    let rtl_sdr = Command::new("rtl_sdr")
        .args(["-f", "345M", "-s", "250k", "-"])
        .env_clear()
        .stdout(Stdio::piped())
        .spawn()
        .expect("failed to start RTL_SDR");

    let mut src = BufReader::new(rtl_sdr.stdout.unwrap());

    let mut scanner = MessageScanner::new(sample_rate);
    let parser = MessageParser::new(sample_rate);

    let mut next = 0;
    fs::create_dir_all("captures").unwrap();

    loop {
        let broadcast = match scanner.scan(&mut src) {
            Ok(v) => v,
            Err(e) if e.kind() == ErrorKind::UnexpectedEof => break,
            Err(e) => panic!("{:?}", e),
        };

        let data = scanner.extract_signal(&broadcast);

        if let Some(payload) = parser.parse(&data) {
            let message = Message::parse(payload);

            println!("{:?}", message);

            handler(DeviceEvent {
                device_id: message.device_id,
                state: DeviceState {
                    open: message.reed_open,
                    motion: message.motion,
                    low_battery: message.low_battery,
                },
            })
        } else {
            let duration = Duration::from_secs_f32(data.len() as f32 / sample_rate);

            let dst = format!("captures/sample{}_345M_{}k.cu8", next, sample_rate / 1000.0);
            File::create(&dst)
                .unwrap()
                .write_all(&write_signal(&broadcast.data))
                .unwrap();

            eprintln!(
                "Saved unknown broadcast: {:?} {} {}",
                duration, broadcast.frequency, dst
            );

            next = (next + 1) % 100;
        }
    }
}

#[cfg(test)]
#[allow(non_snake_case)]
mod tests {
    use std::{
        fs::File,
        io::{Cursor, prelude::*},
        time::Instant,
    };

    use anyhow::{Result, bail};

    use super::*;

    fn test_sample(filename: &str, sample_rate: f32, messages: &[Option<u32>]) -> Result<()> {
        let src = format!("radio/test-data/{}", filename);

        let mut content = vec![];
        File::open(src)?.read_to_end(&mut content)?;

        let mut src = Cursor::new(content);
        let mut scanner = MessageScanner::new(sample_rate);
        let parser = MessageParser::new(sample_rate);

        let mut expected = messages.iter();

        let t0 = Instant::now();
        for message_index in 0.. {
            let broadcast = match scanner.scan(&mut src) {
                Ok(v) => v,
                Err(e) if e.kind() == ErrorKind::UnexpectedEof => break,
                Err(e) => bail!(e),
            };

            let data = scanner.extract_signal(&broadcast);

            let payload = parser.parse(&data);
            let next_expected = expected.next().unwrap();

            println!("{} {:x?}", message_index, payload);

            if let Some(raw) = payload {
                let message = Message::parse(raw);
                println!("{:?}", message);
            } else {
                assert_eq!(None, *next_expected);

                // fs::create_dir_all("tests-dump")?;

                // let raw_message = raw_data
                //     .iter()
                //     .flat_map(|v| [(v.re * 128.0 + 128.0) as u8, (v.im * 128.0 + 128.0) as u8])
                //     .collect::<Vec<_>>();

                // File::create(format!(
                //     "tests-dump/{}_{}.cu8",
                //     filename.replace(".cu8", ""),
                //     message_index
                // ))?
                // .write_all(&raw_message)?;
            }

            // let mut fft = FftPlanner::new();
            // let fft = fft.plan_fft_forward(1024);

            // let mut csv = BufWriter::new(File::create("out2.csv").unwrap());
            // for index in 0..raw_data.len() - 1024 {
            //     let mut data = raw_data[index..index + 1024].to_vec();
            //     fft.process(&mut data);

            //     writeln!(
            //         csv,
            //         "{}",
            //         data.iter()
            //             .map(|v| v.norm().to_string())
            //             .collect::<Vec<_>>()
            //             .join(",")
            //     )
            //     .unwrap();
            // }

            // break;
        }
        let t1 = Instant::now();

        println!("{:?}", t1 - t0);

        Ok(())
    }

    #[test]
    fn back_door_345M_250k() -> Result<()> {
        test_sample(
            "back_door_345M_250k.cu8",
            250000.0,
            &[
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
            ],
        )
    }

    #[test]
    fn back_door_345M_2048k() -> Result<()> {
        test_sample(
            "back_door_345M_2048k.cu8",
            2048000.0,
            &[
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede12a0),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
                Some(0x8ede1280),
            ],
        )
    }

    #[test]
    fn front_door_345M_250k() -> Result<()> {
        test_sample(
            "front_door_345M_250k.cu8",
            250000.0,
            &[
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                None,
                None,
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
            ],
        )
    }

    #[test]
    fn front_door_345M_2048k() -> Result<()> {
        test_sample(
            "front_door_345M_2048k.cu8",
            2048000.0,
            &[
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6c2),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6e0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                Some(0x8e89b6c0),
                None,
                None,
            ],
        )
    }

    #[test]
    fn rwindow_345M_2048k() -> Result<()> {
        test_sample(
            "rwindow_345M_2048k.cu8",
            2048000.0,
            &[
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
            ],
        )
    }

    #[test]
    fn rwindow_345M_250k() -> Result<()> {
        test_sample(
            "rwindow_345M_250k.cu8",
            250000.0,
            &[
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e3a0),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
                Some(0x8d31e380),
            ],
        )
    }

    #[test]
    fn lwindow_345M_2048k() -> Result<()> {
        test_sample(
            "lwindow_345M_2048k.cu8",
            2048000.0,
            &[
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
            ],
        )
    }

    #[test]
    fn lwindow_345M_250k() -> Result<()> {
        test_sample(
            "lwindow_345M_250k.cu8",
            250000.0,
            &[
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b99a0),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
                Some(0x851b9980),
            ],
        )
    }
}
