#define _GNU_SOURCE
#include "../compat.h"

#pragma GCC diagnostic error "-Wunknown-pragmas"
#pragma GCC diagnostic error "-Wincompatible-pointer-types"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <wayland-egl.h>
#include "wayland/xdg-shell.h"
#include "wayland/xdg-decoration-unstable-v1.h"
#include "wayland/idle-inhibit-unstable-v1.h"

#include <EGL/egl.h>

/*#if defined(__unix__)
typedef int native_buffer_handle_t;
#include <sys/mman.h>
#include <unistd.h>
#else
#error Unsupported platform
#endif*/

#include <EGL/eglext.h>
#include <GL/gl.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"

#define GFX_API_NAME "SM64 PC Version (Wayland - OpenGL)"
#define GFX_API_NAME_GL_VER "SM64 PC Version (Wayland - OpenGL %u.%u)"
#define WL_APP_ID "SM64-pc-wayland"

static struct gfx_wayland_context {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_output *output;
	struct xdg_wm_base *wm_base;
	struct zxdg_decoration_manager_v1 *decoration_manager;
	struct zwp_idle_inhibit_manager_v1 *idle_inhibit_manager;
	#if 0
	struct wl_shm *shm;
	struct wl_shm_pool *shm_pool;

	native_buffer_handle_t shm_pool_fd;
	size_t shm_pool_size;
	enum wl_shm_format format;
	bool format_picked;
	#endif

	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	struct zxdg_toplevel_decoration_v1 *xdg_toplevel_decoration;
	struct zwp_idle_inhibitor_v1 *surface_idle_inhibitor;
	bool surface_configured;
	#if 0
	bool buffer_in_use;
	struct wl_buffer *buffer;
	#endif
	int32_t buffer_width, buffer_height;

	struct wl_egl_window *egl_window;
	EGLDisplay egl_display;
	EGLSurface egl_surface;
	EGLContext egl_context;

	void (*on_fullscreen_changed)(bool is_now_fullscreen);
	bool is_fullscreen;

	struct wl_event_queue *frame_queue;
} wl_context;

static void gfx_wl_handle_global_added(
	void *data,
	struct wl_registry *wl_registry,
	uint32_t name,
	const char *interface,
	uint32_t version
) {
	printf("[wl] Got new global object: [%04X v%u] %s\n", name, version, interface);
	if (!strcmp(interface, wl_compositor_interface.name)) {
		wl_context.compositor = wl_registry_bind(wl_context.registry, name, &wl_compositor_interface, version);
	} else if (!strcmp(interface, xdg_wm_base_interface.name)) {
		wl_context.wm_base = wl_registry_bind(wl_context.registry, name, &xdg_wm_base_interface, version);
	} else if (!strcmp(interface, wl_output_interface.name) && wl_context.output == NULL) {
		wl_context.output = wl_registry_bind(wl_context.registry, name, &wl_output_interface, version);
	} else if (!strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name)) {
		wl_context.idle_inhibit_manager = wl_registry_bind(wl_context.registry, name, &zwp_idle_inhibit_manager_v1_interface, version);
	} else if (!strcmp(interface, zxdg_decoration_manager_v1_interface.name)) {
		wl_context.decoration_manager = wl_registry_bind(wl_context.registry, name, &zxdg_decoration_manager_v1_interface, version);
	#if 0
	} else if (!strcmp(interface, "wl_shm")) {
		wl_context.shm = wl_registry_bind(wl_context.registry, name, &wl_shm_interface, version);
	#endif
	}
}

static void gfx_wl_handle_global_removed(void *data, struct wl_registry *wl_registry, uint32_t name) {
	// TODO
}

static void gfx_sync_done(void *finished, struct wl_callback *_cb, uint32_t _cb_data) {
	atomic_store((atomic_bool *)finished, true);
}

static inline void gfx_wl_roundtrip(void) {
	wl_display_roundtrip(wl_context.display);
}

static void gfx_wl_sync(void) {
	struct wl_callback *cb = wl_display_sync(wl_context.display);
	struct wl_callback_listener listener = {
		.done = gfx_sync_done,
	};
	atomic_bool finished = false;
	wl_callback_add_listener(cb, &listener, &finished);
	while (!finished) {
		wl_display_roundtrip(wl_context.display);
	}
}

