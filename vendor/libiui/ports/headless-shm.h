/*
 * ports/headless-shm.h - Shared Memory Interface for External Tool Control
 *
 * This header defines the shared memory layout for IPC between the headless
 * backend and external tools (Python scripts, shell commands, etc.).
 *
 * Usage (C - libiui side):
 *   #include "ports/headless.h"
 *   #include "ports/headless-shm.h"
 *
 *   iui_port_ctx *ctx = g_iui_port.init(800, 600, "Test");
 *   iui_headless_enable_shm(ctx, "/libiui_shm");
 *
 *   // Run main loop - external tools can now interact via SHM
 *   while (g_iui_port.poll_events(ctx)) { ... }
 *
 *   iui_headless_disable_shm(ctx);
 *
 * Usage (Python - external tool):
 *   import mmap
 *   import struct
 *
 *   # Open shared memory
 *   fd = os.open("/dev/shm/libiui_shm", os.O_RDWR)
 *   shm = mmap.mmap(fd, 0)
 *
 *   # Read header
 *   magic, version, width, height = struct.unpack("<IIII", shm[:16])
 *   assert magic == 0x49554953  # "SIUI"
 *
 *   # Inject mouse click
 *   event = struct.pack("<IffII", 1, 100.0, 200.0, 1, 0)  # MOUSE_CLICK
 *   # Write to event ring buffer...
 *
 * Memory Layout:
 *   [Header: 256 bytes]
 *   [Event Ring Buffer: 64 * sizeof(event) bytes]
 *   [Framebuffer: width * height * 4 bytes (ARGB32)]
 */

#ifndef IUI_HEADLESS_SHM_H
#define IUI_HEADLESS_SHM_H

#include <stdbool.h>
#include <stdint.h>

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared memory magic number: "SIUI" (little-endian) */
#define IUI_SHM_MAGIC 0x49554953

/* Protocol version */
#define IUI_SHM_VERSION 1

/* Event ring buffer size */
#define IUI_SHM_EVENT_RING_SIZE 64

/* Header size (padded for alignment) */
#define IUI_SHM_HEADER_SIZE 256

/* Event Types for IPC */
typedef enum {
    IUI_SHM_EVENT_NONE = 0,
    IUI_SHM_EVENT_MOUSE_MOVE = 1,
    IUI_SHM_EVENT_MOUSE_CLICK = 2,
    IUI_SHM_EVENT_MOUSE_DOWN = 3,
    IUI_SHM_EVENT_MOUSE_UP = 4,
    IUI_SHM_EVENT_KEY_PRESS = 5,
    IUI_SHM_EVENT_TEXT_INPUT = 6,
    IUI_SHM_EVENT_SCROLL = 7,
} iui_shm_event_type;

/* Command Types for synchronous operations */
typedef enum {
    IUI_SHM_CMD_NONE = 0,
    IUI_SHM_CMD_SCREENSHOT = 1,  /* Save screenshot to path */
    IUI_SHM_CMD_RESET_STATS = 2, /* Reset statistics counters */
    IUI_SHM_CMD_EXIT = 3,        /* Request graceful exit */
    IUI_SHM_CMD_GET_STATS = 4,   /* Copy stats to response area */
} iui_shm_command_type;

/* Response Status */
typedef enum {
    IUI_SHM_STATUS_PENDING = 0,
    IUI_SHM_STATUS_OK = 1,
    IUI_SHM_STATUS_ERROR = 2,
} iui_shm_status;

/* Event structure for ring buffer */
typedef struct {
    uint32_t type; /* iui_shm_event_type */
    float x, y;    /* Mouse position or scroll delta */
    uint32_t key;  /* Key code or button mask */
    uint32_t text; /* Unicode codepoint for text input */
} iui_shm_event_t;

