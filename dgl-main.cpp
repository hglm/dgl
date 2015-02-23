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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <cstdarg>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#ifdef DGL_USE_PIXMAN
#include <pixman.h>
#endif

#include "dgl.h"


// General functions.

static int dgl_internal_debug_message_level = DGL_MESSAGE_INFO;

void dglMessage(int priority, const char *format, ...) {
	if (priority > dgl_internal_debug_message_level)
		return;
	va_list args;
	va_start(args, format);
	printf("dgl: ");
	if (priority == DGL_MESSAGE_WARNING)
		printf("WARNING: ");
	else if (priority == DGL_MESSAGE_CRITICAL)
		printf("CRITICAL: ");
	vprintf(format, args);
	va_end(args);
	if (priority <= DGL_MESSAGE_WARNING)
		fflush(stdout);
	if (priority == DGL_MESSAGE_FATAL_ERROR)
		raise(SIGABRT);
}

void dglSetDebugMessageLevel(int level) {
	dgl_internal_debug_message_level = level;
}

// Pixmap framebuffer.

dglFB *dglCreatePixmapFB(uint32_t format, int w, int h) {
	dglFB *fb = new dglFB;
	fb->format = format;
	fb->bytes_per_pixel = DGL_FORMAT_GET_BYTES_PER_PIXEL(format);
	fb->xres = w;
	fb->yres = h;
	fb->stride = w * fb->bytes_per_pixel;
	fb->total_size = h * fb->stride;
	fb->framebuffer_addr = new uint8_t[h * fb->stride];
	fb->flags = DGL_FB_TYPE_PIXMAP;
	return fb;
}

void dglDestroyPixmapFB(dglFB *fb) {
	delete fb->framebuffer_addr;
	delete fb;
}

// Screen framebuffer

void dglPanDisplay(dglScreenFB *fb, int x, int y) {
	if (fb->flags & DGL_FB_FLAG_HAVE_PAN_DISPLAY)
		fb->PanDisplayFunc(fb, x, y);
}

void dglSetDisplayPage(dglScreenFB *fb, int page) {
        dglPanDisplay(fb, 0, page * fb->yres);
}

void dglWaitVSync(dglScreenFB *fb) {
	if (fb->flags & DGL_FB_FLAG_HAVE_WAIT_VSYNC)
		fb->WaitVSyncFunc(fb);
}

DGL_INLINE_ONLY static void dglCopyArea(dglScreenFB *fb, int sx, int sy, int dx, int dy, int w, int h) {
	if (fb->flags & DGL_FB_FLAG_HAVE_COPY_AREA)
		fb->CopyAreaFunc(fb, sx, sy, dx, dy, w, h);
}

// Contexts

dglContext *dglCreateContext(dglFB *read_fb, dglFB *draw_fb) {
	dglContext *context = new dglContext;
	context->read_fb = read_fb;
	context->draw_fb = draw_fb;
	context->read_yoffset = 0;
	context->draw_yoffset = 0;
	return context;
}

void dglDestroyContext(dglContext *context) {
	delete context;
}

void dglSetReadFramebuffer(dglContext *context, dglFB *fb) {
	context->read_fb = fb;
}

void dglSetDrawFramebuffer(dglContext *context, dglFB *fb) {
	context->draw_fb = fb;
}

// Image handling.

dglImage *dglCreateImageFromBuffer(uint32_t format, int w, int h, uint8_t *buffer) {
	dglImage *image = new dglImage;
	image->framebuffer_addr = buffer;
	image->xres = w;
	image->yres = h;
	image->format = format;
	image->bytes_per_pixel = DGL_FORMAT_GET_BYTES_PER_PIXEL(format);
	image->stride = w * image->bytes_per_pixel;
	image->total_size = h * image->stride;
	image->flags = DGL_FB_TYPE_IMAGE;
	return image;
}

dglImage *dglCreateImage(uint32_t format, int w, int h) {
	uint8_t *buffer = new uint8_t[w * h * DGL_FORMAT_GET_BYTES_PER_PIXEL(format)];
	return dglCreateImageFromBuffer(format, w, h, buffer);
}

void dglDestroyImage(dglImage *image) {
	delete [] image->framebuffer_addr;
	delete image;
}

// Generic drawing functions.

void dglPutPixel(dglContext *context, int x, int y, uint32_t pixel) {
	y += context->draw_yoffset;
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	uint8_t *dp = fb->framebuffer_addr + y * fb->stride +
		x * fb->bytes_per_pixel;
	if (fb->bytes_per_pixel == 4)
		*((uint32_t *)dp) = pixel;
	else
		*((uint16_t *)dp) = pixel;
}

// Uncomplicated region copy within the same framebuffer. Detects whether
// top-to-bottom blit is needed.

