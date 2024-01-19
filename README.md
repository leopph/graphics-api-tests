# A collection of graphics API test projects

This repository is a collection of my test projects using several different graphics APIs. I use these to try and play with new features and learn about the different methods and techniques.

## VulkanTutorial
A Vulkan learning project based on https://vulkan-tutorial.com/.

## D3D12
The D3D12 project contains implementations for
- multiple geometry pipeline methods
  - vertex pushing (using the input assembler)
  - vertex pulling (reading vertex buffers as shader resources)
- multiple resource binding methods
  - bindful descriptors
  - bindless descriptors using SM 5.1 dynamic indexing and unbounded arrays
  - bindless descriptors using SM 6.6 dynamic resources (ResourceDescriptorHeap and SamplerDescriptorHeap)
- multiple barrier usage methods
  - legacy resource barriers (ResourceBarrier API)
  - enhanced barriers (Barrier API)
- multiple fullscreen methods
  - using a fullscreen swap chain
  - using a windowed swap chain and a screen sized window
- etc.

These can be toggled using macros. See *main.cpp* for more details.

The demo also demonstrates the use of true immediate independent flip in a borderless window.

## D3D11
The D3D11 test implements
- independent flip true immediate support for windowed swap chains (replacing exclusive fullscreen mode)
- hardware composition support query in fullscreen and windowed scenarios
- waitable swap chains
- automatic input layout generation using shader reflection
- compute shaders
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