static struct wl_registry_listener const wl_global_listener = {
	.global = gfx_wl_handle_global_added,
	.global_remove = gfx_wl_handle_global_removed,
};

static void gfx_wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
	xdg_wm_base_pong(wm, serial);
	puts("[wl] Compositor ping");
}

static struct xdg_wm_base_listener const gfx_wm_listener = {
	.ping = gfx_wm_ping,
};

static void gfx_wl_handle_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
	if (wl_context.egl_window != NULL) {
		wl_egl_window_resize(wl_context.egl_window, wl_context.buffer_width, wl_context.buffer_height, 0, 0);
		int set_w, set_h;
		wl_egl_window_get_attached_size(wl_context.egl_window, &set_w, &set_h);
		printf("Window was created with dimensions %dx%d\n", set_w, set_h);
	}
	if (!wl_context.surface_configured) {
		wl_context.surface_configured = true;
		puts("[wl] Initial configure");
	}
	xdg_surface_ack_configure(surface, serial);
}

static struct xdg_surface_listener const gfx_surface_listener = {
	.configure = gfx_wl_handle_configure,
};

static void gfx_wl_set_fullscreen(bool enable);

static void gfx_wl_tl_configure(void *data, struct xdg_toplevel *surface, int32_t width, int32_t height, struct wl_array *states) {
	enum xdg_toplevel_state *state;
	bool is_fullscreen = false;
	wl_array_for_each(state, states) {
		if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) {
			is_fullscreen = true;
		}
	}
	printf("[wl] Window configure (%dx%d; fullscreen=%s)\n", width, height, is_fullscreen ? "true" : "false");
	if (wl_context.surface_configured) {
		gfx_wl_set_fullscreen(is_fullscreen);
		wl_context.buffer_width = width;
		wl_context.buffer_height = height;

		struct wl_region *region = wl_compositor_create_region(wl_context.compositor);
		wl_region_add(region, 0, 0, wl_context.buffer_width, wl_context.buffer_height);
		wl_surface_set_opaque_region(wl_context.surface, region);
		wl_region_destroy(region);
		//xdg_surface_set_window_geometry(wl_context.xdg_surface, 0, 0, width, height);
	}
}

static void gfx_wl_tl_close(void *data, struct xdg_toplevel *surface) {
	exit(0);
}

static void gfx_wl_tl_configure_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height) {
	printf("[wl] Window configure bounds (%dx%d)\n", width, height);
}

static void gfx_wl_tl_wm_caps(void *data, struct xdg_toplevel *toplevel, struct wl_array *capabilities) {
	puts("[wl] Window manager capabilities");
}

static struct xdg_toplevel_listener const gfx_toplevel_listener = {
	.configure = gfx_wl_tl_configure,
	.close = gfx_wl_tl_close,
	.configure_bounds = gfx_wl_tl_configure_bounds,
	.wm_capabilities = gfx_wl_tl_wm_caps,
};

#if 0
static void gfx_wl_shm_available_format(void *data, struct wl_shm *shm, uint32_t format) {
	if (wl_context.format_picked)
		return;

	switch (format) {
		case WL_SHM_FORMAT_ARGB8888:
		case WL_SHM_FORMAT_XRGB8888:
			wl_context.format = format;
			wl_context.format_picked = true;
		default: ;
	}
}

static struct wl_shm_listener const gfx_shm_listener = {
	.format = gfx_wl_shm_available_format,
};
#endif

static void gfx_wl_finish(void);

static void gfx_wl_fullscreen_enter(void);
static void gfx_wl_fullscreen_exit(void);

#if 0
static void gfx_wl_init_shm_pool(void) {
	#if defined(__linux__)
	// 16 MiB
	const size_t init_pool_size = (size_t)1 << 24;
	wl_context.shm_pool_fd = memfd_create("wayland main shm_pool", MFD_CLOEXEC);
	if (wl_context.shm_pool_fd < 0) {
		perror("Failed to create shared memory backing storage");
		exit(1);
	}

	wl_context.shm_pool_size = init_pool_size;
	if (ftruncate(wl_context.shm_pool_fd, init_pool_size)) {
		perror("Failed to allocate memory for shm_pool backing storage");
		exit(1);
	}

	wl_context.shm_pool = wl_shm_create_pool(wl_context.shm, wl_context.shm_pool_fd, init_pool_size);
	if (wl_context.shm_pool == NULL) {
		fputs("Cannot create shared memory pool\n", stderr);
		exit(1);
	}
	#else
	#error Unsupported platform
	#endif
}

