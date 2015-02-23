// Simple example program for the DGL graphics library.
// Compile with g++ simple-example.cpp -o simple-example -ldgl -lpixman.

// Include standard library.
#include <stdlib.h>
// Include unistd.h for sleep function.
#include <unistd.h>
// Include DGL library header file
#include "dgl.h"

// C/C++ main entry function.
int main(int arg, char *argv[]) {
	// Create and initialize the console framebuffer. A pointer to the
	// dglFB structure is returned. This can be used to create a drawing
	// context, or directly with certain functions. The dlgFB structure
	// contains information about the address and dimensions of the
	// framebuffer.
	dglConsoleFB *fb = dglCreateConsoleFramebuffer();

	// Create a graphics context. The context consists of a read
	// framebuffer and a draw framebuffer. The draw framebuffer is most
	// relevant since it will be the target of most drawing functions.
	// The read framebuffer will only be used by dglCopyArea.
	//
	// In this example, both the read and draw framebuffers are set to
	// the console framebuffer (the screen), so that drawing operations
	// will be immediately visible.
	dglContext *context = dglCreateContext(fb, fb);

	// Clear the entire screen with black pixels. dglFill uses the current
	// draw framebuffer from the context as target. fb->xres is the width
	// of the screen and fb->yres the height. A pixel value of 0
	// represents red, green and blue color components of zero, independent
	// of framebuffer depth setting.
	dglFill(context, 0, 0, fb->xres, fb->yres, 0x000000);

	// Calculate the pixel value of three color components that range
	// from 0.0 to 1.0 (floating point). In this example, the selected
	// color will be greenish (green component is the maximum value)
	// with blue added (0.6f), creating a color close to cyan.
	// The first parameter is the pixel format of the console framebuffer
	// (which can be 16 bpp or 32bpp). uint32_t is a standard 32-bit
	// unsigned integer type.
	uint32_t pixel = dglConvertColor(fb->format, 0.0f, 1.0f, 0.6f);

	// Draw a rectangle filled with the color. The current draw
	// framebuffer (the screen) will be used. The x and y coordinates
	// are 100, and the width and height of the rectangle are 200 and
	// 600 pixels, respectively. This draws a elongated rectangle on
	// the left side of the screen, which will fit even on low-resolution
	// screens.
	dglFill(context, 100, 100, 200, 600, pixel);

	// Now we copy the region that precisely surrounds the filled
	// rectangle to the right part of the screen using the dglCopyArea
	// function, creation an identical elongated rectangle.
	// The top-left source coordinates are (100, 100) and the destination
	// x coordinate is 100 pixels offset to the right from the middle
	// of the screen witha destination y coordinate of 100. Finally,
	// the width and height of 200 by 600 precisely match the filled
	// rectangle.
	dglCopyArea(context, 100, 100, fb->xres / 2 + 100, 100, 200, 600);

	// Wait five seconds in order to show the result before restoring
	// textmode.
	sleep(5);

	// Restore text mode and free the fb structure.
	dglDestroyConsoleFramebuffer(fb);
}
