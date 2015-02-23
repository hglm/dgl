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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <dstTimer.h>
#include <dstRandom.h>

#include "dgl.h"

// Duration of each benchmark in microseconds.
#define BENCHMARK_DURATION 2000000
// Number of pixel rows used during CopyArea test.
#define COPY_HEIGHT 256
// Number of pixel rows used during Fill test.
#define FILL_HEIGHT 256
// Size of the image used during PutImage test.
#define PUT_IMAGE_WIDTH 256
#define PUT_IMAGE_HEIGHT 256
// Duration of the animated demo.
#define DEMO_DURATION 10000000

// Fill pattern parameters.
#define PATTERN_HEIGHT 32
#define PATTERN_WIDTH 32

enum {
	// Animated demo variant where the new frame in drawn directly into
	// a framebuffer that has multiple pages, using page flipping.
	DEMO_MODE_PAGEFLIP,
	// Animated demo variant where the new frame is drawn into the second
	// page of a framebuffer that has multiple pages, and then copied
	// each frame to the first page using DMA CopyArea.
	DEMO_MODE_DMA,
	// Animated demo variant where the new frame is drawn into an offscreen
	// pixmap each frame and copied to the screen framebuffer.
	DEMO_MODE_MEMCPY,
};

static dstRNG *rng;

static void DrawPattern(dglContext *context) {
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	for (int i = 0; i < PATTERN_HEIGHT; i++) {
		int y = fb->yres * i / PATTERN_HEIGHT;
		int h = fb->yres * (i + 1) / PATTERN_HEIGHT - y;
		for (int j = 0; j < PATTERN_WIDTH; j++) {
			int x = fb->xres * j /  PATTERN_WIDTH;
			int w = fb->xres * (j + 1) / PATTERN_WIDTH - x;
			uint32_t pixel = dglConvertColor(fb->format,
				rng->RandomFloat(1.0f),	rng->RandomFloat(1.0f),
				rng->RandomFloat(1.0f));
			dglFill(context, x, y, w, h, pixel);
		}
	}
}

static dglImage *CreateImage(dglContext *console_context) {
	dglFB *console_fb;
	DGL_GET_DRAW_FB(console_context, console_fb);
	dglImage *image = dglCreateImage(console_fb->format,
		PUT_IMAGE_WIDTH, PUT_IMAGE_HEIGHT);
	// Draw into the image.
	dglContext *context = dglCreateContext(NULL, image);
	float x_center = (float)image->xres / 2 - 0.5f;
	float y_center = (float)image->yres / 2 - 0.5f;
	float max_dist = sqrtf((float)image->xres * image->xres / 4 +
		image->yres * image->yres / 4);
	for (int y = 0; y < image->yres; y++)
		for (int x = 0; x < image->xres; x++) {
			float dx = fabsf(x - x_center);
			float dy = fabsf(y - y_center);
			float dist = sqrtf(dx * dx + dy * dy);
			float r, g, b;
			r = 1.0f - (dist / max_dist);
			g = fmodf(dist / max_dist, 0.2f) / 0.3f;
			b = 0.5f - (dist / max_dist) * 0.5f;
			uint32_t pixel = dglConvertColor(image->format,
				r, g, b);
			dglPutPixel(context, x, y, pixel);
		}
	dglDestroyContext(context);
	return image;
}

static uint64_t FillTest(dglContext *context, dstThreadedTimeout *tt) {
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	int n = 0;
	for (;;) {
		uint32_t pixel = dglConvertColor(fb->format,
			rng->RandomFloat(1.0f),	rng->RandomFloat(1.0f),
			rng->RandomFloat(1.0f));
		int y = rng->RandomInt(fb->yres - FILL_HEIGHT);
		dglFill(context, 0, y, fb->xres, FILL_HEIGHT, pixel);
		n++;
		if (tt->StopSignalled())
			break;
	}
	return (uint64_t)n * fb->xres * FILL_HEIGHT;
}

static uint64_t CopyTest(dglContext *context, dstThreadedTimeout *tt) {
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	int n = 0;
	for (;;) {
		// Copy from the bottom half of the screen to the top half.
		int y1 = fb->yres / 2 + rng->RandomInt(fb->yres / 2 - COPY_HEIGHT);
		int y2 = rng->RandomInt(fb->yres / 2 - COPY_HEIGHT);
		dglCopyArea(context, 0, y1, 0, y2, fb->xres, COPY_HEIGHT);
		n++;
		if (tt->StopSignalled())
			break;
	}
	return (uint64_t)n * fb->xres * COPY_HEIGHT;
}

