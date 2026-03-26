Adios2Backend
=================

This plugin is a skeleton for an ADIOS2-based Synavis backend.

Goals:
- Provide a module that can register a `UAdios2Streamer` component implementing the
  `USynavisCommunicationInterface` used by SynavisUE.
- Use ADIOS2 to publish raw buffers (no encoding) from Unreal to consumers.

Notes:
- This initial commit provides a minimal plugin structure and a stub `UAdios2Streamer`.
- Real ADIOS2 integration requires linking against ADIOS2 and adding read/write calls.

ADIOS2 references:
- ADIOS2 components guide: https://adios2.readthedocs.io/en/v2.11.0/components/components.html
