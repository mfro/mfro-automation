import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import lfilter, butter, firwin

sample_rate = 2048000

# raw_data = pd.read_csv('out.csv', header=None)
with open('../front_door_345M_2048k.cu8', 'rb') as src:
   raw_data = np.frombuffer(src.read(), dtype=np.uint8).astype(float)

raw_data = (raw_data - 128) / 128.0
input_complex = raw_data[::2] + raw_data[1::2] * 1j

input_complex = input_complex[int(16 * sample_rate):int(16.024 * sample_rate)]

def mix_to_baseband(x, fs, f_offset, start_sample_index=0):
  n = np.arange(len(x)) + start_sample_index
  osc = np.exp(-2j * np.pi * f_offset * n / fs)
  return x * osc

def peak_frequency(iq, fs):
    """
    Find the frequency (Hz) of the strongest component in a block of IQ samples.

    Parameters
    ----------
    iq : np.ndarray
        Complex baseband IQ samples.
    fs : float
        Sample rate in Hz.

    Returns
    -------
    f_peak : float
        Peak frequency in Hz relative to center frequency (0 Hz).
    spectrum : np.ndarray
        Magnitude spectrum (linear scale).
    freqs : np.ndarray
        Frequency axis corresponding to spectrum.
    """
    N = len(iq)

    # FFT and shift so DC is in the middle
    spectrum = np.fft.fftshift(np.fft.fft(iq, n=N))
    mag = np.abs(spectrum)

    # Frequency axis
    freqs = np.fft.fftshift(np.fft.fftfreq(N, d=1/fs))

    # Find peak
    idx_peak = np.argmax(mag)
    f_peak = freqs[idx_peak]

    return f_peak, mag, freqs

def get_intensity(iq, fs=2048000, bw=1000, decim=1, fir_len=129):
    """
    Estimate narrowband intensity (power in dB) at the tuned center frequency (0 Hz).

    Parameters
    ----------
    iq : np.ndarray
        Complex numpy array of IQ samples.
    fs : float
        Sample rate in Hz (default 2048000).
    bw : float
        Bandwidth around 0 Hz to keep (Hz).
    decim : int
        Decimation factor after lowpass filter.
    fir_len : int
        Number of taps in FIR filter.

    Returns
    -------
    times : np.ndarray
        Time stamps (s) for each output sample.
    power_db : np.ndarray
        Power (dB) over time.
    """
    # Design low-pass filter
    taps = firwin(fir_len, bw/(fs/2), window='hamming')

    # Filter
    y = lfilter(taps, 1.0, iq)

    # Decimate (keep every decim-th sample)
    y = y[::decim]

    # Instantaneous power
    power = np.abs(y)**2

    # Time axis
    time = np.arange(len(y)) * decim / fs

    return time, power

amplitude = np.abs(input_complex)
time = np.arange(len(input_complex)) / sample_rate

plt.figure(figsize=(100, 10), dpi=80)
plt.margins(x=0)

b, a = butter(6, [10000 / (sample_rate / 2), 100000 / (sample_rate / 2)], btype='bandpass')
print(b)
print(a)

mean = np.convolve(amplitude, (np.ones(256) / 256), mode='same')

# slide = np.lib.stride_tricks.sliding_window_view(input_complex[16300:18000], 267)
# v1 = []
# v2 = []
# for v in slide:
#     z1 = mix_to_baseband(v, sample_rate, 86471.11111111111)
#     time, power1 = get_intensity(z1, fs=sample_rate)

#     z2 = mix_to_baseband(v, sample_rate, 91022.22222222222)
#     time, power2 = get_intensity(z2, fs=sample_rate)
#   # value = (a2[np.where(a3 == a1)] / np.sum(a2))[0]
#     v1.append(np.mean(power1))
#     v2.append(np.mean(power2))

# f_peak, mag, freqs = peak_frequency(input_complex, sample_rate)
# plt.plot(freqs, mag)

# z1 = mix_to_baseband(input_complex, sample_rate, 48000)
# time, power1 = get_intensity(z1, fs=sample_rate)