/* Shared Memory Header
 *
 * This structure is placed at the beginning of the shared memory region.
 * External tools can read/write this to interact with the headless backend.
 *
 * Memory layout after header:
 *   offset 256: Event ring buffer [64 * 20 bytes = 1280 bytes]
 *   offset 1536: Framebuffer [width * height * 4 bytes]
 */
typedef struct {
    /* Identification (offset 0) */
    uint32_t magic;   /* IUI_SHM_MAGIC */
    uint32_t version; /* IUI_SHM_VERSION */

    /* Framebuffer dimensions (offset 8) */
    int32_t width;
    int32_t height;

    /* State flags (offset 16) */
    uint32_t running;      /* Non-zero if backend is running */
    uint32_t frame_count;  /* Current frame number */
    uint64_t timestamp_ns; /* Last update timestamp (nanoseconds) */

    /* Statistics (offset 32) */
    uint32_t draw_box_calls;
    uint32_t draw_line_calls;
    uint32_t draw_circle_calls;
    uint32_t draw_arc_calls;
    uint32_t set_clip_calls;
    uint32_t path_stroke_calls;
    uint64_t total_pixels_drawn;

    /* Event ring buffer indices (offset 64) */
    uint32_t event_write_idx; /* Written by external tool */
    uint32_t event_read_idx;  /* Read by headless backend */

    /* Command/response interface (offset 72) */
    uint32_t command;         /* iui_shm_command_type */
    uint32_t command_seq;     /* Incremented by external tool */
    uint32_t response_seq;    /* Set to command_seq when processed */
    uint32_t response_status; /* iui_shm_status */

    /* Command parameters (offset 88) */
    char command_path[128]; /* Path for screenshot command */

    /* Padding to 256 bytes */
    uint8_t _reserved[40];
} iui_shm_header_t;

/* Calculate total shared memory size */
static inline size_t iui_shm_total_size(int width, int height)
{
    return IUI_SHM_HEADER_SIZE +
           (IUI_SHM_EVENT_RING_SIZE * sizeof(iui_shm_event_t)) +
           ((size_t) width * (size_t) height * 4);
}

/* Get pointer to event ring buffer */
static inline iui_shm_event_t *iui_shm_get_events(void *shm_base)
{
    return (iui_shm_event_t *) ((uint8_t *) shm_base + IUI_SHM_HEADER_SIZE);
}

/* Get pointer to framebuffer */
static inline uint32_t *iui_shm_get_framebuffer(void *shm_base)
{
    return (uint32_t *) ((uint8_t *) shm_base + IUI_SHM_HEADER_SIZE +
                         (IUI_SHM_EVENT_RING_SIZE * sizeof(iui_shm_event_t)));
}

/* Shared Memory API */

/* Enable shared memory mode with specified name (e.g., "/libiui_shm")
 * Returns true on success.
 * The name should start with '/' for POSIX shm_open compatibility.
 */
bool iui_headless_enable_shm(iui_port_ctx *ctx, const char *shm_name);

/* Disable shared memory mode and unmap/unlink the shared memory */
void iui_headless_disable_shm(iui_port_ctx *ctx);

/* Check if shared memory mode is enabled */
bool iui_headless_shm_enabled(iui_port_ctx *ctx);

/* Get the shared memory header (for internal use or testing) */
iui_shm_header_t *iui_headless_get_shm_header(iui_port_ctx *ctx);

/* Process pending events from the shared memory ring buffer
 * Called automatically during poll_events, but can be called manually.
 */
void iui_headless_process_shm_events(iui_port_ctx *ctx);

/* Process pending commands from external tools
 * Called automatically during poll_events.
 */
void iui_headless_process_shm_commands(iui_port_ctx *ctx);

/* Update shared memory statistics (called during end_frame) */
void iui_headless_update_shm_stats(iui_port_ctx *ctx);

/* Copy framebuffer to shared memory (called during end_frame) */
void iui_headless_sync_shm_framebuffer(iui_port_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* IUI_HEADLESS_SHM_H */
