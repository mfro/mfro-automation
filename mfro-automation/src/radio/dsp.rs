//! Digital Signal Processing utilities

use std::f32::consts::PI;

use rustradio::{Complex, fir, window::WindowType};

/// Read a signal from interleaved complex 8 bit unsigned data
pub fn read_signal(src: &[u8]) -> Vec<Complex> {
    src.chunks(2)
        .map(|pair| Complex {
            re: (pair[0] as f32 - 128.0) / 128.0,
            im: (pair[1] as f32 - 128.0) / 128.0,
        })
        .collect()
}

pub fn write_signal(src: &[Complex]) -> Vec<u8> {
    src.iter()
        .flat_map(|c| [c.re, c.im])
        .map(|f| (f * 128.0 + 128.0) as u8)
        .collect()
}

/// Remix a signal to a be centered at a given frequency
pub fn mix(input: &[Complex], sample_rate: f32, frequency: f32) -> Vec<Complex> {
    input
        .iter()
        .enumerate()
        .map(|(index, value)| {
            let phase = -2.0 * PI * frequency * index as f32 / sample_rate;
            value * Complex::from_polar(1.0, phase)
        })
        .collect()
}

/// Create a convolution kernel for a narrow-band filter
pub fn low_pass(
    sample_rate: f32,
    bandwidth: f32,
    window_type: &WindowType,
    twidth: f32,
) -> Vec<f32> {
    fir::low_pass(sample_rate, bandwidth, twidth, &window_type)
}

pub fn convolve<A, B, C>(a: &[A], b: &[B]) -> impl Iterator<Item = C>
where
    A: Copy + std::ops::Mul<B, Output = C>,
    B: Copy,
    C: std::iter::Sum,
{
    a.windows(b.len()).map(|window| {
        window
            .iter()
            .rev()
            .zip(b.iter())
            .map(|(a, b)| *a * *b)
            .sum::<C>()
    })
}

pub fn filter(data: &[Complex], kernel: &[f32]) -> Vec<f32> {
    convolve(&data, &kernel)
        .map(|v| v.norm_sqr())
        .collect::<Vec<_>>()
}
