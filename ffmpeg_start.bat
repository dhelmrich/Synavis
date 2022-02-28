@echo off
Rem ffplay -analyzeduration 100M -probesize 100M -fflags nobuffer -loglevel debug -protocol_whitelist file,udp,rtp -i pixelstreaming.sdp
Rem ffmpeg -analyzeduration 100M -probesize 100M -fflags nobuffer -loglevel debug -protocol_whitelist file,udp,rtp -i tensorworks.sdp -y output.mkv
ffplay -analyzeduration 1000M -probesize 1000M  -fflags nobuffer -flags low_delay -i tensorworks.sdp -protocol_whitelist file,udp,rtp