static void dglCopyAreaSimple(dglFB *fb, int sx, int sy, int dx, int dy, int w, int h) {
	uint8_t *sp = fb->framebuffer_addr + sy * fb->stride +
		sx * fb->bytes_per_pixel;
	uint8_t *dp = fb->framebuffer_addr + dy * fb->stride +
		dx * fb->bytes_per_pixel;
	int stride = fb->stride;
	if (w * fb->bytes_per_pixel == fb->stride && (dy < sy || dy >= sy + h)) {
		// Contiguous area.
		memcpy(dp, sp, fb->stride * h);
		return;
	}
	if (dy > sy) {
		// Blit from bottom to top.
		stride = - fb->stride;
		sp += (h - 1) * fb->stride;
		dp += (h - 1) * fb->stride;
	}
	while (h > 0) {
		memcpy(dp, sp, w * fb->bytes_per_pixel);
		sp += stride;
		dp += stride;
		h--;
	}
}

// Complicated blit (dy == sy, dx > sx) that requires right-to-left processing or
// a scratch buffer.

static void dglCopyAreaDifficult(dglFB *fb, int sx, int sy, int dx, int dy, int w, int h) {
	uint8_t *sp = fb->framebuffer_addr + sy * fb->stride +
		sx * fb->bytes_per_pixel;
	uint8_t *dp = fb->framebuffer_addr + dy * fb->stride +
		dx * fb->bytes_per_pixel;
	int stride = fb->stride;
	if (w * fb->bytes_per_pixel == fb->stride && (dy < sy || dy >= sy + h)) {
		// Contiguous area.
		memcpy(dp, sp, fb->stride * h);
		return;
	}
	if (dy > sy) {
		// Blit from bottom to top.
		stride = - fb->stride;
		sp += (h - 1) * fb->stride;
		dp += (h - 1) * fb->stride;
	}
	uint8_t *scratch_buffer = new uint8_t[w * fb->bytes_per_pixel];
	while (h > 0) {
		memcpy(scratch_buffer, sp, w * fb->bytes_per_pixel);
		memcpy(dp, scratch_buffer, w * fb->bytes_per_pixel);
		sp += stride;
		dp += stride;
		h--;
	}
	delete [] scratch_buffer;
}

// Copy area across different framebuffers, same pixel format.

#ifndef DGL_USE_PIXMAN

static void dglCopyAreaAcross(dglFB *read_fb, dglFB *draw_fb, int sx, int sy,
int dx, int dy, int w, int h) {
	uint8_t *sp = read_fb->framebuffer_addr + sy * read_fb->stride +
		sx * draw_fb->bytes_per_pixel;
	uint8_t *dp = draw_fb->framebuffer_addr + dy * draw_fb->stride +
		dx * draw_fb->bytes_per_pixel;
	if (w * draw_fb->bytes_per_pixel == read_fb->stride &&
	read_fb->stride == draw_fb->stride) {
		// Contiguous area.
		memcpy(dp, sp, draw_fb->stride * h);
		return;
	}
	while (h > 0) {
		memcpy(dp, sp, w * draw_fb->bytes_per_pixel);
		sp += read_fb->stride;
		dp += draw_fb->stride;
		h--;
	}
}

#endif

#ifdef DGL_USE_PIXMAN

// Copy area using pixman library blit function. 

DGL_INLINE_ONLY static void dglCopyAreaBasicPixman(dglFB *read_fb, dglFB *draw_fb, int sx, int sy,
int dx, int dy, int w, int h) {
	pixman_blt(
		(uint32_t *)read_fb->framebuffer_addr,
		(uint32_t *)draw_fb->framebuffer_addr,
		read_fb->stride / 4, draw_fb->stride / 4,
		read_fb->bytes_per_pixel * 8, draw_fb->bytes_per_pixel * 8,
		sx, sy, dx, dy, w, h);
}

#endif

#define HORIZONTAL_BLT_PIXEL_MARGIN 0

void dglCopyArea(dglContext *context, int sx, int sy, int dx, int dy, int w, int h) {
	if (w <= 0 || h <= 0)
		return;
	sy += context->read_yoffset;
	dy += context->draw_yoffset;

	dglFB *read_fb, *draw_fb;
	DGL_GET_READ_FB(context, read_fb);
	DGL_GET_DRAW_FB(context, draw_fb);
	// Check whether the read and draw framebuffers are the same.
	if (read_fb == draw_fb) {
		// If the framebuffer supports accelerated copy area blits, use that,
		if (draw_fb->flags & DGL_FB_FLAG_HAVE_COPY_AREA) {
			dglScreenFB *fb = (dglScreenFB *)draw_fb;
			dglCopyArea(fb, sx, sy, dx, dy, w, h);
			return;
		}

#ifdef DGL_USE_PIXMAN
		// Pixman only supports a basic top-down, left-to-right blit. That
		// means the regions must not overlap or dy < sy.
		bool basic = (dy < sy) || (dy > sy + h) ||
			(dx + w < sx) || (dx >= sx + w);
		if (basic) {
			dglCopyAreaBasicPixman(read_fb, draw_fb, sx, sy, dx, dy, w, h);
			return;
		}
		// The regions overlap. Check whether either a top-to-bottom or
		// bottom-to-top blit is sufficient.
		bool simple = (dx < sx - HORIZONTAL_BLT_PIXEL_MARGIN);
#else
		// Check whether either a top-to-bottom or bottom-to-top blit is sufficient.
		bool simple = (dy < sy) || (dy >= sy + h) ||
			(dx < sx - HORIZONTAL_BLT_PIXEL_MARGIN) || (dx >= sx + w);
#endif
		if (simple) {
			dglCopyAreaSimple(draw_fb, sx, sy, dx, dy, w, h);
			return;
		}
		dglCopyAreaDifficult(draw_fb, sx, sy, dx, dy, w, h);
		dglMessage(DGL_MESSAGE_WARNING,
			"dglCopyArea: Difficult software blit not yet supported.\n");
		return;
	}

	if (read_fb->bytes_per_pixel == draw_fb->bytes_per_pixel)
#ifdef DGL_USE_PIXMAN
		dglCopyAreaBasicPixman(read_fb, draw_fb, sx, sy, dx, dy, w, h);
#else
		dglCopyAreaAcross(read_fb, draw_fb, sx, sy, dx, dy, w, h);
#endif
	else
		dglMessage(DGL_MESSAGE_WARNING,
			"dglCopyArea: Read and draw framebuffers differ in format.\n");
}

