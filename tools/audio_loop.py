# SPDX-License-Identifier: GPL-2.0-or-later
"""Records what the speakers play (WASAPI loopback of the default output) to a WAV, for tools\\record.ps1.

  python -I tools/audio_loop.py OUT.wav STOPFILE

Runs until STOPFILE exists. Prints one line `audio: start EPOCH rate R ch C` once the first buffer is in:
EPOCH = wall-clock time (time.time) of the first recorded sample, so record.ps1 can line it up with the
video's first frame (ffmpeg RTCTIME, the same clock). WASAPI loopback delivers nothing while no sound
plays, which would shift everything after a pause; a silent output stream keeps it delivering.
Needs PyAudioWPatch (`python -m pip install --user PyAudioWPatch`).
"""
import os
import sys
import threading
import time
import wave

import pyaudiowpatch as pyaudio

FRAMES = 1024


def main():
    out, stop = sys.argv[1], sys.argv[2]
    pa = pyaudio.PyAudio()
    dev = pa.get_default_wasapi_loopback()
    rate, ch = int(dev["defaultSampleRate"]), dev["maxInputChannels"]

    # Keep loopback delivering during silence: play zeros on the same output.
    spk = pa.get_device_info_by_index(pa.get_host_api_info_by_type(pyaudio.paWASAPI)["defaultOutputDevice"])
    # WASAPI shared mode takes only the device's own mix format (channels, rate).
    keep = pa.open(format=pyaudio.paInt16, channels=spk["maxOutputChannels"],
                   rate=int(spk["defaultSampleRate"]), output=True, output_device_index=spk["index"])
    zeros = bytes(2 * spk["maxOutputChannels"] * FRAMES)
    running = True

    def silence():
        while running:
            keep.write(zeros)

    threading.Thread(target=silence, daemon=True).start()

    inp = pa.open(format=pyaudio.paInt16, channels=ch, rate=rate, input=True,
                  input_device_index=dev["index"], frames_per_buffer=FRAMES)
    wav = wave.open(out, "wb")
    wav.setnchannels(ch)
    wav.setsampwidth(2)
    wav.setframerate(rate)
    first = True
    while not os.path.exists(stop):
        data = inp.read(FRAMES, exception_on_overflow=False)
        if first:
            t0 = time.time() - FRAMES / rate
            print(f"audio: start {t0:.6f} rate {rate} ch {ch}", flush=True)
            first = False
        wav.writeframes(data)
    running = False
    wav.close()
    inp.close()
    keep.close()
    pa.terminate()


if __name__ == "__main__":
    main()