static uint64_t PutImageTest(dglContext *context, dstThreadedTimeout *tt,
dglImage *image) {
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	int n = 0;
	for (;;) {
		int x = rng->RandomInt(fb->xres - image->xres);
		int y = rng->RandomInt(fb->yres - image->yres);
		dglPutImage(context, x, y, image);
		n++;
		if (tt->StopSignalled())
			break;
	}
	return (uint64_t)n * image->xres * image->yres;
}

static void PageFlipTest(dglContext *context, int max_pages) {
	dglFB *fb;
	DGL_GET_DRAW_FB(context, fb);
	int nu_pages = maxi(((dglScreenFB *)fb)->nu_pages, max_pages);
	for (int i = 0; i < nu_pages; i++) {
		float r, g, b;
		r = g = b = 0.0f;
		if (i == 0)
			r = 1.0f;
		else if (i == 1)
			g = 1.0f;
		else
			b = 1.0f;
		uint32_t pixel = dglConvertColor(fb->format, r, g, b);
		dglSetDrawPage(context, i);
		dglFill(context, 0, 0, fb->xres, fb->yres, pixel);
	}
	for (int i = 0; i < 10; i++) {
		int j = i % nu_pages;
		dglSetDisplayPage((dglScreenFB *)fb, j);
		sleep(1);
	}
	dglSetDisplayPage((dglScreenFB *)fb, 0);
	dglSetDrawPage(context, 0);
}

class MovingObject {
public :
	float x;
	float y;
	float velocity;
	float heading;
	float rgb[3];
	float size;
	float turn;
};

#define NU_MOVING_OBJECTS 64
#define MAX_VELOCITY 100.0f
#define MAX_OBJECT_RADIUS 35.0f

// Animated demo showing squares of different sizes moving with
// different velocities and varying directions. Intended to demonstrate
// page flipping and animation techniques using an off-screen buffer.