# z2 = mix_to_baseband(input_complex, sample_rate, 32000)
# time, power2 = get_intensity(z2, fs=sample_rate)

# f_peak, mag, freqs = peak_frequency(input_complex[16800:17250], sample_rate)
# print(f_peak)
# plt.plot(freqs, mag)

# f_peak, mag, freqs = peak_frequency(input_complex[17400:17850], sample_rate)
# print(f_peak)
# plt.plot(freqs, mag)

debug = np.zeros_like(amplitude)

debug[16800] = 1
debug[17250] = 1

debug[17400] = 1
debug[17850] = 1

# plt.plot(v1)
# plt.plot(v2)

plt.plot(time, amplitude)
plt.plot(time, debug)
plt.plot(time, mean)
# plt.plot(time, power1)
# plt.plot(time, power2)
# # plt.twinx().plot(time, np.sign(power2 - power1), color='C3')
plt.savefig('output3.png', bbox_inches='tight', pad_inches=0)

# fig, axs = plt.subplots(2, 1, figsize=(100*2, 10), dpi=80)

# axs[0].margins(x=0)
# # axs[0].plot(freqs, mag)
# # axs[0].plot(time, power)
# # ax = axs[0].twinx()
# # ax.plot(time, x[::32], color='orange')
# axs[1].margins(x=0)
# # axs[1].plot(time, np.abs(input_complex))

# # tmp1 = np.zeros_like(input_complex)
# # tmp2 = np.zeros_like(input_complex)
# # tmp1[900] = 1
# # tmp1[1100] = 1
# # tmp1[1180] = 1
# # tmp1[1380] = 1
# axs[1].plot(time, power1)
# axs[1].plot(time, power2)
# # axs[1].twinx().plot(time, np.sign(power2 - power1), color='C2')

# sample1 = input_complex[900:1100]
# sample2 = input_complex[1180:1380]
# a1, a2, a3 = peak_frequency(sample1, 2048000)
# b1, b2, b3 = peak_frequency(sample2, 2048000)

# asnda = axs[0].twinx()
# asnda.plot(a3, a2)
# asnda.plot(b3, b2)

# # slide = np.lib.stride_tricks.sliding_window_view(input_complex, 128)[::16]
# # values = []
# # for v in slide:
# #   a1, a2, a3 = peak_frequency(v, 2048000)
# #   value = a1
# #   # value = (a2[np.where(a3 == a1)] / np.sum(a2))[0]
# #   values.append(value)

# # axs[1].twinx().plot(time[::16][:len(values)], values)
# # print(values)
# # print(b2[np.where(b3 == b1)] / np.sum(b2))

# # weights = np.ones(128) / 128
# # sma = np.convolve(np.angle(z1), weights, mode='valid')
# # axs[1].twinx().plot(time[:-127], sma, color='C3')
# # axs[1].twinx().plot(time, np.angle(z), color='C4')
# # axs[1].twinx().plot(time[:-127], -np.sign(np.gradient(sma)), color='C5')
# # axs[1].twinx().plot(time, np.angle(np.exp(-2j * np.pi * f_peak * np.arange(len(input_complex)) / sample_rate)), color='C6')

# fig.tight_layout(rect=[0, 0, 1, 0.96])
# fig.savefig('output3.png', bbox_inches='tight', pad_inches=0)

# # 7. Analyze the IQ signal (optional)
# # You can perform a Fast Fourier Transform (FFT) on the IQ samples
# # to see the spectrum centered at 0 Hz (baseband).
# fft_samples = np.fft.fft(iq_samples)
# fft_frequencies = np.fft.fftfreq(len(iq_samples), 1/sampling_rate)

# plt.figure(figsize=(10, 5))
# plt.plot(fft_frequencies, np.abs(np.fft.fftshift(fft_samples)))
# plt.title('Spectrum of IQ Samples (FFT)')
# plt.xlabel('Frequency (Hz)')
# plt.ylabel('Magnitude')
# plt.grid()
# plt.show()
