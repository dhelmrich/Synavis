# Synavis

C++ WebRTC Bridge for Unreal Engine PixelStreaming

Unreal Engine released PixelStreaming with its 4.27 version. It enables remote visualization for small-frontend devices.

Synavis enables the coupling of simulation and ML tools to the Unreal Engine by leveraging PixelStreaming as data source.

See our video:

[![Synavis Introduction](https://img.youtube.com/vi/H9cw_aE-l3A/0.jpg)](https://www.youtube.com/watch?v=H9cw_aE-l3A)

## Feature Set

- DataConnector and MediaReceiver: Connect DL Frameworks and Simulations to UE and interact with the scene
- Synavis: Setup connections via a bridged network (like a TURN server) via port relays. This is intended for HPC systems but might be used together with Putty to bridge to client PCs
- Syanvis Signalling Server: Load Handling and Connection Management
- PySynavis: Python Coupling of all revelant functionality

## Open Issues

- Automatic Connection setup and infrastructure-focussed handling is in works
- Decoding is in works, with UE allowing the parsing of software-based encoders, we aim to support these primarily, as they are not dependent on a specific GPU.

## Tutorial and Wiki information

- Our course "Virtual Worlds for Machine Learning" provided some in-depth information on the framework and is the newest tutorial. Contact us for more information.
- The wiki contains all current information and is being edited more frequently than this repository: [Synavis Wiki](https://github.com/dhelmrich/Synavis/wiki)

## Collaboration

I would greatly appreciate help with this project. It is an integral part of my thesis, but not the main focus of it.
I am working in Berlin time and will respond during work hours when I can spare time.

## Funding

I would like to acknowledge funding provided by the German government to the Gauss Centre for Supercomputing via the InHPC-DE project (01—H17001).


