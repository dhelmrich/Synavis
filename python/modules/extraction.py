import numpy as np
import threading
import time
import sys
import os
import json
from collections import defaultdict, deque

# set working directory to script directory
olddir = os.getcwd()
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Synavis: Find build
# Prefer platform-appropriate build output directory
if os.name == 'nt':
  sys.path.append(r"C:/work/Synavis/build_win/synavis/Release/")
else:
  # Resolve the unix build path relative to this script
  script_dir = os.path.dirname(os.path.abspath(__file__))
  unix_build = os.path.abspath(os.path.join(script_dir, "../../build_unix"))
  sys.path.append(unix_build)

import PySynavis as syn

syn.SetGlobalLogVerbosity(syn.LogVerbosity.LogWarning)
#syn.VerboseMode(True)
#syn.RegisterAvLogCallback(True)
pylog = syn.Logger()
pylog.setidentity("Synavis Unit Test")
# ensure previous logfile is rotated and a fresh logfile started
pylog.rotateLogFile("extraction.log")

pylog.log("Starting extraction module")

HEIGHT = 256
WIDTH = 256

import matplotlib
import matplotlib.pyplot as plt
plt.ion()
_plt_fig = None
_plt_ax = None
_plt_img = None
from queue import Queue, Empty

# queue for handing frames from worker thread to main thread for plotting
frame_queue = Queue(maxsize=800)

message_buffer = []

# statistics logging interval and accumulator
LOG_INTERVAL = 5.0
STATS = {
  'total_frames': 0,
  'bytes_total': 0,
  'by_resolution': defaultdict(int),
  'by_timestamp': defaultdict(int),
  'frame_times': deque(maxlen=1000),
  'last_log': time.time(),
  'last_total': 0,
  'last_bytes': 0
}

# a method to reset the message buffer
def reset_message() :
  global message_buffer
  message_buffer = []

# a method to get the next message from the buffer
def get_message() :
  global message_buffer
  while len(message_buffer) == 0 :
    time.sleep(0.1)
  message = message_buffer.pop(0)
  return message

# a callback function for the data connector
def message_callback(msg) :
  global message_buffer
  # Normalize message to a str and log using a single argument
  if isinstance(msg, (bytes, bytearray)):
    s = msg.decode('utf-8', errors='replace')
  else:
    s = str(msg)
  pylog.log(f"Received message: {s}")
  message_buffer.append(s)

# a callback function for the data connector
def data_callback(data) :
  pylog.log("Received raw data packet of length {}".format(len(data)))

