# Synavis

C++ WebRTC Bridge for Unreal Engine PixelStreaming

Unreal Engine released PixelStreaming with its 4.27 version. It enables remote visualization for small-frontend devices.

Synavis enables the coupling of simulation and ML tools to the Unreal Engine by leveraging PixelStreaming as data source.

For testing purposes, I generally recommend the internal signalling server as it, moving forward, inherently more compatible than the PixelStreamingInfrastructure one.

## Progress so far

This Project includes a bridging ability, coupling ability and some means of steering the framework.

- DataConnector and MediaReceiver: Connect DL Frameworks and Simulations to UE and interact with the scene
- WebRTCBridge: Setup connections via a bridged network (like a TURN server) via port relays. This is intended for HPC systems but might be used together with Putty to bridge to client PCs

## Open Issues

- The framework does not base of libwebrtc and thus does not offer decoding capabilities itself. This is intentional for our cluster setup as we provide functionality there.
- There are test cases missing that still need to be added, those will also introduce robustness and remove ambiguity.

## Collaboration

I would greatly appreciate help with this project. It is an integral part of my thesis, but not the main focus of it.
I am working in Berlin time and will respond during work hours when I can spare time.

## Funding

I would like to acknowledge funding provided by the German government to the Gauss Centre for Supercomputing via the InHPC-DE project (01—H17001).


