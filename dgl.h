/*

Copyright (c) 2015 Harm Hanemaaijer <fgenfb@yahoo.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/

#ifndef __DGL_H__
#define __DGL_H__

#include <stdint.h>

#define DGL_INLINE_ONLY __attribute__((always_inline)) inline

enum {
	DGL_FORMAT_LSB_ORDER_BGR_BIT = 0x0,
	DGL_FORMAT_LSB_ORDER_RGB_BIT = 0x1,
	DGL_FORMAT_ALPHA_BIT = 0x2,
	DGL_FORMAT_PIXEL_SIZE_16_BIT = 0x4,
	DGL_FORMAT_XRGB8888 = DGL_FORMAT_LSB_ORDER_BGR_BIT,
	DGL_FORMAT_XBGR8888 = DGL_FORMAT_LSB_ORDER_RGB_BIT,
	DGL_FORMAT_ARGB8888 = DGL_FORMAT_LSB_ORDER_BGR_BIT | DGL_FORMAT_ALPHA_BIT,
	DGL_FORMAT_ABGR8888 = DGL_FORMAT_LSB_ORDER_RGB_BIT | DGL_FORMAT_ALPHA_BIT,
	DGL_FORMAT_RGB565 = DGL_FORMAT_LSB_ORDER_BGR_BIT | DGL_FORMAT_PIXEL_SIZE_16_BIT,
	DGL_FORMAT_BGR565 = DGL_FORMAT_LSB_ORDER_RGB_BIT | DGL_FORMAT_PIXEL_SIZE_16_BIT,
};

enum {
	DGL_FB_TYPE_PIXMAP = 0,
	DGL_FB_TYPE_IMAGE = 1,
	DGL_FB_TYPE_CONSOLE = 2,
	DGL_FB_TYPE_MASK = 0x7,
	DGL_FB_FLAG_HAVE_COPY_AREA = 0x1000,
	DGL_FB_FLAG_HAVE_PAN_DISPLAY = 0x2000,
	DGL_FB_FLAG_HAVE_WAIT_VSYNC = 0x4000,
};

class dglPixelBuffer {
public :
	uint8_t *framebuffer_addr;
	uint32_t format;
	uint32_t flags;
	int xres, yres;
	int stride;
	int total_size;		// Can be derived from dimensions and format.
	int bytes_per_pixel;	// Can be derived from format.
};

typedef dglPixelBuffer dglFB;
typedef dglPixelBuffer dglImage;

class dglScreenFB : public dglFB {
public :
	int virtual_xres;
	int virtual_yres;
	int nu_pages;

	void (*PanDisplayFunc)(dglScreenFB *fb, int x, int y);
	void (*WaitVSyncFunc)(dglScreenFB *fb);
	void (*CopyAreaFunc)(dglScreenFB *fb, int sx, int sy, int dx, int dy, int w, int h);
};

class dglConsoleFB : public dglScreenFB {
public :
	int fd;
	bool graphics_mode_set;
};

class dglContext {
public :
	dglFB *read_fb;
	dglFB *draw_fb;
	int read_yoffset;
	int draw_yoffset;
};

class dglClipRectangle {
public :
	int x1;
	int y1;
	int x2;
	int y2;
};

// General functions.

// Messages will only be displayed if the priority is smaller than or equal
// to the configured debug message verbosity level (dglSetDebugMessageLevel).
enum {
    // When the debug message level is equal to - 3, in principle no text
    // output should occur.
    DGL_MESSAGE_FATAL_ERROR = - 4,
    DGL_MESSAGE_QUIET = - 3,
    DGL_MESSAGE_CRITICAL = - 2,
    DGL_MESSAGE_WARNING = - 1,
    // Priority levels 0 to 2 are information (0 corresponds to the
    // least frequent information that always displayed by default with
    // a debug message verbosity of 0, level 2 corresponds to frequent
    // information at logging level).
    DGL_MESSAGE_INFO = 0,
    DGL_MESSAGE_LOG = 1,
    DGL_MESSAGE_VERBOSE_LOG = 2
};

void dglMessage(int level, const char *format, ...);
void dglSetDebugMessageLevel(int level);

// Functions specific to console framebuffer

dglConsoleFB *dglCreateConsoleFramebuffer();
void dglDestroyConsoleFramebuffer(dglConsoleFB *cfb);
void dglConsoleFBPanDisplay(dglScreenFB *cfb, int x, int y);
void dglConsoleFBWaitVSync(dglScreenFB *cfb);
const char *dglGetInfoString(dglConsoleFB *cfb);
void dglConsoleFBCopyArea(dglScreenFB *fb, int sx, int sy, int dx, int dy, int w, int h);

// Functions for pixmap framebuffer.

dglFB *dglCreatePixmapFB(uint32_t format, int w, int h);
void dglDestroyPixmapFB(dglFB *fb);

// Functions for screen framebuffer (e.g. console framebuffer,
// or X framebuffer).

void dglPanDisplay(dglScreenFB *fb, int x, int y);
void dglSetDisplayPage(dglScreenFB *cfb, int page);
void dglWaitVSync(dglScreenFB *fb);

// Context

dglContext *dglCreateContext(dglFB *read_fb, dglFB *draw_fb);
void dglDestroyContext(dglContext *context);
void dglSetReadFramebuffer(dglContext *context, dglFB *fb);
void dglSetDrawFramebuffer(dglContext *context, dglFB *fb);

// Screen framebuffer

void dglWaitVSync(dglScreenFB *context);
void dglPanDisplay(dglScreenFB *context, int x, int y);

// Images.

dglImage *dglCreateImage(uint32_t format, int w, int h);
dglImage *dglCreateImageFromBuffer(uint32_t format, int w, int h, uint8_t *buffer);
void dglDestroyImage(dglImage *image);

// Drawing functions.

void dglPutPixel(dglContext *context, int x, int y, uint32_t pixel);
void dglCopyArea(dglContext *context, int sx, int sy, int dx, int dy, int w, int h);
void dglPutImage(dglContext *context, int x, int y, dglImage *image);
void dglPutPartialImage(dglContext *context, int sx, int sy, int dx, int dy,
int w, int h, dglImage *image);
void dglFill(dglContext *context, int x, int y, int w, int h, uint32_t pixel);

// Miscellaneous.

uint32_t dglConvertColor(uint32_t format, float r, float g, float b);

DGL_INLINE_ONLY static void dglSetReadYOffset(dglContext *context, int yoffset) {
	context->read_yoffset = yoffset;
}

DGL_INLINE_ONLY static void dglSetDrawYOffset(dglContext *context, int yoffset) {
	context->draw_yoffset = yoffset;
}

DGL_INLINE_ONLY static void dglSetReadPage(dglContext *context, int page) {
	context->read_yoffset = page * context->read_fb->yres;
}

DGL_INLINE_ONLY static void dglSetDrawPage(dglContext *context, int page) {
	context->draw_yoffset = page * context->draw_fb->yres;
}

// Clipping functions.

DGL_INLINE_ONLY static void dglSetClipRectangleFromFramebufferDimensions(dglFB *fb,
dglClipRectangle &cr) {
	cr.x1 = 0;
	cr.y1 = 0;
	cr.x2 = fb->xres;
	cr.y2 = fb->yres;
}

DGL_INLINE_ONLY static void dglSetClipRectangle(int x1, int y1, int x2, int y2,
dglClipRectangle &cr) {
	cr.x1 = x1;
	cr.y1 = y1;
	cr.x2 = x2;
	cr.y2 = y2;
}

DGL_INLINE_ONLY static void dglClip(const dglClipRectangle *cr, int& x, int& y) {
        if (x < cr->x1)
                x = cr->x1;
        if (y < cr->y1)
                y = cr->y1;
        if (x >= cr->x2)
                x = cr->x2 - 1;
        if (y >= cr->y2)
                y = cr->y2 - 1;
}

#define DGL_GET_READ_FB(_context, _fb) { _fb = _context->read_fb; }
#define DGL_GET_DRAW_FB(_context, _fb) { _fb = _context->draw_fb; }

#define DGL_GET_FB_TYPE(_fb) (_fb->flags & DGL_FB_TYPE_MASK)

#define DGL_FORMAT_GET_BYTES_PER_PIXEL(format) (4 - ((format & DGL_FORMAT_PIXEL_SIZE_16_BIT) >> 1))

// Inline PutPixel functions

DGL_INLINE_ONLY void dglPutPixel32(dglContext *context, int x, int y, uint32_t pixel) {
	y += context->draw_yoffset;
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	uint8_t *dp = fb->framebuffer_addr + y * fb->stride + x * 4;
	*((uint32_t *)dp) = pixel;
}

DGL_INLINE_ONLY void dglPutPixel16(dglContext *context, int x, int y, uint32_t pixel) {
	y += context->draw_yoffset;
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	uint8_t *dp = fb->framebuffer_addr + y * fb->stride + x * 2;
	*((uint16_t *)dp) = pixel;
}


#endif
