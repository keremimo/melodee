#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kerem Kilic (Ellic Studio)
"""CPU-run USB capture with explicit stereo/stem layout and host evidence.

The existing device USB layout must already match. This never changes device
settings or reconnects USB. Retains the duplex capture approach from
Melodee's audio_performance_device.py and fd-2 capture from audio_cpu_stress.py.
"""
import contextlib
import json
import os
import sys
import time
import wave


@contextlib.contextmanager
def capture_driver_stderr(path):
    """Capture native driver output, including open/close errors."""
    sys.stderr.flush()
    saved = os.dup(2)
    try:
        with path.open('wb') as output:
            os.dup2(output.fileno(), 2)
            try:
                yield
            finally:
                sys.stderr.flush()
                os.dup2(saved, 2)
    finally:
        os.close(saved)


@contextlib.contextmanager
def usb_streams(path, format='int24', mode='stereo'):
    import sounddevice as sd
    if mode not in ('stereo', 'stems') or format not in ('int16', 'int24'):
        raise ValueError('USB capture mode stereo/stems and format int16/int24')
    channels = 4 if mode == 'stems' else 2
    devices = sd.query_devices()
    inputs = [i for i, d in enumerate(devices) if d['name'] in ('Melodee', 'Melodee In')
              and d['max_input_channels'] == channels]
    outputs = [i for i, d in enumerate(devices) if d['name'] in ('Melodee', 'Melodee Out')
               and d['max_output_channels'] >= 2]
    if not inputs or not outputs:
        raise RuntimeError(f'USB {mode} requires an existing {channels}-channel Melodee input and stereo output; configure the device before this run')
    inp, out = inputs[0], outputs[0]
    statuses = []
    evidence = dict(mode=mode, format=format, channels=channels, samplerate=44100,
                    input_device=inp, output_device=out, capture_frames=0, playback_frames=0,
                    opened=False, closed=False)
    try:
        with wave.open(str(path), 'wb') as wav:
            wav.setnchannels(channels);wav.setsampwidth(2 if format == 'int16' else 3);wav.setframerate(44100)
            def capture(data, frames, timing, status):
                if status:
                    statuses.append(str(status))
                wav.writeframesraw(bytes(data))
                evidence['capture_frames'] += frames
            def playback(data, frames, timing, status):
                if status:
                    statuses.append(str(status))
                data[:] = bytes(len(data))
                evidence['playback_frames'] += frames
            with sd.RawInputStream(device=inp, channels=channels, samplerate=44100, dtype=format, callback=capture), \
                 sd.RawOutputStream(device=out, channels=2, samplerate=44100, dtype=format, callback=playback):
                evidence['opened'] = True
                time.sleep(1.3)  # alternate/feedback negotiation is outside measured windows
                yield statuses
        evidence['closed'] = True
    finally:
        evidence['callback_statuses'] = list(statuses)
        path.with_suffix('.status.json').write_text(json.dumps(evidence, indent=2)+'\n')


def capture_result(path, mode):
    """Check actual capture/duplex callbacks after all stream shutdown work."""
    if mode == 'inactive':
        return dict(required=False, passed=True)
    result = dict(required=True, passed=False)
    try:
        evidence = json.loads(path.with_suffix('.status.json').read_text())
        with wave.open(str(path), 'rb') as wav:
            channels, frames, rate = wav.getnchannels(), wav.getnframes(), wav.getframerate()
            width, data_bytes = wav.getsampwidth(), 0
            while data := wav.readframes(8192):
                data_bytes += len(data)
        expected = 4 if mode == 'stems' else 2
        result.update(evidence, wave_channels=channels, wave_frames=frames, wave_data_bytes=data_bytes)
        result['passed'] = bool(evidence['mode'] == mode and evidence['channels'] == expected == channels
            and evidence['opened'] and evidence['closed'] and frames == evidence['capture_frames'] > 0
            and evidence['playback_frames'] > 0 and data_bytes == frames*channels*width
            and width == (3 if evidence['format'] == 'int24' else 2) and rate == evidence['samplerate'] == 44100
            and not evidence['callback_statuses'])
    except (OSError, ValueError, KeyError, TypeError, wave.Error) as error:
        result['error'] = f'{type(error).__name__}: {error}'
    return result