#ifndef MIN
#define MIN(A_, B_) ((A_) < (B_) ? (A_) : (B_))
#endif
#ifndef MAX
#define MAX(A_, B_) ((A_) > (B_) ? (A_) : (B_))
#endif

#ifndef ALIGN
// align value to N-byte boundary
#define ALIGN(VAL_, ALIGNMENT_) (((VAL_) + ((ALIGNMENT_) - 1)) & ~((ALIGNMENT_) - 1))
#endif

static void gfx_wl_resize_shm_pool(size_t required_size) {
	// Cap to the smaller of `pointer size` / 4 and 2**36 (64 GiB)
	const size_t max_pool_size = (size_t)1 << MIN(SIZE_WIDTH - 2, 0x24);
	#if defined(__unix__)
	// Align requested size to 32 KiB
	required_size = ALIGN(required_size, (size_t)1 << 15);
	if (required_size >= max_pool_size) {
		fputs("Requesting too much buffer memory\n", stderr);
		exit(-2);
	}
	if (required_size <= wl_context.shm_pool_size) {
		// Shouldn't happen, but let it pass, for now.
		return;
	}
	if (ftruncate(wl_context.shm_pool_fd, required_size) < 0) {
		perror("Failed to resize memory for shm_pool backing storage");
		exit(1);
	}
	#endif

	// Wayland doesn't handle sizes bigger than int limit
	if (required_size >= (size_t)1 << 31) {
		fputs("Buffer too large to handle by Wayland", stderr);
		exit(1);
	}
	wl_shm_pool_resize(wl_context.shm_pool, required_size);
}

static struct wl_buffer *gfx_wl_create_buffer(int32_t width, int32_t height) {
	while (!wl_context.format_picked) {
		gfx_wl_sync();
	}
	uint32_t format = wl_context.format;
	size_t stride;
	switch (format) {
		case WL_SHM_FORMAT_ARGB8888:
		case WL_SHM_FORMAT_XRGB8888:
			stride = 4 * (size_t)width;
			break;

		default:
			fputs("[BUG] Invalid format\n", stderr);
			exit(-1);
	}
	size_t buffer_size = stride * (size_t)height;
	if (buffer_size > wl_context.shm_pool_size) {
		gfx_wl_resize_shm_pool(buffer_size);
	}
	return wl_shm_pool_create_buffer(wl_context.shm_pool, 0, width, height, stride, format);
}

static void gfx_wl_buffer_released(void *data, struct wl_buffer *buffer) {
	//wl_context.buffer_in_use = false;
}

static struct wl_buffer_listener gfx_buffer_listener = {
	.release = gfx_wl_buffer_released,
};
#endif

