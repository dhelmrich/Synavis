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

syn.SetGlobalLogVerbosity(syn.LogVerbosity.LogVerbose)
syn.VerboseMode()
pylog = syn.Logger()
# ensure log file is created (OpenUniqueFile will append timestamp)
pylog.logFile("extraction.log")
pylog.setidentity("Synavis Unit Test")

pylog.log("Starting extraction module")

HEIGHT = 512
WIDTH = 512

message_buffer = []

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
    try:
      s = msg.decode('utf-8')
    except Exception:
      s = msg.decode('utf-8', errors='replace')
  else:
    s = str(msg)
  pylog.log(f"Received message: {s}")
  message_buffer.append(s)

# a callback function for the data connector
def data_callback(data) :
  pylog.log("Received raw data packet of length {}".format(len(data)))

def frame_callback(frame) :
  # Update running statistics about received frames
  try:
    # Try to access expected FrameContent fields exposed from C++
    width = getattr(frame, 'Width', None)
    height = getattr(frame, 'Height', None)
    timestamp = getattr(frame, 'Timestamp', None)
    try:
      data_len = len(frame.Data)
    except Exception:
      data_len = 0
  except Exception:
    width = height = timestamp = None
    data_len = 0

  STATS['total_frames'] += 1
  STATS['bytes_total'] += data_len
  if width and height:
    res = f"{int(width)}x{int(height)}"
    STATS['by_resolution'][res] += 1
  if timestamp is not None:
    STATS['by_timestamp'][int(timestamp)] += 1

  now = time.time()
  STATS['frame_times'].append(now)

  # Periodically log aggregated stats (every LOG_INTERVAL seconds)
  if now - STATS['last_log'] >= LOG_INTERVAL:
    elapsed = now - STATS['last_log']
    frames = STATS['total_frames'] - STATS['last_total']
    bytes_sent = STATS['bytes_total'] - STATS['last_bytes']
    fps = frames / elapsed if elapsed > 0 else 0
    avg_bytes = (bytes_sent / frames) if frames > 0 else 0
    pylog.log(f"Frames total={STATS['total_frames']} recent={frames} fps={fps:.2f} avg_bytes={avg_bytes:.1f}")
    # log top resolutions
    top_res = sorted(STATS['by_resolution'].items(), key=lambda x: -x[1])[:5]
    for r, c in top_res:
      pylog.log(f"  res={r} count={c}")
    STATS['last_log'] = now
    STATS['last_total'] = STATS['total_frames']
    STATS['last_bytes'] = STATS['bytes_total']

m = syn.MediaReceiver()
f = syn.FrameDecode()
f.SetFrameCallback(frame_callback)
m.Initialize()
#Media.SetConfigFile("config.json")
m.SetConfig({"SignallingIP": "172.21.96.1","SignallingPort":8080})
m.SetTakeFirstStep(False)
m.StartSignalling()
m.SetDataCallback(data_callback)
m.SetMessageCallback(message_callback)
#m.SetFrameReceptionCallback(f.CreateAcceptor(data_callback))
# temporarily just log whether we got a track message
m.SetFrameReceptionCallback(lambda data: pylog.log("Received track data of length {}".format(len(data))))
m.SetOnTrackOpenCallback(lambda: pylog.log("Track opened"))
# exit Python when the incoming track closes
m.SetOnTrackCloseCallback(lambda: syn.ExitWithMessage("Track closed", 1))
# exit Python when the data channel closes
m.SetOnClosedCallback(lambda: syn.ExitWithMessage("Data channel closed", 2))
m.SetRetryOnErrorResponse(True)
m.LockUntilConnected(1000)

while not m.GetState() == syn.EConnectionState.CONNECTED:
  time.sleep(0.1)

pylog.log("Connected to media sender.")

# attempt to choose a sensible default datachannel (one that contains 'handler')
try:
  names = m.GetDataChannelNames()
  pylog.log(f"Available datachannels: {names}")
  for nm in names:
    try:
      if "handler" in nm.lower():
        if m.SelectDataChannelByName(nm):
          pylog.log(f"Selected datachannel '{nm}' as default")
          break
    except Exception:
      continue
except Exception as e:
  pylog.log(f"Could not enumerate/select datachannels: {e}")
# Helper: poll message buffer for the actor list response
def poll_for_actor_list(timeout=2.0):
  start = time.time()
  while time.time() - start < timeout:
    # iterate over a copy to allow removal
    for idx, raw in enumerate(list(message_buffer)):
      try:
        obj = json.loads(raw)
      except Exception:
        continue
      if obj.get("type") == "query" and obj.get("name") == "all" and isinstance(obj.get("data"), list):
        # remove the matched entry from the real buffer
        try:
          # find exact index in original buffer (could have shifted)
          for j in range(len(message_buffer)):
            if message_buffer[j] == raw:
              message_buffer.pop(j)
              break
        except Exception:
          pass
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
    try:
      if prefix.lower() in n.lower():
        return n
    except Exception:
      continue
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
  {"type": "query", "object": resolved_camera, "property": "Position"},
  {"type": "command", "name": "navigate", "x": 100.0, "y": 200.0, "z": 300.0},
  {"type": "command", "name": "cam", "camera": "scene"},
  {"type": "command", "name": "trace", "direction": {"x": 0, "y": 0, "z": -1}},
  {"type": "query", "spawn": "any"},
  {"type": "track", "object": resolved_camera, "property": "Position"},
  {"type": "untrack", "object": resolved_camera, "property": "Position"},
  {"type": "command", "name": "start"}
]

for t in tests:
  pylog.log("Sending test message: {}".format(t))
  m.SendJSON(t)
  time.sleep(0.3)

# keep the script running

while True:
  # allow for ctrl+c exit
  try:
    time.sleep(1)
  except KeyboardInterrupt:
    break

# --- Statistics setup ---
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