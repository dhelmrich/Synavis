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

- We are working on providing software decoding in-place.
- There are test cases missing that still need to be added, those will also introduce robustness and remove ambiguity.

## Tutorial and Wiki information

- I am updating the wikis slowly, including some information on how to get started.
- Additionally, I am still editting the recordings for our JSC course "UE for Remote Visualization and Machine Learning", as I would like them to be of high enough quality for publishing.

## Collaboration

I would greatly appreciate help with this project. It is an integral part of my thesis, but not the main focus of it.
I am working in Berlin time and will respond during work hours when I can spare time.

## Funding

I would like to acknowledge funding provided by the German government to the Gauss Centre for Supercomputing via the InHPC-DE project (01—H17001).

## Cite as

Dirk Norbert Helmrich, Felix Maximilian Bauer, Mona Giraud, Andrea Schnepf, Jens Henrik Göbbert, Hanno Scharr, Ebba Þora Hvannberg, Morris Riedel, A Scalable Pipeline to Create Synthetic Datasets from Functional-Structural Plant Models for Deep Learning, in silico Plants, 2023;, diad022, https://doi.org/10.1093/insilicoplants/diad022


