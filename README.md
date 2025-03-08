# A Texture Viewer

at last that's what it's supposed to become eventually..

**Goals:**

* Support at least Windows and Linux (and probably similar Unix-likes), maybe Mac if someone with a Mac takes care of that.
* Self-contained executable, using OpenGL and Dear ImGui and libglfw3
    - Only thing left to do for that: Add ability to link glfw statically
* Load some common image file formats (whatever stb_image supports :-p) and DDS textures containing BC1-3 or BC7 data
    - Maybe also BC4-6, maybe also KTX, maybe obscure formats from games like Quake2
* Show some basic info (format, encoding, size, ...)
* Support selecting mipmap level for display
* Zooming in/out, dragging the texture around the window
* Support selecting linear and nearest filtering
* Support showing all mipmap levels at once
* Support tiled view
    - including with different mipmap levels next to each other to see how the transitions line up
    - ideally also a perspective view with a big plane going towards infinity to see the texture's
      mipmapping (with different anisotropic filtering levels) in action
* Maybe different texture files next to each other (for example to compare quality of encoders)

**Maybe at some point:**

* Support rendering texture on a 3D object (cube, sphere, ...) with additional textures (normalmap, ...)
  and moveable light and camera
    - Allow customizing shader code for that (feasible with OpenGL as it takes GLSL directly)
* Diffing images (e.g. to show differences between source image and compressed texture)
