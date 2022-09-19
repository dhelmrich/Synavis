# In progress project -  do not use in production!

**Update 2022-03-01** Dependencies are now all updated and should work without issue. Gstreamer can still read the frames from the rtp relay, and you should be able to access them via opencv with gstreamer. libdatachannel has an example on how this works. We are working on enhancing compatability with vanilla opencv.

# What will happen to this project going forward

We have decided to remodel our workflows to use encapsulated networking. This enables us to use Unreal Engine workflows in a combination of cloud and HPC services which are physically and logically seperate and where no direct webrtc communication is possible.

# UnrealReceiver
C++ WebRTC Bridge for Unreal Engine PixelStreaming

Unreal Engine released PixelStreaming with its 4.27 version. I want to be able to receive the images and use them as 2D numpy arrays for batch assembly in machine learning.
To test this project, you need to have a PixelStreaming project running in the background.

For my testing purposes, I have a very simple project of the player pawn following a spline, rendered in only 1024x768@30FPS s.t. my computer doesn't notice it much.

Many aspects are still hardcoded as this project is in progress. The ultimate goal is to use this bridge as a lightweight programm informing Pytorch or Tensorflow batches on our compute cluster.

## Progress so far

I have, with the help of Tensorworks' WebRTC Bridge (https://github.com/TensorWorks/Unreal-PixelStreaming-RTP-Bridge), libdatachannel (subproject), and the Unreal Engine PixelStreaming sample, implemented this repo so far.
I have introduced Python bindings for the functions so that I can easier prototype decoding.
I can verify, with the relay over the old winsocket, that the frames are received as planned.

## Open Issues

We have tested a running pipeline with one-sided image provision and a Gstreamer/OpenCV decoding as training sequence. THis works well and we have tested this for depth estimation from monocular images.

## Collaboration

I would greatly appreciate help with this project. It is an integral part of my thesis, but not the main focus of it.
I am working in Berlin time and will respond during work hours when I can spare time.

## Legal Details

I would like to acknowledge funding provided by the German government to the Gauss Centre for Supercomputing via the InHPC-DE project (01—H17001).