static void gfx_wl_init(const char *game_name, bool start_in_fullscreen) {
	wl_context.display = wl_display_connect(NULL);
	if (wl_context.display == NULL) {
		fputs("Cannot connect to Wayland server\n", stderr);
		exit(1);
	}
	atexit(gfx_wl_finish);
	wl_context.registry = wl_display_get_registry(wl_context.display);
	wl_registry_add_listener(wl_context.registry, &wl_global_listener, NULL);
	//wl_context.kb_state = wl_kb_state_init();
	size_t count = 0;
	while (wl_context.compositor == NULL || wl_context.wm_base == NULL || wl_context.output == NULL) {
		gfx_wl_roundtrip();
		count += 1;
	}
	//printf("[DBG] Waited %zu cycles until necessary global objects are available\n", count);

	wl_context.frame_queue = wl_display_create_queue(wl_context.display);

	#if 0
	wl_shm_add_listener(wl_context.shm, &gfx_shm_listener, NULL);
	gfx_wl_init_shm_pool();
	#endif

	xdg_wm_base_add_listener(wl_context.wm_base, &gfx_wm_listener, NULL);

	wl_context.surface = wl_compositor_create_surface(wl_context.compositor);
	if (wl_context.surface == NULL) {
		fputs("Cannot create a Wayland surface\n", stderr);
		exit(1);
	}

	wl_proxy_set_queue((struct wl_proxy *)wl_context.surface, wl_context.frame_queue);

	wl_context.xdg_surface = xdg_wm_base_get_xdg_surface(wl_context.wm_base, wl_context.surface);
	if (wl_context.xdg_surface == NULL) {
		fputs("Cannot create XDG surface\n", stderr);
		exit(1);
	}

	wl_context.xdg_toplevel = xdg_surface_get_toplevel(wl_context.xdg_surface);
	if (wl_context.xdg_toplevel == NULL) {
		fputs("Cannot get XDG toplevel surface\n", stderr);
		exit(1);
	}

	xdg_surface_add_listener(wl_context.xdg_surface, &gfx_surface_listener, NULL);

	xdg_toplevel_set_app_id(wl_context.xdg_toplevel, WL_APP_ID);
	xdg_toplevel_set_title(wl_context.xdg_toplevel, GFX_API_NAME);
	xdg_toplevel_add_listener(wl_context.xdg_toplevel, &gfx_toplevel_listener, NULL);

	if (wl_context.decoration_manager != NULL) {
		wl_context.xdg_toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(wl_context.decoration_manager, wl_context.xdg_toplevel);
		zxdg_toplevel_decoration_v1_set_mode(wl_context.xdg_toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}

	if (wl_context.idle_inhibit_manager != NULL) {
		wl_context.surface_idle_inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(wl_context.idle_inhibit_manager, wl_context.surface);
	}

	wl_context.buffer_width = DESIRED_SCREEN_WIDTH;
	wl_context.buffer_height = DESIRED_SCREEN_HEIGHT;
	#if 0
	wl_context.buffer = gfx_wl_create_buffer(wl_context.buffer_width, wl_context.buffer_height);
	if (wl_context.buffer == NULL) {
		fputs("Cannot create surface buffer\n", stderr);
		exit(1);
	}

	wl_buffer_add_listener(wl_context.buffer, &gfx_buffer_listener, NULL);

	wl_surface_attach(wl_context.surface, wl_context.buffer, 0, 0);
	#endif

	//xdg_surface_set_window_geometry(wl_context.xdg_surface, 0, 0, wl_context.buffer_width, wl_context.buffer_height);

	{
		struct wl_region *region = wl_compositor_create_region(wl_context.compositor);
		wl_region_add(region, 0, 0, wl_context.buffer_width, wl_context.buffer_height);
		wl_surface_set_opaque_region(wl_context.surface, region);
		wl_region_destroy(region);
	}

	wl_context.egl_window = wl_egl_window_create(wl_context.surface, DESIRED_SCREEN_WIDTH, DESIRED_SCREEN_HEIGHT);
	if (wl_context.egl_window == NULL) {
		fputs("Cannot create an EGL window\n", stderr);
		exit(1);
	}

	wl_context.egl_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, wl_context.display, NULL);
	if (!wl_context.egl_display) {
		fputs("Cannot open EGL display\n", stderr);
		exit(1);
	}
	if (!eglInitialize(wl_context.egl_display, NULL, NULL)) {
		fputs("Cannot initialize EGL\n", stderr);
		exit(1);
	}
	EGLConfig configs[1];
	EGLint num_configs = 0;
	{
		static EGLint const attribs[] = {
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_CONFIG_CAVEAT, EGL_NONE,
			EGL_DEPTH_SIZE, 24,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_NONE,
		};
		eglBindAPI(EGL_OPENGL_API);
		if (!eglChooseConfig(wl_context.egl_display, attribs, configs, sizeof configs / sizeof *configs, &num_configs)) {
			fputs("Cannot choose EGL config\n", stderr);
			exit(1);
		}

		printf("Configs available: %d\n", num_configs);
	}

	EGLConfig config = configs[0];

	{
		EGLint min_int = -1;
		EGLint max_int = -1;
		eglGetConfigAttrib(wl_context.egl_display, config, EGL_MIN_SWAP_INTERVAL, &min_int);
		eglGetConfigAttrib(wl_context.egl_display, config, EGL_MAX_SWAP_INTERVAL, &max_int);
		printf("Supported swap intervals: %d..%d\n", (int)min_int, (int)max_int);
	}

	{
		static EGLAttrib const surface_attrs[] = {
			EGL_GL_COLORSPACE, EGL_GL_COLORSPACE_SRGB,
			EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
			EGL_NONE,
		};

		wl_context.egl_surface = eglCreatePlatformWindowSurface(wl_context.egl_display, config, wl_context.egl_window, surface_attrs);
		if (wl_context.egl_surface == EGL_NO_SURFACE) {
			fputs("Cannot create EGL surface\n", stderr);
			exit(1);
		}
	}

	{
		EGLint context_attrs[] = {
			EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
			EGL_CONTEXT_MINOR_VERSION_KHR, 1,
			EGL_NONE,
		};

		wl_context.egl_context = eglCreateContext(wl_context.egl_display, config, EGL_NO_CONTEXT, context_attrs);
		if (wl_context.egl_context == EGL_NO_CONTEXT) {
			fputs("Cannot create EGL context\n", stderr);
			exit(1);
		}

		eglMakeCurrent(wl_context.egl_display, wl_context.egl_surface, wl_context.egl_surface, wl_context.egl_context);
	}

	{
		GLint major = 0, minor = 0;

		glGetIntegerv(GL_MAJOR_VERSION, &major);
		glGetIntegerv(GL_MINOR_VERSION, &minor);

		unsigned vmaj = major, vmin = minor;

		printf("OpenGL version: %u.%u\n", vmaj, vmin);

		char title[sizeof GFX_API_NAME + 8];
		title[0] = 0;
		snprintf(title, sizeof title, GFX_API_NAME_GL_VER, vmaj, vmin);
		if (title[0]) {
			xdg_toplevel_set_title(wl_context.xdg_toplevel, title);
		}
	}

	// Disable swap as we'll control vsyncs ourselves.
	eglSwapInterval(wl_context.egl_display, 0);

	if (start_in_fullscreen) {
		gfx_wl_fullscreen_enter();
	}

	wl_surface_commit(wl_context.surface);
	while (!wl_context.surface_configured) {
		gfx_wl_roundtrip();
	}

	{
		int set_w, set_h;
		wl_egl_window_get_attached_size(wl_context.egl_window, &set_w, &set_h);
		printf("Window was created with dimensions %dx%d\n", set_w, set_h);
	}
}

