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

// Partly based on code from xf86-video-fbturbo which has the following
// copyright message:

/*
 * Copyright © 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "dgl.h"

/*
 * HACK: non-standard ioctl, which provides access to fb_copyarea accelerated
 * function in the kernel. It just accepts the standard fb_copyarea structure
 * defined in "linux/fb.h" header
 */
#define FBIOCOPYAREA            _IOW('z', 0x21, struct fb_copyarea)
/*
 * HACK: another non-standard ioctl, which is used to check whether the
 * fbdev kernel driver actually returns errors on unsupported ioctls.
 */
#define FBUNSUPPORTED           _IOW('z', 0x22, struct fb_copyarea)


// Console framebuffer.

// Functions for textmode restoration.

static bool saved_graphics_mode_set;

static void dglConsoleFBRestoreConsoleState() {
	// If graphics mode was already enabled before running the
	// program, do noting.
	if (!saved_graphics_mode_set)
		return;
	fflush(stdout);
	fflush(stderr);
	int tty = open("/dev/tty0", O_RDWR); 
	// First check whether the console is already in the mode to
	// be restored (this function may be called multiple times due
	// to signals and atexit).
	int current_kd_mode;
	if (ioctl(tty, KDGETMODE, &current_kd_mode) < 0)
		return;
	if (current_kd_mode == KD_TEXT) {
		close(tty);
		return;
	}
	ioctl(tty, KDSETMODE, KD_TEXT);
	usleep(100000);
	// Switch to another VT and back to restore the text content.
	struct vt_stat vtstat;
	ioctl(tty, VT_GETSTATE, &vtstat);
	int current_vt = vtstat.v_active;
	int temp_vt;
	if (current_vt == 1)
		temp_vt = 2;
	else
		temp_vt = 1;
	ioctl(tty, VT_ACTIVATE, temp_vt);
	ioctl(tty, VT_WAITACTIVE, temp_vt);
	ioctl(tty, VT_ACTIVATE, current_vt);
	ioctl(tty, VT_WAITACTIVE, current_vt);
	fflush(stdout);
	close(tty);
}

static struct sigaction signal_quit_oldact, signal_segv_oldact, signal_int_oldact,
	signal_abort_oldact;

static void signal_quit(int num, siginfo_t *info, void *p) {
	dglConsoleFBRestoreConsoleState();
	if (signal_quit_oldact.sa_flags & SA_SIGINFO)
		signal_quit_oldact.sa_sigaction(num, info, p);
	else
		signal_quit_oldact.sa_handler(num);
}

static void signal_segv(int num, siginfo_t *info, void *p) {
	dglConsoleFBRestoreConsoleState();
	if (signal_segv_oldact.sa_flags & SA_SIGINFO)
		signal_segv_oldact.sa_sigaction(num, info, p);
	else
		signal_segv_oldact.sa_handler(num);
}

static void signal_int(int num, siginfo_t *info, void *p) {
	dglConsoleFBRestoreConsoleState();
	if (signal_int_oldact.sa_flags & SA_SIGINFO)
		signal_int_oldact.sa_sigaction(num, info, p);
	else
		signal_int_oldact.sa_handler(num);
}

static void signal_abort(int num, siginfo_t *info, void *p) {
	dglConsoleFBRestoreConsoleState();
	if (signal_abort_oldact.sa_flags & SA_SIGINFO)
		signal_abort_oldact.sa_sigaction(num, info, p);
	else
		signal_abort_oldact.sa_handler(num);
}

static void dglConsoleFBInstallConsoleRestoreHandlers(
bool graphics_mode_set) {
	saved_graphics_mode_set = graphics_mode_set;
	atexit(dglConsoleFBRestoreConsoleState);
	struct sigaction act;
	act.sa_sigaction = signal_quit;
	__sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGQUIT, &act, &signal_quit_oldact);
	act.sa_sigaction = signal_segv;
	sigaction(SIGSEGV, &act, &signal_segv_oldact);
	act.sa_sigaction = signal_int;
	sigaction(SIGINT, &act, &signal_int_oldact);
	// SIGABRT is raised by assert().
	act.sa_sigaction = signal_abort;
	sigaction(SIGABRT, &act, &signal_abort_oldact);
}