void dglPutImage(dglContext *context, int x, int y, dglImage *image) {
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
#ifdef DGL_USE_PIXMAN
	dglCopyAreaBasicPixman(image, fb, 0, 0, x, y, image->xres, image->yres);
#else
	dglCopyAreaAcross(image, fb, 0, 0, x, y, image->xres, image->yres);
#endif
}

void dglPutPartialImage(dglContext *context, int sx, int sy, int dx, int dy, int w, int h,
dglImage *image) {
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
#ifdef DGL_USE_PIXMAN
	dglCopyAreaBasicPixman(image, fb, sx, sy, dx, dy, w, h);
#else
	dglCopyAreaAcross(image, fb, sx, sy, dx, dy, w, h);
#endif
}

#ifndef DGL_USE_PIXMAN

static void memset32(uint8_t *destp, uint32_t value, int size) {
	uint32_t *dp = (uint32_t *)destp;
	while (size >= 4) {
		dp[0] = value;
		dp[1] = value;
		dp[2] = value;
		dp[3] = value;
		dp += 4;
		size -= 4;
	}
	while (size > 0) {
		*dp = value;
		dp++;
		size--;
	}
}

static void memset16(uint8_t *destp, uint32_t value, int size) {
	uint16_t *dp = (uint16_t *)destp;
	if ((uintptr_t)dp & 0x2) {
		*dp = (uint16_t)value;
		dp++;
		size--;
	}
	uint32_t value32 = value | (value << 16);
	while (size >= 2) {
		uint32_t *dp32 = (uint32_t *)dp;
		*dp32 = value32;
		dp += 2;
		size -= 2;
	}
	if (size > 0)
		*dp = (uint16_t)value;
}

#endif

void dglFill(dglContext *context, int x, int y, int w, int h, uint32_t pixel) {
	if (w <= 0 || h <= 0)
		return;
	y += context->draw_yoffset;

	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);

#ifdef DGL_USE_PIXMAN
	bool r = pixman_fill((uint32_t *)fb->framebuffer_addr, fb->stride / 4,
		fb->bytes_per_pixel * 8, x, y, w, h, pixel);
	if (!r)
		dglMessage(DGL_MESSAGE_WARNING,
			"dlgFill: pixman fill unsuccesful\n");
#else
	uint8_t *dp = fb->framebuffer_addr + y * fb->stride +
		x * fb->bytes_per_pixel;
	if (fb->bytes_per_pixel == 4)
		while (h > 0) {
			memset32(dp, pixel, w);
			dp += fb->stride;
			h--;
		}
	else
		while (h > 0) {
			memset16(dp, pixel, w);
			dp += fb->stride;
			h--;
		}
#endif
}

uint32_t dglConvertColor(uint32_t format, float r_float, float g_float,
float b_float) {
	uint32_t r = floorf(r_float * 255.5f);
	uint32_t g = floorf(g_float * 255.5f);
	uint32_t b = floorf(b_float * 255.5f);
	if ((format & (DGL_FORMAT_PIXEL_SIZE_16_BIT |
	DGL_FORMAT_LSB_ORDER_RGB_BIT)) == 0)
		return (r << 16) + (g << 8) + b;
	else if ((format & (DGL_FORMAT_PIXEL_SIZE_16_BIT |
	DGL_FORMAT_LSB_ORDER_RGB_BIT)) == DGL_FORMAT_PIXEL_SIZE_16_BIT) {
		r += 4;
		if (r >= 256)
			r = 255;
		r >>= 3;
		g += 2;
		if (g >= 256)
			g = 255;
		g >>= 2;
		b += 4;
		if (b >= 256)
			b = 255;
		b >>= 3;
		return (r << 11) + (g << 5) + b;
	}
	dglMessage(DGL_MESSAGE_WARNING,
		"dlgConvertPixel: Cannot handle pixel format 0x%04X\n",
		format);
	return 0;
}