static void gfx_wl_finish(void) {
	if (wl_context.display) {
		if (wl_context.egl_display != EGL_NO_DISPLAY) {
			if (wl_context.egl_context != EGL_NO_CONTEXT) {
				eglDestroyContext(wl_context.egl_display, wl_context.egl_context);
			}

			if (wl_context.egl_surface != EGL_NO_SURFACE) {
				eglDestroySurface(wl_context.egl_display, wl_context.egl_surface);
			}

			eglTerminate(wl_context.egl_display);
		}

		if (wl_context.egl_window != NULL) {
			wl_egl_window_destroy(wl_context.egl_window);
		}

		if (wl_context.surface != NULL) {
			#if 0
			if (wl_context.buffer != NULL) {
				wl_buffer_destroy(wl_context.buffer);
			}
			#endif
			if (wl_context.surface_idle_inhibitor != NULL) {
				zwp_idle_inhibitor_v1_destroy(wl_context.surface_idle_inhibitor);
			}
			if (wl_context.xdg_surface != NULL) {
				if (wl_context.xdg_toplevel != NULL) {
					if (wl_context.xdg_toplevel_decoration != NULL) {
						zxdg_toplevel_decoration_v1_destroy(wl_context.xdg_toplevel_decoration);
					}
					xdg_toplevel_destroy(wl_context.xdg_toplevel);
				}
				xdg_surface_destroy(wl_context.xdg_surface);
				wl_context.surface_configured = false;
			}
			wl_surface_destroy(wl_context.surface);
		}

		if (wl_context.compositor != NULL) {
			wl_compositor_destroy(wl_context.compositor);
			wl_context.compositor = NULL;
		}

		if (wl_context.wm_base != NULL) {
			xdg_wm_base_destroy(wl_context.wm_base);
			wl_context.wm_base = NULL;
		}

		if (wl_context.idle_inhibit_manager != NULL) {
			zwp_idle_inhibit_manager_v1_destroy(wl_context.idle_inhibit_manager);
			wl_context.idle_inhibit_manager = NULL;
		}

		if (wl_context.decoration_manager != NULL) {
			zxdg_decoration_manager_v1_destroy(wl_context.decoration_manager);
			wl_context.decoration_manager = NULL;
		}

		if (wl_context.output != NULL) {
			wl_output_destroy(wl_context.output);
			wl_context.output = NULL;
		}

		#if 0
		if (wl_context.shm != NULL) {
			if (wl_context.shm_pool != NULL) {
				wl_shm_pool_destroy(wl_context.shm_pool);
			}
			wl_shm_destroy(wl_context.shm);
			wl_context.shm = NULL;
		}
		#endif

		if (wl_context.registry != NULL) {
			wl_registry_destroy(wl_context.registry);
		}

		if (wl_context.frame_queue != NULL) {
			wl_event_queue_destroy(wl_context.frame_queue);
		}

		wl_display_disconnect(wl_context.display);
		wl_context.display = NULL;
	}
}

