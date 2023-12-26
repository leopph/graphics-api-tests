# A collection of graphics API test projects

This repository is a collection of my test projects using several different graphics APIs. I use these to try and play with new features and learn about the different methods and techniques.

## Vulkan
The Vulkan project is currently WIP.

## D3D12
The D3D12 project contains implementations for
- vertex pulling
- classic bindful resources
- HLSL 5.1 dynamic resource indexing
- SM 6.6 dynamic resources (ResourceDescriptorHeap and SamplerDescriptorHeap)
- etc.

These can be toggled using macros. See *main.cpp* for more details.

## D3D11
The D3D11 test implements
- independent flip true immediate support for windowed swap chains (replacing the classic fullscreen mode)
- waitable swap chains
- frame latency control using fences
- deferred contexts
- etc.

## OpenGL
The OpenGL project demonstrates
- direct state access (DSA)
- separate vertex formats (ARB_vertex_attrib_binding)
- indirect drawing
- multidraw
- color space conversions
- etc.