// No-ops for accelerated function when not available.

static void dglConsoleFBPanDisplayNoOp(dglScreenFB *fb, int x, int y) {
}

static void dglConsoleFBWaitVSyncNoOp(dglScreenFB *fb) {
}

static void dglConsoleFBCopyAreaNoOp(dglScreenFB *fb, int sx, int sy, int dx, int dy, int w, int h) {
}

// Create and initialize console framebuffer.

dglConsoleFB *dglCreateConsoleFramebuffer() {
	struct fb_copyarea copyarea;

	const char *device = "/dev/fb0";
	int fd = open(device, O_RDWR);
	if (fd < 0) {
		dglMessage(DGL_MESSAGE_WARNING,
			"dglCreateConsoleFramebuffer: Cannot open %s", device);
		return NULL;
	}

	bool copyarea_supported = true;

	/*
	 * Check if the unsupported dummy ioctl fails. If it does not, then the
	 * kernel framebuffer driver is buggy and does not handle errors correctly.
	 */
	if (ioctl(fd, FBUNSUPPORTED, &copyarea) == 0)
		copyarea_supported = false;

	/*
	* Check whether the FBIOCOPYAREA ioctl is supported by requesting to do
	* a copy of 1x1 rectangle in the top left corner to itself
	*/
	if (copyarea_supported) {
		copyarea.sx = 0;
		copyarea.sy = 0;
		copyarea.dx = 0;
		copyarea.dy = 0;
		copyarea.width = 1;
		copyarea.height = 1;
		if (ioctl(fd, FBIOCOPYAREA, &copyarea) != 0)
			copyarea_supported = false;
	}
	int flags = DGL_FB_TYPE_CONSOLE;
	if (copyarea_supported)
		flags |= DGL_FB_FLAG_HAVE_COPY_AREA;

	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &fb_var) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &fb_fix) < 0) {
		close(fd);
		dglMessage(DGL_MESSAGE_WARNING, "dglCreateConsoleFramebuffer: "
			"Could not get screen info from kernel\n");
	        return NULL;
	}

	if (ioctl(fd, FBIO_WAITFORVSYNC, NULL) == 0)
		flags |= DGL_FB_FLAG_HAVE_WAIT_VSYNC;

	dglConsoleFB *cfb;
	uint8_t *framebuffer_addr;
	int kd_fd;
	bool graphics_mode, graphics_mode_set;

 	uint32_t format = 0;
	if (fb_var.bits_per_pixel == 32 &&
	((fb_var.blue.offset == 0 && fb_var.blue.length == 8 &&
	fb_var.green.offset == 8 && fb_var.green.length == 8 &&
	fb_var.red.offset == 16 && fb_var.red.length == 8) ||
	(fb_var.red.offset == 0 && fb_var.red.length == 8 &&
	fb_var.green.offset == 8 && fb_var.green.length == 8 &&
	fb_var.blue.offset == 16 && fb_var.blue.length == 8))) {
		// Truecolor
		if (fb_var.red.offset == 0)
			format |= DGL_FORMAT_LSB_ORDER_RGB_BIT;
		if (fb_var.transp.length == 8)
			format |= DGL_FORMAT_ALPHA_BIT;
		else if (fb_var.transp.length != 0)
			goto invalid_format;
	}
	else if (fb_var.bits_per_pixel == 16) {
		if ((fb_var.blue.offset == 0 && fb_var.blue.length == 5 &&
	        fb_var.green.offset == 5 && fb_var.green.length == 6 &&
        	fb_var.red.offset == 11 && fb_var.red.length == 5) ||
	        (fb_var.red.offset == 0 && fb_var.red.length == 5 &&
	        fb_var.green.offset == 6 && fb_var.green.length == 6 &&
        	fb_var.blue.offset == 11 && fb_var.blue.length == 5))
			format = DGL_FORMAT_RGB565;
		else if ((fb_var.blue.offset == 0 && fb_var.blue.length == 5 &&
	        fb_var.green.offset == 5 && fb_var.green.length == 6 &&
	        fb_var.red.offset == 11 && fb_var.red.length == 5) ||
	        (fb_var.red.offset == 0 && fb_var.red.length == 5 &&
	        fb_var.green.offset == 6 && fb_var.green.length == 6 &&
	        fb_var.blue.offset == 11 && fb_var.blue.length == 5))
			format = DGL_FORMAT_BGR565;
		else
			goto invalid_format;
	}
	else
		goto invalid_format;

	framebuffer_addr = (uint8_t *)mmap(0, fb_fix.smem_len,
        	PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (framebuffer_addr == MAP_FAILED) {
		close(fd);
		dglMessage(DGL_MESSAGE_WARNING,
			"dglCreateConsoleFramebuffer: Memory map failed\n");
		return NULL;
        }

	{
	graphics_mode = false;
	graphics_mode_set = false;
	kd_fd = open("/dev/tty0", O_RDWR);
	if (kd_fd >= 0) {
		int current_mode;
		int r = ioctl(kd_fd, KDGETMODE, &current_mode);
		if (r == 0) {
			if (current_mode == KD_TEXT) {
				if (ioctl(kd_fd, KDSETMODE, KD_GRAPHICS) == 0)
					graphics_mode_set = true;
					graphics_mode = true;
					dglConsoleFBInstallConsoleRestoreHandlers(
						true);
			}
			else
				graphics_mode = true;
		}
		close(kd_fd);
	}
	if (!graphics_mode)
		dglMessage(DGL_MESSAGE_WARNING, "dglCreateConsoleFramebuffer: "
			"Could not set graphics mode, superuser priviledges required?\n");
	}

	cfb = new dglConsoleFB;
	cfb->framebuffer_addr = framebuffer_addr;
	cfb->format = format;
	cfb->total_size = fb_fix.smem_len;
	cfb->bytes_per_pixel = (fb_var.bits_per_pixel + 7) / 8;
	cfb->xres = fb_var.xres;
	cfb->yres = fb_var.yres;
	cfb->stride = fb_fix.line_length;

	cfb->virtual_xres = cfb->xres;
	cfb->virtual_yres = cfb->total_size / cfb->stride;
	cfb->nu_pages = cfb->virtual_yres / cfb->xres;

	cfb->fd = fd;
	cfb->graphics_mode_set = graphics_mode_set;

	if (graphics_mode && cfb->virtual_yres > cfb->yres)
		// Assume pan display is available when the framebuffer size is larger
		// than necessary for a single screen, and graphics mode was succesfully
		// set.
		flags |= DGL_FB_FLAG_HAVE_PAN_DISPLAY;
	cfb->flags = flags;

	if (flags & DGL_FB_FLAG_HAVE_PAN_DISPLAY)
		cfb->PanDisplayFunc = dglConsoleFBPanDisplay;
	else
		cfb->PanDisplayFunc = dglConsoleFBPanDisplayNoOp;
	if (flags & DGL_FB_FLAG_HAVE_WAIT_VSYNC)
		cfb->WaitVSyncFunc = dglConsoleFBWaitVSync;
	else
		cfb->WaitVSyncFunc = dglConsoleFBWaitVSyncNoOp;
	if (flags & DGL_FB_FLAG_HAVE_COPY_AREA)
		cfb->CopyAreaFunc = dglConsoleFBCopyArea;
	else
		cfb->CopyAreaFunc = dglConsoleFBCopyAreaNoOp;

	dglMessage(DGL_MESSAGE_INFO,
		"dglCreateConsoleFramebuffer: Succesfully created console framebuffer\n");
	return cfb;

invalid_format:
	dglMessage(DGL_MESSAGE_WARNING, "dglCreateConsoleFramebuffer: "
		"Console pixel format unsupported\n");
	close(fd);
	return NULL;
}

