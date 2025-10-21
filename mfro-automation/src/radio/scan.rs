use std::{
    io::{self, prelude::*},
    sync::Arc,
};

use ringbuffer::{AllocRingBuffer, RingBuffer};
use rustfft::{Fft, FftPlanner, num_traits::Pow};
use rustradio::{Complex, window::WindowType};

use crate::{
    prelude::*,
    radio::dsp::{filter, low_pass, mix, read_signal},
};

pub struct Broadcast {
    pub data: Vec<Complex>,
    pub frequency: f32,
}

pub struct MessageScanner {
    sample_rate: f32,

    fft_size: usize,
    fft: Arc<dyn Fft<f32>>,

    baseline_buffer: AllocRingBuffer<Vec<f32>>,
    baseline_sum: f32,
    baseline_sum_square: f32,

    filter: Vec<f32>,

    buffer: Vec<u8>,
}

impl MessageScanner {
    pub fn new(sample_rate: f32) -> MessageScanner {
        let bandwidth = 2000.0;

        let fft_size = 2.0.pow((sample_rate / bandwidth).log2().round()) as usize;

        let mut fft = FftPlanner::new();
        let fft = fft.plan_fft_forward(fft_size);

        let baseline_buffer = AllocRingBuffer::new(100);
        let baseline_sum = 0.0;
        let baseline_sum_square = 0.0;

        let filter = low_pass(sample_rate, bandwidth, &WindowType::Hamming, 38000.0);

        let scan_interval = (sample_rate * 0.001) as usize;
        let buffer = vec![0; scan_interval * 2];

        Self {
            sample_rate,
            fft_size,
            fft,
            baseline_buffer,
            baseline_sum,
            baseline_sum_square,
            filter,
            buffer,
        }
    }

    fn read_chunk(&mut self, src: &mut impl Read) -> io::Result<Vec<Complex>> {
        src.read_exact(&mut self.buffer)?;
        let chunk = read_signal(&self.buffer);

        Ok(chunk)
    }

    fn get_spectrum(&self, data: &[Complex], index: usize) -> Vec<f32> {
        let mut scan_sample = data[index..index + self.fft_size].to_vec();
        self.fft.process(&mut scan_sample);

        let normalize = 1.0 / (self.fft_size as f32);
        let intensities = scan_sample
            .iter()
            .map(|v| (v * normalize).norm_sqr())
            .collect::<Vec<_>>();

        intensities
    }

    fn analyze_spectrum(&self, sample: &[f32]) -> (f32, f32) {
        let (peak_index, &peak_intensity) = sample
            .iter()
            .enumerate()
            .max_by(|(_, a), (_, b)| a.total_cmp(&b))
            .unwrap();

        let mean = self.baseline_sum / (self.fft_size * self.baseline_buffer.len()) as f32;
        let mean_square =
            self.baseline_sum_square / (self.fft_size * self.baseline_buffer.len()) as f32;

        let stdv = (mean_square - mean * mean).sqrt();

        let relative_peak_index = if peak_index < self.fft_size / 2 {
            peak_index as f32
        } else {
            peak_index as f32 - self.fft_size as f32
        };

        let peak_frequency = relative_peak_index * self.sample_rate / self.fft_size as f32;

        let peak_zscore = (peak_intensity - mean) / stdv;

        (peak_frequency, peak_zscore)
    }

    pub fn scan(&mut self, mut src: impl Read) -> io::Result<Broadcast> {
        let mut previous = vec![];

        loop {
            let chunk = self.read_chunk(&mut src)?;

            let spectrum = self.get_spectrum(&chunk, 0);

            if self.baseline_buffer.is_full() {
                let (peak_frequency, peak_zscore) = self.analyze_spectrum(&spectrum);

                if peak_zscore > 100.0 {
                    let mut frequencies = vec![peak_frequency];
                    let mut data = [previous, chunk].concat();

                    loop {
                        let mut chunk = self.read_chunk(&mut src)?;

                        let spectrum = self.get_spectrum(&chunk, 0);
                        let (peak_frequency, peak_zscore) = self.analyze_spectrum(&spectrum);

                        data.append(&mut chunk);

                        if peak_zscore < 100.0 {
                            let frequency = frequencies.iter().mean();

                            return Ok(Broadcast { data, frequency });
                        }

                        // if this chunk doesn't match our test, we still add
                        // it to the message in case it has a tiny bit of the
                        // broadcast, but we don't consider its frequency

                        frequencies.push(peak_frequency);
                    }
                }
            }

            self.baseline_sum += spectrum.iter().sum::<f32>();
            self.baseline_sum_square += spectrum.iter().map(|i| i * i).sum::<f32>();

            if let Some(drop) = self.baseline_buffer.enqueue(spectrum) {
                self.baseline_sum -= drop.iter().sum::<f32>();
                self.baseline_sum_square -= drop.iter().map(|i| i * i).sum::<f32>();
            }

            previous = chunk;
        }
    }

    pub fn extract_signal(&self, broadcast: &Broadcast) -> Vec<f32> {
        filter(
            &mix(&broadcast.data, self.sample_rate, broadcast.frequency),
            &self.filter,
        )
    }
}
