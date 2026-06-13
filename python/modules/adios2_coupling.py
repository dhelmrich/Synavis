# Adios2Coupling Example
# ======================
# This script demonstrates ADIOS2 coupling using Synavis for UE5 data streaming.
# Unlike the extraction.py example which handles various frame formats (YUV, RGB with stride, etc.),
# ADIOS2 transmits raw RGB data directly from SceneCaptureComponent2D.
#
# ADIOS2 Configuration for BP4 File-Based Coupling:
# -------------------------------------------------
# This example uses BP4 explicitly, which stores stream data in a .bp file set.
# Python and UE must use the same IO name, variable name, and filename prefix.
#   - EngineType: "BP4"
#   - FilenamePrefix: Output file prefix (e.g., "synavis_output")
#   - IOName: "AdiosIO"
#   - Variable: "data" (uint8_t)
#
# BP4 Notes:
# - BP4 is file-based and better suited to deterministic file output and replay.
# - Network interface and port settings are not required for BP4.
# - UE writer and Python reader must point to the same output prefix/path.
#
# See SynavisUEBlank\Plugins\Adios2Backend for UE5 integration details
# See Synavis\synavis\adios\AdiosConnector for C++ implementation details
#
# Autodiscovery Modes:
# -------------------
# 1. LOCAL CONFIGURATION (simplest for testing):
#    - No signaling server required
#    - Use: m.SetNetworkInterface("localhost")
#    - Use: m.SetPort(9001)
#
# 2. SIGNALING ASSISTED (auto-discovery):
#    - Uses existing signaling_server.py
#    - UE broadcasts ADIOS2 config via signaling channel
#    - Python client receives and applies configuration
#    - See adios2_signaling_client.py for full implementation

# LOCAL CONFIGURATION EXAMPLE:
# ---------------------------
# m.SetNetworkInterface("localhost")
# m.SetPort(9001)

# synenv is located in

import numpy as np
import time
import sys
import os
import json
import matplotlib
import matplotlib.pyplot as plt

plt.ion()

# Plotting state (must be global for matplotlib)
_plt_fig = None
_plt_ax = None
_plt_img = None

olddir = os.getcwd()
os.chdir(os.path.dirname(os.path.abspath(__file__)))

if os.name == "nt":
    sys.path.append(r"C:/work/WebRTCCoupling/Synavis/build_win/synavis/Release/")
else:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    unix_build = os.path.abspath(os.path.join(script_dir, "../../build_unix"))
    sys.path.append(unix_build)

import PySynavis as syn

syn.SetGlobalLogVerbosity(syn.LogVerbosity.LogVerbose)
pylog = syn.Logger()
pylog.setidentity("Adios2Coupling")
pylog.rotateLogFile("adios2_coupling.log")

pylog.log("Starting Adios2 coupling module")

HEIGHT = 1080
WIDTH = 1920

from queue import Queue, Empty

frame_queue = Queue(maxsize=8)

message_buffer = []

LOG_INTERVAL = 5.0


def reset_message():
    global message_buffer
    message_buffer = []


def get_message():
    global message_buffer
    while len(message_buffer) == 0:
        time.sleep(0.1)
    message = message_buffer.pop(0)
    return message


def message_callback(msg):
    global message_buffer
    if isinstance(msg, (bytes, bytearray)):
        s = msg.decode("utf-8", errors="replace")
    else:
        s = str(msg)
    pylog.log(f"Received message: {s}")
    message_buffer.append(s)


def data_callback(data):
    pylog.log("Received raw data packet of length {}".format(len(data)))


def frame_callback(frame, info=None):
    pylog.log(
        f"Received frame of type {type(frame)} with size {len(frame.Data) if hasattr(frame, 'Data') else 'N/A'} bytes"
    )
    if info is not None:
        pylog.log(
            f"Frame info: timestamp={getattr(info, 'timestamp', None)}, width={getattr(info, 'width', None)}, height={getattr(info, 'height', None)}"
        )

    if not isinstance(frame, (bytes, bytearray)):
        w = getattr(frame, "Width", None) or getattr(frame, "width", None) or WIDTH
        h = getattr(frame, "Height", None) or getattr(frame, "height", None) or HEIGHT
        ts = getattr(frame, "Timestamp", None) or (
            getattr(info, "timestamp", None) if info is not None else None
        )

        packed = getattr(frame, "Packed", False)

        if packed:
            pylog.log("Frame is packed: raw RGB format from ADIOS2")
            raw = bytes(frame.Data)
        else:
            pylog.log("Frame is not packed: raw RGB format from ADIOS2")
            raw = bytes(frame.Data)

        pylog.log(
            f"Interpreting frame: width={w} height={h} packed={packed} raw_size={len(raw)}"
        )

        rgb = None
        if len(raw) == int(w) * int(h) * 3:
            pylog.log("Frame data matches tightly-packed RGB size")
            rgb = np.frombuffer(raw, dtype=np.uint8).reshape((int(h), int(w), 3)).copy()
        elif len(raw) == int(w) * int(h):
            pylog.log("Frame data is grayscale; converting to RGB")
            g = np.frombuffer(raw, dtype=np.uint8).reshape((int(h), int(w)))
            rgb = np.stack([g, g, g], axis=-1)
        else:
            pylog.log(
                f"Unsupported frame layout: size={len(raw)} w={w} h={h} packed={packed}"
            )

        if rgb is not None:
            pylog.log(f"Enqueuing frame for plotting: ts={ts} rgb_shape={rgb.shape}")
            frame_queue.put((rgb, ts))


m = syn.AdiosConnector()

m.Initialize()
m.SetEngineType("BP4")  # BP4 for file-based coupling
m.SetIOName("AdiosIO")
m.SetVariableName("SynavisData")
m.SetFilenamePrefix("synavis_output")

# BP4 does not use network interface/port parameters.

# Add the variable that UE will send (must match Adios2State.cpp line 63)
# Variables must be defined BEFORE StartStreaming() so they're available when engine opens
m.AddVariable("data", "uint8_t")

m.StartStreaming()

m.SetReadCallback(frame_callback)
m.StartReader()
m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
m.SetOnConnectedCallback(lambda: pylog.log("ADIOS2 BP4 streaming started"))
m.SetOnClosedCallback(lambda: syn.ExitWithMessage("ADIOS2 BP4 streaming stopped", 2))
m.LockUntilConnected(2000)

pylog.log("ADIOS2 BP4 streaming initialized.")

# Send initial JSON configuration/command to UE side
# This mirrors the pattern from extraction.py lines 331, 350-365
initial_commands = [
    {"type": "query"},
    {"type": "command", "name": "start"},
]

for cmd in initial_commands:
    pylog.log(f"Sending JSON command: {cmd}")
    m.SendJSON(cmd)
    time.sleep(0.2)

while True:
    try:
        rgb, ts = frame_queue.get(timeout=0.1)
        if _plt_fig is None:
            _plt_fig, _plt_ax = plt.subplots()
            _plt_img = _plt_ax.imshow(rgb)
            _plt_ax.set_title(f"ts={ts}")
            _plt_fig.canvas.draw()
            plt.show(block=False)
        else:
            _plt_img.set_data(rgb)
            _plt_ax.set_title(f"ts={ts}")
            _plt_fig.canvas.draw_idle()
        plt.pause(0.001)
    except Empty:
        time.sleep(0.05)
