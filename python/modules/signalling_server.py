import asyncio
import websockets as ws
import json
import time
from colorama import init as colorama_init
from colorama import Fore, Back, Style

ws.debug = True

server_port = 8888
client_port = 8889

class Logger() :
  def __init__ (self):
    colorama_init()
  #enddef
  def log(self, message, color = Fore.WHITE) :
    print(color + message + Style.RESET_ALL)
  #enddef
  def info(self, message) :
    self.log(message, Fore.WHITE)
  #enddef
  def warn(self, message) :
    self.log(message, Fore.YELLOW)
  #enddef
  def error(self, message) :
    self.log(message, Fore.RED)
  #enddef
  def success(self, message) :
    self.log(message, Fore.GREEN)
  #enddef

  def log_outgoing(self, message) :
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
    elif level == "warn" :
      self.warn(cumulated)
    elif level == "error" :
      self.error(cumulated)
    elif level == "success" :
      self.success(cumulated)
#endclass

glog = Logger()

connections = {}

class Connection() :
  def __init__(self, websocket) :
    self.websocket = websocket
    self.id = None
    self.role = "server" if websocket.local_address[1] == server_port else "client"
    self.assign_id()
    self.recent = True
    self.connected_ids = []
    self.supports_extmap = False
    self.supports_synchronous_data = False
    self.extmap = []
  #enddef
  def send(self, message) :
    self.websocket.send(message)
  #enddef
  def assign(self, other) :
    self.connected_ids.append(other.id)
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
    self.id = 100
    while self.id in connections :
      self.id += 1
    #endwhile	
  #enddef
  def __str__(self) :
    return "Connection[" + str(self.id) + "]" + " (" + self.role + ")"
  #enddef
  def ParseSDP(self, spd: str) :
    extmap_startswith = "a=extmap"
    extmap_skiplength = len(extmap_startswith)
    lines = spd.splitlines(False)
    categories = []
    medias = [i for i in range(len(lines)) if lines[i].startswith("m=")]
    maplines = [[lines[i] for i in range(j,len(lines)) if lines[i].startswith(extmap_startswith)] for j in medias]
    hpc_line = [line for line in maplines if "hpc-bridge" in line]
    if len(maplines) > 0 :
      self.supports_extmap = True
    if len(hpc_line) > 0 :
      self.supports_synchronous_data = True
    for stream in medias :
      if len(maplines[stream]) > 0 :
        self.extmap.append[(lines[medias.split(0)[2:]], int(maplines.split()[0][extmap_skiplength:]))]
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
  if "type" in content :
    if content["type"] == "ping" :
      glog.log_incoming(content, connection)
      glog.log_outgoing("pong")
      content["type"] = "pong"
      # setting the time to the current time in seconds
      content["time"] = int(time.time())
      await connection.websocket.send(json.dumps(content))
    #endif
    elif "sdp" in content :
      sdp = content["sdp"]
      connection.ParseSDP(sdp)
      data = json()
      data["type"] = "extmap"
      data["extmap"] = json.encoder(connection.extmap)
      if connection.role == "server" :
        for player_id in connection.connected_ids :
          await connections[player_id].send(json.dumps(data))
      elif connection.role == "client" :
        for server_id in connection.connected_ids :
          data["playerId"] = connection.id
          await connections[server_id].send(json.dumps(data))
  #endif
#enddef

async def connection(websocket, path) :
  global connections, glog
  glog.log_many("info", "{" , str([id for id in connections]) , "}")
  connection = next((connections[id] for id in connections if connections[id].websocket == websocket), Connection(websocket))
  if connection.role == "client" :
    await websocket.send(json.dumps({"type": "id", "id": connection.id}))
  if connection.is_recent() :
    glog.success("open connection " + str(connection))
    connections[connection.id] = connection
  #endif
  while True :
    try :
      message = await asyncio.wait_for(websocket.recv(), timeout = 1.0)
      await handle(connection, message)
    except asyncio.exceptions.TimeoutError as t :
      continue
    except ws.WebSocketException as e :
      if e.code in [1000,1001] :
        glog.warn("Websocket closed: " + str(e))
      else :
        glog.error("Websocket error: " + str(e))
      break
  connections.pop(connection.id)
  glog.info("closed connection " + str(connection))
  del connection
#endeif

async def main() :
  global connections, glog
  async with ws.serve(connection, 'localhost', 8888), ws.serve(connection, 'localhost', 8889) :
    await asyncio.Future()

if __name__ == "__main__" :
  glog.info("Starting server...")
  asyncio.run(main())