static float AnimatedDemo(dglContext *context, int mode, int max_pages,
bool vsync, bool half_size) {
	dglFB *console_fb, *pixmap_fb;
	DGL_GET_DRAW_FB(context, console_fb);
	int window_x = 0;
	int window_y = 0;
	int window_w = console_fb->xres;
	int window_h = console_fb->yres;
	if (half_size) {
		window_w = console_fb->xres / 2;
		window_h = console_fb->yres / 2;
		window_x = (console_fb->xres - window_w) / 2;
		window_y = (console_fb->yres - window_h) / 2;
	}
	MovingObject *object = new MovingObject[NU_MOVING_OBJECTS];
	for (int i = 0; i < NU_MOVING_OBJECTS; i++) {
		float scale_factor = 1.0f;
		if (i >= NU_MOVING_OBJECTS / 6) {
			scale_factor = 0.7f;
			if (i >= NU_MOVING_OBJECTS / 4)
				scale_factor = 0.3f;
		}
		object[i].x = rng->RandomFloat(window_w);
		object[i].y = rng->RandomFloat(window_h);
		object[i].velocity = rng->RandomFloat(MAX_VELOCITY / scale_factor);
		object[i].heading = rng->RandomFloat(M_PI * 2);
		object[i].turn = rng->RandomInt(3) * 0.1f * M_PI - 0.1f * M_PI;
		object[i].size = MAX_OBJECT_RADIUS * scale_factor;
		for (;;) {
			for (int j = 0; j < 3; j++)
				object[i].rgb[j] = rng->RandomFloat(1.0f);
			if (object[i].rgb[0] + object[i].rgb[1] + object[i].rgb[2] >= 0.3f)
				break;
		}
	}
	int draw_page = 0;
	if (mode == DEMO_MODE_DMA) {
		// Draw into offscreen framebuffer page.
		dglSetDrawPage(context, 1);
	}
	else if (mode == DEMO_MODE_PAGEFLIP) {
		// Draw into successive framebuffer pages each frame.
		draw_page = 0;
		// Clear the pages.
		for (int i = 0; i < max_pages; i++) {
			dglSetDrawPage(context, i);
			dglFill(context, 0, 0, console_fb->xres,
				console_fb->yres, 0x000000);
		}
		dglSetDrawPage(context, 0);
	}
	else {	// DEMO_MODE_MEMCPY
		// Draw into offscreen buffer in regular memory.
		pixmap_fb = dglCreatePixmapFB(console_fb->format,
			window_w, window_h);
		dglSetReadFramebuffer(context, pixmap_fb);
		dglSetDrawFramebuffer(context, pixmap_fb);
	}
	dglClipRectangle clip_rect;
	if (mode == DEMO_MODE_MEMCPY)
		dglSetClipRectangleFromFramebufferDimensions(pixmap_fb, clip_rect);
	else {
		dglSetClipRectangle(window_x, window_y, window_x + window_w,
			window_y + window_h, clip_rect);
	}
	dstThreadedTimeout *tt = new dstThreadedTimeout;
	tt->Start(DEMO_DURATION);
	int nu_frames = 0;
	dstTimer timer2;
	timer2.Start();
	dstTimer timer;
	timer.Start();
	for (;;) {
		// Draw objects.
		if (mode == DEMO_MODE_MEMCPY) {
			dglFill(context, 0, 0, window_w, window_h, 0);
		}
		else
			dglFill(context, window_x, window_y, window_w, window_h, 0);
		for (int i = 0; i < NU_MOVING_OBJECTS; i++) {
			int x1 = object[i].x - object[i].size;
			int y1 = object[i].y - object[i].size;
			int x2 = object[i].x + object[i].size;
			int y2 = object[i].y + object[i].size;
			if (mode != DEMO_MODE_MEMCPY) {
				x1 += window_x;
				y1 += window_y;
				x2 += window_x;
				y2 += window_y;
			}
			dglClip(&clip_rect, x1, y1);
			dglClip(&clip_rect, x2, y2);
			uint32_t pixel = dglConvertColor(console_fb->format,
				object[i].rgb[0],
				object[i].rgb[1], object[i].rgb[2]);
			dglFill(context, x1, y1, x2 - x1, y2 - y1, pixel);
		}
		if (mode == DEMO_MODE_DMA) {
			dglSetDrawPage(context, 0);
			dglSetReadPage(context, 1);
			if (vsync)
				dglWaitVSync((dglScreenFB *)console_fb);
			dglCopyArea(context, window_x, window_y, window_x, window_y,
				window_w, window_h);
			dglSetDrawPage(context, 1);
		}
		else if (mode == DEMO_MODE_PAGEFLIP) {
			if (vsync)
				dglWaitVSync((dglScreenFB *)console_fb);
			dglSetDisplayPage((dglScreenFB *)console_fb, draw_page);
			int nu_pages = maxi(((dglScreenFB *)console_fb)->nu_pages, max_pages);
			draw_page = (draw_page + 1) % nu_pages;
			dglSetDrawPage(context, draw_page);
		}
		else {
			dglSetDrawFramebuffer(context, console_fb);
			// Copy offscreen pixmap to screen.
			if (vsync)
				dglWaitVSync((dglScreenFB *)console_fb);
			dglCopyArea(context, 0, 0, window_x, window_y,
                                window_w, window_h);
			dglSetDrawFramebuffer(context, pixmap_fb);
		}
		nu_frames++;
		if (tt->StopSignalled())
			break;
		float dt = timer.Elapsed();
		// Update object positions.
		for (int i = 0; i < NU_MOVING_OBJECTS; i++) {
			object[i].heading += dt * object[i].turn;
			float dx = dt * object[i].velocity * cosf(object[i].heading);
			float dy = dt * object[i].velocity * sinf(object[i].heading);
			object[i].x += dx;
			object[i].y += dy;
			// Change turn direction on average once every 10 seconds
			if (rng->RandomFloat(1.0f) < 0.1f * dt)
				object[i].turn = rng->RandomInt(3) * 0.2f * M_PI - 0.1f * M_PI;
		}
	}
	if (mode == DEMO_MODE_PAGEFLIP)
		dglSetDisplayPage((dglScreenFB *)console_fb, 0);
	else if (mode == DEMO_MODE_MEMCPY) {
		dglSetReadFramebuffer(context, console_fb);
		dglSetDrawFramebuffer(context, console_fb);
		dglDestroyPixmapFB(pixmap_fb);
	}
	return nu_frames / timer2.Elapsed();
}

