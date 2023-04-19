import asyncio
import subprocess
import websockets as ws
import json
import time
from colorama import init as colorama_init
from colorama import Fore, Back, Style
import threading
import sys

ws.debug = True

target_ip = "0.0.0.0"
server_port = 8888
client_port = 8080
sfu_port = 8889 # not used yet, but this is the port that the SFU listens on for signalling

class Logger() :
  def __init__ (self):
    colorama_init()
    self.light_mode = False
  #enddef
  def set_light_mode(self, light_mode) :
    self.light_mode = light_mode
    if light_mode :
      self.info("Light mode enabled")
    else :
      self.info("Light mode disabled")
    #endif
  #enddef
  def log(self, message, color = Fore.WHITE) :
    print(color + message + Style.RESET_ALL)
  #enddef
  def info(self, message) :
    self.log(message, (Fore.BLACK if self.light_mode else Fore.WHITE))
  #enddef
  def warn(self, message) :
    self.log(message, (Fore.MAGENTA if self.light_mode else Fore.YELLOW))
  #enddef
  def error(self, message) :
    self.log(message, Fore.RED)
  #enddef
  def success(self, message) :
    self.log(message, Fore.GREEN)
  #enddef

  def log_outgoing(self, message, receiver = None) :
    if receiver != None :
      self.log(str(receiver.role) + "[" + str(receiver.id) + "]" + " -> " + str(message), Fore.BLUE)
    else :
      self.log("-> " + message, Fore.BLUE)
  #enddef
  def log_incoming(self, message, sender = None) :
    if sender != None :
      self.log(str(sender.role) + "[" + str(sender.id) + "]" + " <- " + str(message), Fore.CYAN)
    else :
      self.log("<- " + message, Fore.CYAN)
  #enddef

  def log_many(self, level = "info", *messages) :
    cumulated = ""
    for message in messages :
      cumulated += message + " "
    #endfor
    if level == "info" :
      self.info(cumulated)
    elif level == "warning" :
      self.warn(cumulated)
    elif level == "error" :
      self.error(cumulated)
    elif level == "success" :
      self.success(cumulated)
    else :
      self.info(cumulated)
#endclass

glog = Logger()

connections = {}
active_server = None

# a method that returns all connections that do not have any other connections assigned to them
# parameter role: "server" or "client"
def get_unassigned_connections(role : str = "client") :
  unassigned = []
  for connection in connections.values() :
    if len(connection.connected_ids) == 0 and connection.role == role :
      unassigned.append(connection)
    #endif
  #endfor
  return unassigned
#enddef

# a method that waits for a few moments and then notifies Unreal about unmatched clients
async def notify_unreal_about_unmatched_clients() :
  global connections, glog, active_server
  await asyncio.sleep(2)
  for client in get_unassigned_connections("client") :
    active_server.assign(client)
    await active_server.send(json.dumps({"type":"playerConnected", "playerId": str(client.id), "dataChannel": True, "sfu":False}))
  #endfor

class Connection() :
  def __init__(self, websocket) :
    self.websocket = websocket
    self.id = None
    self.role = "server" if websocket.local_address[1] == server_port else "client"
    self.assign_id()
    self.recent = True
    self.connected_ids = []
    self.expects_media = False
    self.supports_extmap = False
    self.supports_synchronous_data = False
    self.extmap = []
  #enddef
  async def send(self, message) :
    global glog
    glog.log_outgoing(message, self)
    await self.websocket.send(message)
  #enddef
  def assign(self, other) :
    # add the connections to each other's list of connected connections if they are not already in there
    if not other.id in self.connected_ids :
      self.connected_ids.append(other.id)
    if not self.id in other.connected_ids :
      other.connected_ids.append(self.id)
  #enddef
  def unassign(self, other) :
    self.connected_ids.remove(other.id)
    # only we have to remove the id, because the other connection will be removed anyway
  #enddef
  def is_recent(self) :
    if self.recent :
      self.recent = False
      return True
    else :
      return False
  def close(self) :
    self.websocket.close()
  #enddef
  """ a method that assigns a new unique id to the connection"""
  def assign_id(self) :
    self.id = 101
    while self.id in connections :
      self.id += 1
    #endwhile	
  #enddef
  def __str__(self) :
    return "Connection[" + str(self.id) + "]" + "(" + self.role + ")"
  #enddef
  def ParseSDP(self, spd: str) :
    extmap_startswith = "a=extmap"
    extmap_skiplength = len(extmap_startswith)
    lines = spd.splitlines(False)
    categories = []
    app_and_media = [i for i in range(len(lines)) if lines[i].startswith("m=")]
    medias = [i for i in range(len(lines)) if lines[i].startswith("m=video") or lines[i].startswith("m=audio")]
    self.extmap = [[lines[i] for i in range(j,len(lines)) if lines[i].startswith(extmap_startswith)] for j in medias]
    hpc_line = [line for line in self.extmap if "hpc-bridge" in line]
    if len(self.extmap) > 0 :
      self.supports_extmap = True
    if len(hpc_line) > 0 :
      self.supports_synchronous_data = True
    #endfor
  #enddef
#endclass

