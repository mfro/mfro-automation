mod util;

use std::{cell::RefCell, collections::VecDeque};

use anyhow::Result;
use bt_hci::{
    ControllerToHostPacket, HostToControllerPacket, WriteHci,
    cmd::le::LeSetScanParams,
    controller::{ControllerCmdSync, ExternalController},
    param::LeAdvReportsIter,
    transport::WithIndicator,
};
use bt_hci_linux::Transport;
use embassy_futures::join::join;
use embassy_time::{Duration, Timer};
use futures_util::StreamExt as _;
use tokio::{join, time::sleep};
use trouble_host::prelude::*;

use crate::util::debug_addr;

/// Max number of connections
const CONNECTIONS_MAX: usize = 1;
const L2CAP_CHANNELS_MAX: usize = 1;

struct X {
    inner: Transport,
}

impl embedded_io::ErrorType for X {
    type Error = bt_hci_linux::Error;
}

impl bt_hci::transport::Transport for X {
    async fn read<'a>(&self, rx: &'a mut [u8]) -> Result<ControllerToHostPacket<'a>, Self::Error> {
        let packet = self.inner.read(rx).await;
        // println!("read: {:02x?}", packet);
        packet
    }

    async fn write<T: HostToControllerPacket>(&self, val: &T) -> Result<(), Self::Error> {
        let mut buf = Vec::<u8>::new();
        WithIndicator::new(val).write_hci(&mut buf).unwrap();

        let packet = self.inner.write(val).await;
        // println!("write: {:?} {:02x?}", val.size(), buf);
        packet
    }
}

#[tokio::main]
#[allow(dead_code)]
async fn main() -> Result<()> {
    let transport = Transport::new(0)?;
    let controller = ExternalController::<_, 16>::new(X { inner: transport });

    run(controller).await;

    // const APPLE: u16 = 0x004c;
    // const PAYLOAD_REGISTERED: u8 = 0x12;
    // const PAYLOAD_UNREGISTERED: u8 = 0x07;

    // let patterns = vec![
    //     bluer::monitor::Pattern {
    //         data_type: bluer::monitor::data_type::MANUFACTURER_SPECIFIC_DATA,
    //         start_position: 0x00,
    //         content: vec![0x4c, 0x00],
    //     },
    // ];

    // let session = bluer::Session::new().await?;
    // let adapter = session.default_adapter().await?;

    // adapter.set_powered(true).await?;

    // let config = Monitor {
    //     monitor_type: bluer::monitor::Type::OrPatterns,
    //     patterns: Some(patterns),
    //     rssi_sampling_period: Some(RssiSamplingPeriod::All),
    //     ..default()
    // };

    // let monitor = adapter.monitor().await?;
    // let mut handle = monitor.register(config).await?;

    // println!("begin monitoring");

    // while let Some(event) = &handle.next().await {
    //     println!("{:02x?}", event);
    //     if let MonitorEvent::DeviceFound(id) = event {
    //         println!("{}", id.device);
    //         let device = adapter.device(id.device)?;
    //         println!("{:02x?}", device.advertising_data().await?);
    //         // for prop in device.all_properties().await? {
    //         //     match prop {
    //         //         bluer::DeviceProperty::ManufacturerData(map) => {
    //         //             println!("  {:?}", map);
    //         //             if let Some(v) = map.get(&APPLE) {
    //         //                 if v.len() >= 27
    //         //                     && v[0] == PAYLOAD_REGISTERED
    //         //                     && v[1] == 25
    //         //                     && v[26] == 0x00
    //         //                 {
    //         //                     let status = v[2];
    //         //                     println!("  {:02x}", status)
    //         //                 }
    //         //             }
    //         //         }
    //         //         _ => {}
    //         //     }
    //         // }
    //     }
    // }

    Ok(())
}

pub async fn run<C>(controller: C)
where
    C: Controller + ControllerCmdSync<LeSetScanParams>,
{
    // Using a fixed "random" address can be useful for testing. In real scenarios, one would
    // use e.g. the MAC 6 byte array as the address (how to get that varies by the platform).
    let address: Address = Address::random([0xff, 0x8f, 0x1b, 0x05, 0xe4, 0xff]);

    println!("Our address = {:?}", address);

    let mut resources: HostResources<DefaultPacketPool, CONNECTIONS_MAX, L2CAP_CHANNELS_MAX> =
        HostResources::new();

    let stack = trouble_host::new(controller, &mut resources).set_random_address(address);

    let mut host = stack.build();

    let printer = Printer {};
    let mut scanner = Scanner::new(host.central);

    let t1 = host.runner.run_with_handler(&printer);
    let t2 = async {
        let mut config = ScanConfig::default();
        config.active = false;
        config.phys = PhySet::M1;

        println!("start scan");

        let scan = scanner.scan(&config).await;

        println!("scan done: {}", scan.is_ok());

        loop {
            sleep(std::time::Duration::from_secs(1)).await;
        }
    };

    let (r1, _) = join!(t1, t2);

    r1.unwrap()
}

struct Printer {}

impl EventHandler for Printer {
    fn on_adv_reports(&self, mut it: LeAdvReportsIter<'_>) {
        while let Some(Ok(report)) = it.next() {
            if report.data[1..4] == [0xff, 0x4c, 0x00] {
                let id = debug_addr(report.addr);

                println!("{}: {:02x?}", id, &report.data[4..])
            }
        }
    }
}
