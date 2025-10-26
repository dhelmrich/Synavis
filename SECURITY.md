# WebRTC Stack and HolePunching

This project builds on WebRTC, which ultimately depends on hole-punching techniques to establish connections. Synavis also contains a TURN-like bridge implementation for multi-module clusters that allows relaying data when direct peer-to-peer connections cannot be established.

## Security Considerations

1. **Data Exposure**: Signalling Server connections as well as the individual connections through the data channel and video track are not currently encrypted. This is usually not an issue as the software stack is aimed at HPC usage, but personal considerations may apply depending on your use case. Do not transmit sensitive data without additional encryption layers in such cases.
2. **Unauthorized Access**: The signalling server does not implement authentication or authorization mechanisms. Anyone who can reach the signalling server can potentially alter the scene data hosted on the SynavisUE endpoint.

