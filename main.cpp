#include <cstdio>
#include <cerrno>
#include <ctime>
#include <cstring>

extern "C" {
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <wayland-client.h>
#include "wl/xdg-shell-client-protocol.h"
}

static void randname(char *buf) noexcept {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}

static int create_shm_file() noexcept {
	int retries = 100;

	do {
		char name[] = "/wl_shm-XXXXXX";
		randname(name + sizeof(name) - 7);
		--retries;
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);
	
	return -1;
}

static int allocate_shm_file(size_t size) noexcept {
	int fd = create_shm_file();
	if (fd < 0) return -1;

	int ret;
	do {
		ret = ftruncate(fd, size);
	} while(ret < 0 && errno == EINTR);

	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

struct client_state {
	wl_display *	wl_display {};
	wl_registry *	wl_registry {};
	wl_shm *		wl_shm {};
	wl_compositor *	wl_compositor {};
	xdg_wm_base *	xdg_wm_base {};
	wl_surface *	wl_surface {};
	xdg_surface *	xdg_surface {};
	xdg_toplevel *	xdg_toplevel {};
};

static void wl_buffer_release(void *data, wl_buffer *buffer) noexcept {
	wl_buffer_destroy(buffer);
}

static const wl_buffer_listener wl_buffer_listener {
	.release = wl_buffer_release,
};

static wl_buffer *draw_frame(client_state *state) noexcept {
	const int width = 640, height = 480;
	int stride = width * 4;
	int size = stride * height;

	int fd = allocate_shm_file(size);
	if (fd == -1) {
		return NULL;
	}

	uint32_t *data = static_cast<uint32_t *>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
	if (data == MAP_FAILED) {
		close(fd);
		return NULL;
	}

	wl_shm_pool *pool = wl_shm_create_pool(state->wl_shm, fd, size);
	wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

	wl_shm_pool_destroy(pool);
	close(fd);

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			if ((x + y / 8 * 8) % 16 < 8) {
				data[y * width + x ] = 0xFF666666;
			} else {
				data[y * width + x] = 0xFFEEEEEE;
			}
		}
	}

	munmap(data, size);
	wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
	return buffer;

	return nullptr;
}

static void xdg_surface_configure(void *data, xdg_surface *xdg_surface, uint32_t serial) {
	client_state *state = static_cast<client_state *>(data);
	xdg_surface_ack_configure(xdg_surface, serial);

	wl_buffer *buffer = draw_frame(state);
	wl_surface_attach(state->wl_surface, buffer, 0, 0);
	wl_surface_commit(state->wl_surface);
}

static const xdg_surface_listener xdg_surface_listener {
	.configure = xdg_surface_configure,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const xdg_wm_base_listener xdg_wm_base_listener {
	.ping = xdg_wm_base_ping,
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	client_state *state = static_cast<client_state *>(data);

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->wl_shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->wl_compositor = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, 4));
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = static_cast<xdg_wm_base *>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
		xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
	}

	/* printf("interface: '%s', version: %d, name %d\n", interface, version, name); */
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = { .global = registry_handle_global, .global_remove = registry_handle_global_remove };

int main(int argc, char *argv[]) {
	client_state state {};

	state.wl_display = wl_display_connect(NULL);
	if (!state.wl_display) {
		fprintf(stderr, "Failed to connect to Wayland display.\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Connection established!\n");

	state.wl_registry = wl_display_get_registry(state.wl_display);

	puts("test\n");
	wl_registry_add_listener(state.wl_registry, &registry_listener, &state);
	puts("test\n");
	wl_display_roundtrip(state.wl_display);
	puts("test\n");

	state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
	state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
	xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
	state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
	xdg_toplevel_set_title(state.xdg_toplevel, "Example client");
	wl_surface_commit(state.wl_surface);

	while (wl_display_dispatch(state.wl_display)) {

	}



	


	wl_display_disconnect(state.wl_display);
	return EXIT_SUCCESS;
}
