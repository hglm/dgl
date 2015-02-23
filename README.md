DGL is a small low-level graphics library that was put together to test
extensions to the Linux kernel fb interface on ARM-based Raspberry Pi
and Raspberry Pi 2. It also usable on other devices.

The library provides a few simple drawing primitives (Fill, CopyArea,
PutImage, PutPixel) with flexible support for different drawing
surfaces (console framebuffer, offscreen pixmap, image).

On the Raspberry Pi and Raspberry Pi 2, the current kernel fb interface as
of February 2015 is limited to single-buffering (no page flipping), limiting
opportunies for fluid tear-free animation, although library will work.

However, a relatively trivial patch for the Linux kernel for Raspberry Pi
devices extends the framebuffer to three buffers and allows double-buffering
or triple-buffering. The DMA-accelerated CopyArea function that was already
provided by the fb interface has also been extended to allow access to all
three pages. In this way, relatively fluid graphics and animation are more
easily achievable on the Raspberry Pi models, without resorting to EGL and
OpenGL ES 2.0.

The patch is available in my "patches" repository
(raspberrypi/rpi-extend-fb.patch) and also in the extend-fb branch of my fork
of raspberrypi/linux.