int main(int argc, char *argv[]) {
	bool copyarea_dma = false;
	bool copyarea_memcpy = false;
	bool fill_nodma = false;
	bool putimage_memcpy = false;
	bool test_pageflip = false;
	bool demo_dma = false;
	bool demo_pageflip = false;
	bool demo_memcpy = false;
	int max_pages = 3;
	bool vsync = false;
	bool demo_half_size = false;
	if (argc == 1) {
		printf("test-dgl: Test extended framebuffer for RPi.\n"
			"Syntax: test-dgl [commands/options]\n\n"
			"Commands:\n\n"
			"copyarea-dma      Benchmark CopyArea performance using DMA.\n"
			"copyarea-memcpy   Benchmark CopyArea performance using memcpy.\n"
			"fill              Benchmark Fill performance without DMA.\n"
			"putimage          Benchmark PutImage performance without DMA.\n"
			"test-pageflip     Page-flipping test (should show red, green, and possibly blue).\n"
			"demo-dma          Perform animated demo using DMA from offscreen buffer.\n"
			"demo-pageflip     Perform amimated demo using page-flipping.\n"
			"demo-memcpy       Perform animated demo using memcpy from offscreen buffer.\n\n"
			"Options:\n\n"
			"double-buffer     Use double-buffering instead of triple-buffering when using \n"
			"                  page flipping.\n"
			"vsync             Force wait for vsync after drawing each frame.\n"
			"half-size         Use half the display resolution for the animated demo window.\n");
		exit(0);
	}
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[1], "copyarea-dma") == 0)
			copyarea_dma = true;
		else if (strcmp(argv[i], "copyarea-memcpy") == 0)
			copyarea_memcpy = true;
		else if (strcmp(argv[i], "fill") == 0)
			fill_nodma = true;
		else if (strcmp(argv[i], "putimage") == 0)
			putimage_memcpy = true;
		else if (strcmp(argv[i], "test-pageflip") == 0)
			test_pageflip = true;
		else if (strcmp(argv[i], "demo-dma") == 0)
			demo_dma = true;
		else if (strcmp(argv[i], "demo-pageflip") == 0)
			demo_pageflip = true;
		else if (strcmp(argv[i], "demo-memcpy") == 0)
			demo_memcpy = true;
		else if (strcmp(argv[i], "double-buffer") == 0)
			max_pages = 2;
		else if (strcmp(argv[i], "vsync") == 0)
			vsync = true;
		else if (strcmp(argv[i], "half-size") == 0)
			demo_half_size = true;
		else {
			printf("test-dgl: Unrecognized option.\n");
			exit(1);
		}
	}

	dglConsoleFB *cfb = dglCreateConsoleFramebuffer();
	if (cfb == NULL) {
		printf("Initialization error.\n");
		exit(1);
	}
	dglContext *context = dglCreateContext(cfb, cfb);

	if (test_pageflip && (cfb->flags & DGL_FB_FLAG_HAVE_PAN_DISPLAY) == 0) {
		test_pageflip = false;
		printf("Page-flipping test (test_pageflip): PanDisplay not available.\n");
	}
	if (copyarea_dma && (cfb->flags & DGL_FB_FLAG_HAVE_COPY_AREA) == 0) {
		copyarea_dma = false;
		printf("CopyArea benchmark (copyarea_dma): accelerated DMA CopyArea not available.\n");
	}
	if (demo_dma && (cfb->flags & DGL_FB_FLAG_HAVE_COPY_AREA) == 0) {
		demo_dma = false;
		printf("Animated demo (demo_dma): accelerated DMA CopyArea not available.\n");
	}
	if (demo_dma && dglGetNumberOfPages(cfb) < 2) {
		demo_dma = false;
		printf("Animated demo (demo_dma): Need more than one framebuffer page.\n");
	}
	if (demo_pageflip && (cfb->flags & DGL_FB_FLAG_HAVE_PAN_DISPLAY) == 0) {
		demo_pageflip = false;
		printf("Animated demo (demo_pageflip): PanDisplay not available.\n");
	}
	rng = dstGetDefaultRNG();

	dstThreadedTimeout *tt = new dstThreadedTimeout;
	dstTimer timer;

	double elapsed_fill, elapsed_copyarea_memcpy, elapsed_copyarea_dma,
		elapsed_putimage_memcpy;
	uint64_t pixels_fill, pixels_copyarea_memcpy, pixels_copyarea_dma,
		pixels_putimage_memcpy;
	if (fill_nodma) {
		tt->Start(BENCHMARK_DURATION);
		timer.Start();
		pixels_fill = FillTest(context, tt);
		elapsed_fill = timer.Elapsed();
	}
	if (copyarea_memcpy) {
		DrawPattern(context);
		int flags = cfb->flags;
		cfb->flags &= ~DGL_FB_FLAG_HAVE_COPY_AREA;
		tt->Start(BENCHMARK_DURATION);
		timer.Start();
		pixels_copyarea_memcpy = CopyTest(context, tt);
		elapsed_copyarea_memcpy = timer.Elapsed();
		cfb->flags = flags;
	}
	if (copyarea_dma && (cfb->flags & DGL_FB_FLAG_HAVE_COPY_AREA)) {
		DrawPattern(context);
		int flags = cfb->flags;
		cfb->flags |= DGL_FB_FLAG_HAVE_COPY_AREA;
		tt->Start(BENCHMARK_DURATION);
		timer.Start();
		pixels_copyarea_dma = CopyTest(context, tt);
		elapsed_copyarea_dma = timer.Elapsed();
		cfb->flags = flags;
	}

	if (putimage_memcpy) {
		dglImage *image = CreateImage(context);
		tt->Start(BENCHMARK_DURATION);
		timer.Start();
		pixels_putimage_memcpy = PutImageTest(context, tt, image);
		elapsed_putimage_memcpy = timer.Elapsed();
		dglDestroyImage(image);
	}

	if (test_pageflip) {
		PageFlipTest(context, max_pages);
	}

	float fps_pageflip, fps_dma, fps_memcpy;
	if (demo_pageflip)
		fps_pageflip = AnimatedDemo(context, DEMO_MODE_PAGEFLIP, max_pages, vsync,
			demo_half_size);
	if (demo_dma)
		fps_dma = AnimatedDemo(context, DEMO_MODE_DMA, max_pages, vsync,
			demo_half_size);
	if (demo_memcpy)
		fps_memcpy = AnimatedDemo(context, DEMO_MODE_MEMCPY, max_pages, vsync,
			demo_half_size);

	if (fill_nodma || copyarea_memcpy || copyarea_dma || putimage_memcpy
	|| test_pageflip || demo_pageflip || demo_dma || demo_memcpy) {
		// Clear the screen if any tests were performed.
		dglSetDrawPage(context, 0);
		dglFill(context, 0, 0, cfb->xres, cfb->yres, 0x000000);
	}

	const char *info_str = dglGetInfoString(cfb);
	dglDestroyConsoleFramebuffer(cfb);