void dglDestroyConsoleFramebuffer(dglConsoleFB *cfb) {
	if (cfb->graphics_mode_set) {
		int kd_fd = open("/dev/tty0", O_RDWR);
		if (ioctl(kd_fd, KDSETMODE, KD_TEXT) < 0)
			dglMessage(DGL_MESSAGE_WARNING, "dglDestroyConsoleFramebuffer: "
				"Error setting text mode\n");
		close(kd_fd);
	}
	close(cfb->fd);
	delete cfb;
}

// Extra or accelerated functions.

void dglConsoleFBPanDisplay(dglScreenFB *fb, int x, int y) {
	dglConsoleFB *cfb = (dglConsoleFB *)fb;
	struct fb_var_screeninfo fb_var;
	ioctl(cfb->fd, FBIOGET_VSCREENINFO, &fb_var);
	if (x + cfb->xres >= cfb->virtual_xres)
		x = cfb->virtual_xres - cfb->xres;
	if (y + cfb->yres >= cfb->virtual_yres)
		y = cfb->virtual_yres - cfb->yres;
	fb_var.xoffset = x;
	fb_var.yoffset = y;
	ioctl(cfb->fd, FBIOPUT_VSCREENINFO, &fb_var);
}

void dglConsoleFBWaitVSync(dglScreenFB *fb) {
	dglConsoleFB *cfb = (dglConsoleFB *)fb;
	if (ioctl(cfb->fd, FBIO_WAITFORVSYNC, NULL))
		dglMessage(DGL_MESSAGE_WARNING, "FBIO_WAITFORVSYNC failed.\n");
}