static void gfx_wl_set_keyboard_callbacks(bool (*on_key_down)(int scancode), bool (*on_key_up)(int scancode), void (*on_all_keys_up)(void)) {
}

static void gfx_wl_set_fullscreen_changed_callback(void (*on_fullscreen_changed)(bool is_now_fullscreen)) {
	wl_context.on_fullscreen_changed = on_fullscreen_changed;
}

static void gfx_wl_set_fullscreen(bool enable) {
	if (wl_context.is_fullscreen == enable)
		return;

	wl_context.is_fullscreen = enable;
	if (enable)
		gfx_wl_fullscreen_enter();
	else
		gfx_wl_fullscreen_exit();

	if (wl_context.on_fullscreen_changed)
		wl_context.on_fullscreen_changed(enable);
}

static void gfx_wl_fullscreen_enter(void) {
	xdg_toplevel_set_fullscreen(wl_context.xdg_toplevel, NULL);
}

static void gfx_wl_fullscreen_exit(void) {
	xdg_toplevel_unset_fullscreen(wl_context.xdg_toplevel);
}

static void gfx_wl_main_loop(void (*run_one_game_iter)(void)) {
	while (1) {
		run_one_game_iter();
	}
}

static void gfx_wl_get_dimensions(uint32_t *width, uint32_t *height) {
	//int w = 0, h = 0;
	//wl_egl_window_get_attached_size(wl_context.egl_window, &w, &h);
	*width = wl_context.buffer_width;
	*height = wl_context.buffer_height;
}

static void gfx_wl_handle_events(void) {
	// TODO
	gfx_wl_roundtrip();
}

static bool gfx_wl_start_frame(void) {
	//wl_surface_damage(wl_context.surface, 0, 0, wl_context.buffer_width, wl_context.buffer_height);
	//bool res = eglMakeCurrent(wl_context.egl_display, wl_context.egl_surface, wl_context.egl_surface, wl_context.egl_context);
	//printf("Start frame: %s\n", res ? "true" : "false");
	return true;
}

static void print_bool(char const *str, bool val) {
	printf("%s: %s\n", str, val ? "true" : "false");
}

static void gfx_wl_frame_done(void* _user, struct wl_callback* cb, uint32_t time) {
	atomic_store((atomic_bool*)_user, true);
	//puts("Callback");
	wl_callback_destroy(cb);
}

// Throttle frame rate by waiting a desired number of vsyncs.
// We can't rely on eglSwapInterval, as it's commonly restricted to just 1 (e.g. in Mesa),
// so we have to commit & wait ourselves.
static void gfx_wl_wait_vsyncs(uint32_t vsyncs) {
	static struct wl_callback_listener const frame_listener = {
		.done = gfx_wl_frame_done,
	};
	while (vsyncs) {
		struct wl_callback *callback = wl_surface_frame(wl_context.surface);
		atomic_bool done = false;
		wl_callback_add_listener(callback, &frame_listener, &done);
		wl_surface_commit(wl_context.surface);
		while (!done) {
			wl_display_dispatch_queue(wl_context.display, wl_context.frame_queue);
		}

		vsyncs -= 1;
		//puts("VSync");
	}
}

static void gfx_wl_swap_buffers_begin(void) {
	eglSwapBuffers(wl_context.egl_display, wl_context.egl_surface);
}

static void gfx_wl_swap_buffers_end(void) {
	// TODO
	gfx_wl_wait_vsyncs(2);
}

struct GfxWindowManagerAPI gfx_wayland = {
	gfx_wl_init,
	gfx_wl_set_keyboard_callbacks,
	gfx_wl_set_fullscreen_changed_callback,
	gfx_wl_set_fullscreen,
	gfx_wl_main_loop,
	gfx_wl_get_dimensions,
	gfx_wl_handle_events,
	gfx_wl_start_frame,
	gfx_wl_swap_buffers_begin,
	gfx_wl_swap_buffers_end,
};
