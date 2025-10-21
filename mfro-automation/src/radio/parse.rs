use bitvec::prelude::*;
use crc::{Algorithm, Crc};

use crate::prelude::*;

const HONEYWELL_5816_CRC: Algorithm<u16> = crc::Algorithm {
    width: 16,
    poly: 0x8005,
    init: 0x0000,
    refin: false,
    refout: false,
    xorout: 0,
    check: 0xaee7,
    residue: 0,
};

pub struct MessageParser {
    reset_time: usize,
    crc: Crc<u16>,
}

impl MessageParser {
    pub fn new(sample_rate: f32) -> Self {
        let reset_time = (sample_rate * 0.002) as usize;
        let crc = Crc::<u16>::new(&HONEYWELL_5816_CRC);

        Self { reset_time, crc }
    }

    fn get_bits(&self, data: &[f32]) -> Option<BitVec<u8, Msb0>> {
        let gradient = data
            .windows(2)
            .map(|v| v[1] - v[0])
            .chain([0.0])
            .collect::<Vec<_>>();

        let mut sorted = gradient.to_vec();
        sorted.sort_by(f32::total_cmp);

        let rising = sorted[(sorted.len() as f32 * 0.96) as usize];
        let falling = sorted[(sorted.len() as f32 * 0.04) as usize];

        // let mut csv = BufWriter::new(File::create("out.csv").unwrap());
        // for (index, value) in data.iter().enumerate() {
        //     writeln!(
        //         csv,
        //         "{},{},{},{}",
        //         value,
        //         gradient[index] * 5.0,
        //         rising * 5.0,
        //         falling * 5.0,
        //     )
        //     .unwrap();
        // }

        let mut sync = Vec::with_capacity(15);

        let mut origin = 0;
        while sync.len() < 15 {
            let next = origin + gradient[origin..].iter().position(|raw| *raw > rising)?;
            sync.push(next);

            origin = next + gradient[next..].iter().position(|raw| *raw < falling)?;

            if origin - next > self.reset_time {
                sync.clear();
                origin = next + self.reset_time;
            }
        }

        let clock = (sync.last().unwrap() - sync.first().unwrap()) as f32 / 14.0;
        let half = clock as usize / 2;

        // let mut csv = BufWriter::new(File::create("out.csv").unwrap());
        // for (index, value) in data.iter().enumerate() {
        //     writeln!(
        //         csv,
        //         "{},{},{},{},{}",
        //         value,
        //         gradient[index] * 5.0,
        //         rising * 5.0,
        //         falling * 5.0,
        //         sync.contains(&index)
        //             || index > origin && ((index - origin) as f32 % clock).abs() < 2.0
        //     )
        //     .unwrap();
        // }

        if origin + (clock * 48.0) as usize + half > data.len() {
            eprintln!("{} {}", origin + (clock * 49.0) as usize + half, data.len());
            None
        } else {
            let bits = (0..48)
                .map(|i| {
                    let center = origin + (clock * (i + 1) as f32) as usize;
                    let b1 = &data[center - half..center].iter().mean();
                    let b2 = &data[center..center + half].iter().mean();

                    b2 > b1
                })
                .collect();

            return Some(bits);
        }
    }

    pub fn parse(&self, data: &[f32]) -> Option<u32> {
        let bits = self.get_bits(data)?;

        let body: u32 = bits[..32].load_be();
        let crc_check: u16 = bits[32..].load_be();

        let mut digest = self.crc.digest();
        digest.update(&body.to_be_bytes());

        let crc_computed = digest.finalize();

        if crc_check == crc_computed {
            Some(body)
        } else {
            eprintln!("CRC failed: {:x} {:x} {:x}", body, crc_check, crc_computed);
            None
        }
    }
}

#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct Message {
    pub channel: u8,
    pub device_id: u32,
    pub event: u8,
    pub contact: bool,
    pub tamper: bool,
    pub reed_open: bool,
    pub alarm: bool,
    pub low_battery: bool,
    pub heartbeat: bool,
}

impl Message {
    pub fn parse(body: u32) -> Message {
        let channel = ((body >> 28) & 0xf) as u8;
        let id = (body >> 8) & 0xfffff;
        let event = (body & 0xff) as u8;

        let contact = (event & 0x80) != 0;
        let tamper = (event & 0x40) != 0;
        let reed = (event & 0x20) != 0;
        let alarm = (event & 0x10) != 0;
        let battery = (event & 0x08) != 0;
        let heartbeat = (event & 0x04) != 0;

        Self {
            channel,
            device_id: id,
            event,
            contact,
            tamper,
            reed_open: reed,
            alarm,
            low_battery: battery,
            heartbeat,
        }
    }
}