static const char *enabled_str[2] = {
	"disabled",
	"enabled"
};

const char *dglGetInfoString(dglConsoleFB *cfb) {
	int pan_display_enabled = (cfb->flags & DGL_FB_FLAG_HAVE_PAN_DISPLAY) != 0;
	int wait_vsync_enabled = (cfb->flags & DGL_FB_FLAG_HAVE_WAIT_VSYNC) != 0;
	int copy_area_enabled = (cfb->flags & DGL_FB_FLAG_HAVE_COPY_AREA) != 0;
	char *info_str = new char[1024];
        sprintf(info_str,
		"Resolution %dx%d, %d bytes per pixel, screen framebuffer size %d, "
                "total framebuffer size %d, stride %d, virtual resolution %dx%d, "
                "framebuffer address %p, PanDisplay %s, WaitVSync %s, CopyArea %s\n",
                cfb->xres, cfb->yres, cfb->bytes_per_pixel, cfb->stride * cfb->yres,
                cfb->total_size, cfb->stride, cfb->xres, cfb->virtual_yres, cfb->framebuffer_addr,
		enabled_str[pan_display_enabled],
		enabled_str[wait_vsync_enabled],
		enabled_str[copy_area_enabled]);
	return info_str;
}

void dglConsoleFBCopyArea(dglScreenFB *fb, int sx, int sy, int dx, int dy, int w, int h) {
	dglConsoleFB *cfb = (dglConsoleFB *)fb;
	struct fb_copyarea copyarea;
	copyarea.sx = sx;
	copyarea.sy = sy;
	copyarea.dx = dx;
	copyarea.dy = dy;
	copyarea.width = w;
	copyarea.height = h;

	if (ioctl(cfb->fd, FBIOCOPYAREA, &copyarea)) {
		dglMessage(DGL_MESSAGE_WARNING,
			"FBIOCOPYAREA ioctl failed (%d, %d, %d, %d, %d, %d).\n",
			sx, sy, dx, dy, w, h);
	}
}