def frame_callback(frame, info=None) :
  global _plt_fig, _plt_ax, _plt_img
  # log type of frame and size
  pylog.log(f"Received frame of type {type(frame)} with size {len(frame.Data) if hasattr(frame, 'Data') else 'N/A'} bytes, timestamp={getattr(info, 'timestamp', None)}")
  # log frame info if available
  if info is not None:
    pylog.log(f"Frame info: timestamp={getattr(info, 'timestamp', None)}, width={getattr(info, 'width', None)}, height={getattr(info, 'height', None)}")
  # Attempt to interpret and display the received frame (live update)
  if not isinstance(frame, (bytes, bytearray)):
    w = getattr(frame, 'Width', None) or getattr(frame, 'width', None) or WIDTH
    h = getattr(frame, 'Height', None) or getattr(frame, 'height', None) or HEIGHT
    ts = getattr(frame, 'Timestamp', None) or (getattr(info, 'timestamp', None) if info is not None else None)
    # Prefer consumers that support packed frames. If the producer sent a packed YUV420P buffer
    # `frame.Packed` will be True and `frame.Linesize` contains the canonical (tight) strides.
    # Otherwise `frame.Data` contains per-plane rows including FFmpeg stride/padding; use
    # `frame.Linesize` to reconstruct a tightly-packed buffer for downstream code that
    # expects canonical YUV420 layout.
    packed = getattr(frame, 'Packed', False)
    linesize = getattr(frame, 'Linesize', None)
    if packed:
      pylog.log("Frame is packed: assuming tightly-packed YUV420P layout")
      raw = bytes(frame.Data)
    else:
      pylog.log("Frame is not packed: reconstructing tightly-packed buffer using linesize metadata")
      # If linesize is present, first check whether it represents RGB stride (e.g. [3*w,0,0])
      # in which case the frame.Data may already be RGB rows with pitch rather than YUV planes.
      if linesize and len(linesize) >= 1 and int(linesize[0]) >= int(w) * 3 and (len(linesize) < 3 or (int(linesize[1]) == 0 and int(linesize[2]) == 0)):
        pylog.log(f"Linesize indicates RGB/padded rows: {linesize}, attempting RGB row reconstruction")
        # Ensure bytes-like
        if isinstance(frame.Data, (bytes, bytearray, memoryview)):
          srcbuf = bytes(frame.Data)
        else:
          srcbuf = bytes(frame.Data)
        row_stride = int(linesize[0])
        expected = row_stride * int(h)
        if len(srcbuf) >= expected:
          pylog.log(f"RGB with stride present and raw buffer size {len(srcbuf)} is sufficient for expected {expected}, attempting reshape")
          buf = np.frombuffer(srcbuf, dtype=np.uint8)
          try:
            buf = buf.reshape((int(h), row_stride))
            rgb = buf[:, :int(w) * 3].reshape((int(h), int(w), 3)).copy()
            raw = srcbuf  # preserve raw for logging if needed
          except Exception as e:
            pylog.log(f"Failed to reshape RGB stride buffer: {e}")
            raw = srcbuf
        else:
          pylog.log(f"RGB with stride present but raw buffer too small: raw={len(srcbuf)} expected>={expected}")
          raw = srcbuf
      # Otherwise, if linesize has 3 or more elements assume YUV plane rows with stride
      elif linesize and len(linesize) >= 3:
        pylog.log(f"Linesize metadata found: {linesize}")
        ls0, ls1, ls2 = int(linesize[0]), int(linesize[1]), int(linesize[2])
        # plane heights (support odd heights defensively)
        ph0 = int(h)
        ph1 = (int(h) + 1) // 2
        ph2 = ph1
        cw = (int(w) + 1) // 2
        y_size = int(w) * int(h)
        uv_size = cw * ph1
        tight = bytearray(y_size + 2 * uv_size)
        # frame.Data may be bytes-like or a Python list of ints depending on pybind exposure.
        # Ensure we have a bytes-like object before creating a memoryview.
        if isinstance(frame.Data, (bytes, bytearray, memoryview)):
          src = memoryview(frame.Data)
        else:
          # convert list/iterable of ints to bytes
          src = memoryview(bytes(frame.Data))
        off = 0
        dst_off = 0
        # copy Y rows
        for row in range(ph0):
          row_src = src[off: off + ls0]
          tight[dst_off: dst_off + int(w)] = row_src[:int(w)]
          off += ls0
          dst_off += int(w)
        # copy U rows
        for row in range(ph1):
          row_src = src[off: off + ls1]
          tight[dst_off: dst_off + cw] = row_src[:cw]
          off += ls1
          dst_off += cw
        # copy V rows
        for row in range(ph2):
          row_src = src[off: off + ls2]
          tight[dst_off: dst_off + cw] = row_src[:cw]
          off += ls2
          dst_off += cw
        raw = bytes(tight)
      else:
        # No linesize metadata available: fall back to raw bytes as-is
        pylog.log("No linesize metadata available; using raw frame data as-is (may be YUV420P with stride or RGB or grayscale)")
        raw = bytes(frame.Data)
    #
    pylog.log(f"Interpreting frame data: width={w} height={h} packed={packed} linesize={linesize} raw_size={len(raw)}")
    # Deterministic handling: prefer tightly-packed YUV420P (producer sets `Packed=True`).
    # Fall back to RGB (3*w*h) or grayscale (w*h). If none match, log and skip frame.
    rgb = None
    y_size = int(w) * int(h)
    uv_size = (int(w) // 2) * (int(h) // 2)

    # If producer declared packed YUV420P, assume canonical layout and convert directly.
    if getattr(frame, 'Packed', False):
      if len(raw) >= y_size + 2 * uv_size:
        pylog.log("Processing packed YUV420P frame")
        arr = np.frombuffer(raw, dtype=np.uint8)
        Y = arr[0:y_size].reshape((int(h), int(w)))
        U = arr[y_size:y_size + uv_size].reshape((int(h)//2, int(w)//2))
        V = arr[y_size + uv_size:].reshape((int(h)//2, int(w)//2))
        U_up = U.repeat(2, axis=0).repeat(2, axis=1)
        V_up = V.repeat(2, axis=0).repeat(2, axis=1)
        C = Y.astype(np.int32) - 16
        D = U_up.astype(np.int32) - 128
        E = V_up.astype(np.int32) - 128
        R = (298 * C + 409 * E + 128) >> 8
        G = (298 * C - 100 * D - 208 * E + 128) >> 8
        B = (298 * C + 516 * D + 128) >> 8
        rgb = np.stack([np.clip(R, 0, 255), np.clip(G, 0, 255), np.clip(B, 0, 255)], axis=-1).astype(np.uint8)
      else:
        pylog.log(f"Packed frame signalled but buffer too small: got {len(raw)} expected {y_size + 2*uv_size}")
    else:
      # try RGB or grayscale; handle possible per-row stride (pitch) provided via Linesize
      pylog.log("Attempting to interpret frame as RGB or grayscale")
      # Tightly-packed RGB
      if len(raw) == int(w) * int(h) * 3:
        pylog.log("Frame data matches tightly-packed RGB size; interpreting as RGB")
        rgb = np.frombuffer(raw, dtype=np.uint8).reshape((int(h), int(w), 3)).copy()
      # RGB delivered with per-row stride/pitch in linesize[0]
      elif linesize and len(linesize) >= 1 and int(linesize[0]) >= int(w) * 3:
        pylog.log(f"Attempting RGB reconstruction from stride using linesize: {linesize}")
        try:
          row_stride = int(linesize[0])
          expected = row_stride * int(h)
          if len(raw) >= expected:
            buf = np.frombuffer(raw, dtype=np.uint8)
            buf = buf.reshape((int(h), row_stride))
            rgb = buf[:, :int(w) * 3].reshape((int(h), int(w), 3)).copy()
          else:
            pylog.log(f"RGB with stride present but raw buffer too small: raw={len(raw)} expected>={expected}")
        except Exception as e:
          pylog.log(f"Failed to reinterpret stride-RGB buffer: {e}")
      # try grayscale
      elif len(raw) == int(w) * int(h):
        g = np.frombuffer(raw, dtype=np.uint8).reshape((int(h), int(w)))
        rgb = np.stack([g, g, g], axis=-1)
      else:
        pylog.log(f"Unsupported frame layout: size={len(raw)} w={w} h={h} Packed={getattr(frame,'Packed',False)} Linesize={getattr(frame,'Linesize',None)}")
    
    # diagnostic: if the frame is overwhelmingly green, dump the raw data to a file once for offline analysis (e.g. to check if it's actually YUV data with a misinterpretation)
    if rgb is not None:
      green_ratio = np.mean(rgb[:, :, 1]) / (np.mean(rgb) + 1e-6)
      if green_ratio > 1.5:
        pylog.log(f"High green ratio detected: {green_ratio:.2f}, dumping raw frame data for analysis")
        with open("debug_frame_dump.bin", "wb") as f:
          pylog.log(f"Dumping raw frame data of length {len(raw)} bytes to debug_frame_dump.bin")
          f.write(raw)
        syn.ExitWithMessage("High green ratio frame received; dumped raw data for analysis. Exiting.", 3)

    # enqueue frame for plotting in main thread (matplotlib must run in main thread)
    if rgb is not None:
      try:
        pylog.log(f"Enqueuing frame for plotting: ts={ts} rgb_shape={rgb.shape}")
        frame_queue.put_nowait((rgb, ts))
      except Exception as e:
        pylog.log("Frame queue is full; dropping frame. Exception: {}".format(e))
        # drop frame if queue is full
        pass

m = syn.MediaReceiver()
f = syn.FrameDecode(syn.Codec.VP9)
f.OutputMode = syn.OutputMode.PackedRGB
f.SetAcceptOnlyKeyframes(True)

m.Initialize()
#Media.SetConfigFile("config.json")
m.SetConfig({"SignallingIP": "172.21.96.1","SignallingPort":8080})
m.SetTakeFirstStep(False)
m.StartSignalling()
# Register callbacks
m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
# Create the acceptor and wrap it to log return values (so we can detect when it returns False)
_acceptor = f.CreateAcceptor(frame_callback)
def _acceptor_wrapper(data, info=None):
  size = len(data) if hasattr(data, '__len__') else -1
  pylog.log(f"Acceptor invoked: incoming size={size} ts={getattr(info, 'timestamp', None)}")
  ret = _acceptor(data, info)
  pylog.log(f"Acceptor returned: {ret} for size={size} ts={getattr(info, 'timestamp', None)}")
  return ret

# Register the single frame reception callback (acceptor wrapper)
m.SetFrameReceptionCallback(_acceptor_wrapper)


m.SetOnTrackOpenCallback(lambda: pylog.log("Track opened"))
# exit Python when the incoming track closes
m.SetOnTrackCloseCallback(lambda: syn.ExitWithMessage("Track closed", 1))
# exit Python when the data channel closes
m.SetOnClosedCallback(lambda: syn.ExitWithMessage("Data channel closed", 2))
m.SetRetryOnErrorResponse(True)
m.LockUntilConnected(2000)

pylog.log("Connected to media sender.")

# attempt to choose a sensible default datachannel (one that contains 'handler')
names = m.GetDataChannelNames()
pylog.log(f"Available datachannels: {names}")
for nm in names:
  if "handler" in nm.lower():
    if m.SelectDataChannelByName(nm):
      pylog.log(f"Selected datachannel '{nm}' as default")
      break

cnt = m.NumRemoteMedia()
pylog.log(f"NumRemoteMedia={cnt}")
for i in range(cnt):
  desc = m.RemoteMediaDescription(i)
  pylog.log(f"RemoteMediaDescription[{i}]={desc}")
  f.ParseDescription(desc)
pylog.log("Called FrameDecode.ParseDescription for all remote media")

# Helper: poll message buffer for the actor list response
def poll_for_actor_list(timeout=2.0):
  start = time.time()
  while time.time() - start < timeout:
    # iterate over a copy to allow removal
    for idx, raw in enumerate(list(message_buffer)):
      obj = json.loads(raw)
      if obj.get("type") == "query" and obj.get("name") == "all" and isinstance(obj.get("data"), list):
        # remove the matched entry from the real buffer
        for j in range(len(message_buffer)):
          if message_buffer[j] == raw:
            message_buffer.pop(j)
            break
        return obj.get("data")
    time.sleep(0.1)
  return None


def resolve_actor(prefix, timeout=2.0):
  # ask the scene for all actor names, then find one that contains the prefix
  m.SendJSON({"type": "query"})
  names = poll_for_actor_list(timeout)
  if not names:
    return None
  for n in names:
    if prefix.lower() in n.lower():
      return n
  return None


# firing a few test messages (small suite to exercise scene commands/queries)
# first resolve SceneCam actual name (UE may decorate names)
resolved_camera = resolve_actor("SceneCam", timeout=2.0)
if resolved_camera:
  pylog.log("Resolved SceneCam -> {}".format(resolved_camera))
else:
  resolved_camera = "SceneCam"

tests = [
  {"type": "query"},
  #{"type": "query", "object": resolved_camera, "property": "Position"},
  #{"type": "command", "name": "navigate", "x": 100.0, "y": 200.0, "z": 300.0},
  {"type": "command", "name": "cam", "camera": "scene"},
  {"type": "console", "command": "t.MaxFPS 10"},
  #{"type": "console", "command": "log LogTemp Verbose"},
  #{"type": "query", "spawn": "any"},
  #{"type": "track", "object": resolved_camera, "property": "Position"},
  #{"type": "untrack", "object": resolved_camera, "property": "Position"},
  {"type": "command", "name": "start"}
]

for t in tests:
  pylog.log("Sending test message: {}".format(t))
  m.SendJSON(t)
  time.sleep(0.3)

# keep the script running and handle plotting from the main thread
while True:
  try:
    # process one frame for plotting if available
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
      # no frame ready; yield to other work
      pass
    time.sleep(0.05)
  except KeyboardInterrupt:
    break