async def handle(connection, message) :
  global connections, glog
  content = {}
  id = -1
  try :
    content = json.loads(message)
  except :
    glog.error("Invalid JSON: " + message)
    return
  glog.log_incoming(content, connection)
  if "type" in content :
    if content["type"] == "ping" :
      glog.log_outgoing("pong")
      content["type"] = "pong"
      # setting the time to the current time in seconds
      content["time"] = int(time.time())
      await connection.send(json.dumps(content))
    #endif
    elif "sdp" in content :
      sdp = content["sdp"]
      connection.ParseSDP(sdp)
      # clean up the json object so that unreal doesn't complain
      data = {"type": content["type"], "sdp": sdp}
      if connection.role == "server" :
        for player_id in connection.connected_ids :
          await connections[player_id].send(json.dumps(data))
      elif connection.role == "client" :
        if len(connection.connected_ids) == 0 :
          glog.log_many("warning", "No server connected for ", str(connection), " we will try again when a server connects")
          await connection.send(json.dumps({"type": "control", "message": "No server connected"}))
        for server_id in connection.connected_ids :
          data["playerId"] = connection.id
          await connections[server_id].send(json.dumps(data))
        #endfor
      #endif
    elif "candidate" in content or "iceCandidate" in content :
      data = {"type": content["type"], "candidate": content["candidate"]}
      if connection.role == "server" :
        for player_id in connection.connected_ids :
          await connections[player_id].send(json.dumps(data))
      elif connection.role == "client" :
        for server_id in connection.connected_ids :
          data["playerId"] = connection.id
          await connections[server_id].send(json.dumps(data))
        #endfor
      #endif
    elif "request" in content :
      if content["request"] == "id" :
        await connection.send(json.dumps({"type": "id", "id": connection.id}))
      elif content["request"] == "role" :
        await connection.send(json.dumps({"type": "role", "role": connection.role}))
      elif content["request"] == "connections" :
        await connection.send(json.dumps({"type": "connections", "connections": zip([id for id in connections], [connections[id].role for id in connections])}))
      elif content["request"] == "broadcast" :
        glog.info("Client " + str(connection.id) + " requested a broadcast")
    #endif
  #endif
#enddef

async def connection(websocket, path) :
  global connections, glog, active_server, active_client
  glog.log_many("info", "{" , str([id for id in connections]) , "}")
  connection = next((connections[id] for id in connections if connections[id].websocket == websocket), Connection(websocket))
  if connection.is_recent() :
    glog.success("open connection " + str(connection))
    connections[connection.id] = connection
  if connection.role == "client" :
    await connection.send(json.dumps({"type": "id", "id": connection.id}))
    if active_server != None :
      connection.assign(active_server)
      active_server.assign(connection)
      # send an unreal-like player connect message
      await active_server.send(json.dumps({"type":"playerConnected", "playerId": str(connection.id), "dataChannel": True, "sfu": False}))
  elif connection.role == "server" :
    # deactivate ping frames
    connection.websocket.ping_interval = None
    if active_server == None :
      # run the notify method non-blocking so that we can continue to accept new connections
      asyncio.ensure_future(notify_unreal_about_unmatched_clients())
    glog.info("New active server: " + str(connection))
    active_server = connection
  #endif
  while True :
    try :
      message = await asyncio.wait_for(websocket.recv(), timeout = 1.0)
      await handle(connection, message)
    except asyncio.exceptions.TimeoutError as t :
      continue
    except ws.WebSocketException as e :
      # determine unix timestamp of when the connection was closed
      connection.close_time = int(time.time())
      if e.code in [1000,1001] :
        glog.warn("Websocket closed: " + str(e) + " " + str(connection) + " at time " + str(connection.close_time))
      else :
        glog.error("Websocket error: " + str(e) + " " + str(connection) + " at time " + str(connection.close_time))
      break
  connections.pop(connection.id)
  # send an unreal-like player disconnect message
  if connection.role == "client" :
    for server_id in connection.connected_ids :
      try :
        if connections[server_id].websocket.open :
          await connections[server_id].send(json.dumps({"type":"playerDisconnected", "playerId": str(connection.id)}))
      finally:
        connections[server_id].unassign(connection)
  elif connection.role == "server" :
    for client_id in connection.connected_ids :
      try :
        if connections[client_id].websocket.open :
          await connections[client_id].send(json.dumps({"type":"serverDisconnected", "serverId": connection.id}))
      finally:
        connections[client_id].unassign(connection)
    if active_server == connection :
      active_server = None
  glog.info("closed connection " + str(connection))
  del connection
#endeif

async def main() :
  global connections, glog
  # checking if we have command line arguments
  if len(sys.argv) > 1 :
    # checking if we have lightmode enabled
    if "--lightmode" in sys.argv :
      glog.set_light_mode(True)
  # start the signalling server
  glog.info("Starting server...")
  async with ws.serve(connection, target_ip, server_port), ws.serve(connection, target_ip, client_port) :
    await asyncio.Future()

def start_signalling(External : bool = False) :
  global glog
  global server_port
  global connections
  global client_port
  global active_server
  glog.info("Starting server...")
  # run main in a new thread so that it doesn't block the main thread
  if External :
    # start unreal signalling instead
    # run node in command line
    # node ./unreal_signalling.js
    # run node in python
    output = open("unreal_signalling.log", "w")
    subprocess.Popen(["node", "../../../PixelStreamingInfrastructure/SignallingWebServer/cirrus.js"],stdout=output,stderr=output)
  else :
    return threading.Thread(target = asyncio.run, args = (main(),)).start()

if __name__ == "__main__" :
  asyncio.run(main())