//	system("clear");
	printf("%s", info_str);
	delete [] info_str;

	double throughput_fill, throughput_memcpy, throughput_dma, throughput_putimage_memcpy;
	if (fill_nodma) {
		throughput_fill = pixels_fill / elapsed_fill;
		printf("Fill pixel throughput (software fill): %.5G Mpix/s (%.5G MB/s)\n",
			throughput_fill / pow(10.0d, 6.0d),
			throughput_fill * cfb->bytes_per_pixel / pow(2.0d, 20.0d));
	}
	if (putimage_memcpy) {
		throughput_putimage_memcpy = pixels_putimage_memcpy / elapsed_putimage_memcpy;
		printf("PutImage (%dx%d) pixel throughput: %.5G Mpix/s (%.5G MB/s)\n",
			PUT_IMAGE_WIDTH, PUT_IMAGE_HEIGHT,
			throughput_putimage_memcpy / pow(10.0d, 6.0d),
			throughput_putimage_memcpy * cfb->bytes_per_pixel / pow(2.0d, 20.0d));
	}
	if (copyarea_memcpy) {
		throughput_memcpy = pixels_copyarea_memcpy / elapsed_copyarea_memcpy;
		printf("CopyArea pixel throughput (software blit): %.5G Mpix/s (%.5G MB/s)\n",
			throughput_memcpy / pow(10.0d, 6.0d),
			throughput_memcpy * cfb->bytes_per_pixel / pow(2.0d, 20.0d));
	}
	if (copyarea_dma) {
		throughput_dma = pixels_copyarea_dma / elapsed_copyarea_dma;
		printf("CopyArea pixel throughput (DMA ioctl): %.5G Mpix/s (%.5G MB/s)\n",
			throughput_dma / pow(10.0d, 6.0d),
			throughput_dma * cfb->bytes_per_pixel / pow(2.0d, 20.0d));
	}
	if (demo_dma)
		printf("Demo (DMA) fps: %f\n", fps_dma);
	if (demo_pageflip)
		printf("Demo (page flip) fps: %f\n", fps_pageflip);
	if (demo_memcpy)
		printf("Demo (memcpy) fps: %f\n", fps_memcpy);

	exit(0);
}

