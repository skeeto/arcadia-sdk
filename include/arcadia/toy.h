/*
 * arcadia/toy.h — Arcadia Toy SDK, public C API.
 *
 * Write a toy (a mini-game plugin for Synthetic Reality's Arcadia) as a plain
 * C DLL. Include this header, implement ArcadiaToyRegister(), fill in the
 * callbacks you care about, and link against the SDK runtime. The runtime
 * provides the six exports Arcadia's loader expects (MPRegisterCallback,
 * MPOpenOffer, MPCloseOffer, MPNegotiationRequired, MPIncomingPacket,
 * MPSendGEDMessage) and forwards them to your callbacks.
 *
 * The binary contract is documented in sdk/docs/ABI.md. Where the ABI is only
 * partially understood, the corresponding wrapper is marked "unverified" and
 * you can always fall back to ar_host() to talk to the host directly.
 *
 * Everything here is 32-bit x86 and __cdecl. Build with i686-w64-mingw32 or
 * MSVC /MACHINE:X86. No MFC and no host import library are required.
 */
#ifndef ARCADIA_TOY_H
#define ARCADIA_TOY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 *  Context and player info
 * ------------------------------------------------------------------ */

/* Opaque per-session context handed to your callbacks. */
typedef struct ArContext ArContext;

/* Player record filled by ar_get_player_info(). Layout matches the host's
 * 0x6c-byte structure (dispatch selector 0x1b). Only `name` and `flags` are
 * fully trusted; the field_* members are raw copies from the host player
 * table whose exact meaning is not yet decoded. */
typedef struct ArPlayerInfo {
    int  size;          /* 0x00: set to sizeof(ArPlayerInfo) before the call  */
    int  reserved04;    /* 0x04 */
    char name[28];      /* 0x08: NUL-terminated display name                  */
    int  flags;         /* 0x24: bit0 = is-bot (other bits: team/ready)       */
    int  field_28;      /* 0x28 */
    int  field_2c;      /* 0x2c */
    int  field_30;      /* 0x30 */
    int  field_34;      /* 0x34 */
    int  field_38;      /* 0x38 */
    int  field_3c;      /* 0x3c */
    int  field_40;      /* 0x40 */
    char reserved44[0x6c - 0x44]; /* 0x44..0x6b */
} ArPlayerInfo;         /* sizeof == 0x6c (108) */

/* ------------------------------------------------------------------ *
 *  The toy definition you register
 * ------------------------------------------------------------------ */

/* Fill in the callbacks you need; leave the rest NULL. All are optional
 * except that a drawable toy will usually want at least `paint`. */
typedef struct ArToy {
    const char *name;   /* optional: for logging */

    /* Lifecycle.
     *   open: session start. `offer` is the host's raw session descriptor
     *         (MPOpenOffer's struct arg) — valid ONLY during this call, layout
     *         only partially mapped (see ABI.md §7); most toys can ignore it. */
    int  (*open)(ArContext *ctx, HWND hwnd, void *offer);  /* MPOpenOffer */
    void (*close)(ArContext *ctx);                /* MPCloseOffer: session end  */
    void (*tick)(ArContext *ctx, unsigned now_ms);/* per-frame update (~32 Hz)  */

    /* paint: draw one frame. The SDK calls this after each tick with a
     * flicker-free, double-buffered memory DC sized to the canvas client area,
     * then blits it to the Arcadia window. Just draw with ordinary GDI.
     * (Authors who want to use the host's native DIB surfaces instead can
     * ignore this and drive ar_surface_* directly — see sdk/docs/ABI.md.) */
    void (*paint)(ArContext *ctx, HDC dc);

    /* Networking (host-driven sync).
     *   serialize: called by the host (GED 0x08) to snapshot your outgoing
     *              state for `channel` into `buf` (capacity `cap`); return the
     *              number of bytes written (<=cap), or 0 for nothing to send.
     *   packet:    a payload addressed to this toy arrived (MPIncomingPacket). */
    int  (*serialize)(ArContext *ctx, int channel, void *buf, int cap);
    void (*packet)(ArContext *ctx, int channel, const void *data, int len);

    /* Higher-level host events (best-effort decode; see ABI.md).
     *   chat:         a chat/system line for `channel` (GED 0x0e).
     *   player_event: a player joined / acted (GED 0x0f); `name` may be NULL. */
    void (*chat)(ArContext *ctx, int channel, const char *text);
    void (*player_event)(ArContext *ctx, int player_id, const char *name);

    /* Raw GED escape hatch: receives every host event code before the typed
     * callbacks above. Return nonzero to mark it handled (suppresses the typed
     * callback); return 0 to let default routing proceed. Optional. */
    int  (*ged)(ArContext *ctx, int code, int chan, void *p3, void *p4, int p5);

    /* Capability bitmask returned from MPNegotiationRequired.
     * 0 means "use the SDK default" (AR_NEGOTIATE_DEFAULT). */
    unsigned negotiate;
} ArToy;

/* Implement this exactly once in your toy. The runtime calls it during load;
 * fill *toy with your callbacks. */
void ArcadiaToyRegister(ArToy *toy);

#define AR_NEGOTIATE_DEFAULT 0x08000000u  /* value used by the reference toys */

/* ------------------------------------------------------------------ *
 *  Host services (toy -> host dispatch wrappers)
 *
 *  These are thin wrappers over the single host dispatch function. They work
 *  from anywhere once the session is open. Selector numbers are in ABI.md.
 * ------------------------------------------------------------------ */

/* Raw dispatch. `selector` is a host opcode (9..0x4c); extra args are passed
 * straight through (cdecl). Returns the host's result, or -1 if the host
 * pointer is not yet registered. Use this for any selector without a wrapper. */
int  ar_host(int selector, int a1, int a2, int a3, int a4, int a5, int a6);

/* The Arcadia canvas window for the current session (NULL before open). */
HWND ar_window(void);

/* Force a repaint (calls your paint callback on the next opportunity). Also
 * marks the canvas window invalid so the host refreshes it. */
void ar_invalidate(void);

/* Chat / console.  ar_print writes one line to the Arcadia chat window
 * (posts `text` to the current channel via selector 0x0e). */             /* sel 0x0e [V] */
void ar_print(const char *text);
void ar_printf(const char *fmt, ...);

/* Players */
int  ar_local_player_id(void);                                   /* sel 0x1e  [V] */
int  ar_get_player_info(int player_id, ArPlayerInfo *out);       /* sel 0x1b  [V] */
int  ar_get_player_list(int max, int *out_ids);                  /* sel 0x18  [I] */
int  ar_num_players(void);                                       /* sel 0x0c  [?] */

/* Persistence (per-toy blob store) */
int  ar_store_write(int id, const void *buf, int len);           /* sel 0x24  [I] */
int  ar_store_read(int id, void *buf, int cap);                  /* sel 0x25  [V] */

/* Per-toy registry values (HKCU\...\Arcadia\TOYS\TOY#\<name>) */
int  ar_reg_set(const char *name, const void *val, int type_or_len); /* sel 0x22 [V] */
int  ar_reg_get(const char *name, void *out, int cap, int type);     /* sel 0x23 [V] */

/* Off-screen 8-bit DIB surfaces.
 * create builds a `width`x`height` DIB (flags reserved, pass 0) and returns a
 * handle (or -1). load/save read/write image files; fill memsets the bits. */
int  ar_surface_create(int width, int height, int flags);        /* sel 0x29  [V] */
void ar_surface_destroy(int h);                                  /* sel 0x2a  [V] */
int  ar_surface_load(int h, const char *file);                   /* sel 0x2b  [I] */
void ar_surface_fill(int h, int byte_value);                     /* sel 0x2c  [V] */
int  ar_surface_save(int h, const char *file, int format);       /* sel 0x34  [I] */
void ar_surface_blit_ex(int dst, int src, int a3, int a4, int a5, int a6); /* 0x41 [?] */
int  ar_surface_valid(int h);                                    /* sel 0x33  [V] */
int  ar_surface_pitch(int h);                                    /* sel 0x37  [V] */
void*ar_surface_pixel_addr(int h, int x, int y);                 /* sel 0x32  [V] */
int  ar_surface_get_size(int h, int *w, int *height);            /* sel 0x31  [I] */
int  ar_surface_palette(int h, int index);                       /* sel 0x4b  [V] */
HDC  ar_surface_get_dc(int h);                                   /* sel 0x30  [I] */
void ar_surface_blit(int dst, int src, int x, int y,
                     int w, int height, int a7, int a8);          /* sel 0x2d  [V] */
void ar_surface_blit_to_dc(int h, HDC dc, int x, int y);         /* sel 0x2e  [V] */
void ar_surface_blit_rect(int dst, int src, int p3, int p4);     /* sel 0x2f  [V] */

/* Networking.
 *   ar_send:  broadcast `len` bytes of `data` to peers on `channel`; each peer
 *             receives it via its packet() callback / MPIncomingPacket. This is
 *             the discrete-message path most toys use (selector 0x0f). The host
 *             applies a lossless RLE+escape codec, so arbitrary binary is safe.
 *   ar_flush: ask the host to pull your serialize() snapshot for all channels
 *             and transmit whatever you return (>0). Use after changing state
 *             you sync via serialize() rather than ar_send (selector 0x49). */
void ar_send(int channel, const void *data, int len);            /* sel 0x0f  [V] */
void ar_flush(void);                                             /* sel 0x49  [V] */

/* Audio (selector 0x09). Reference sounds by bare filename; the host resolves
 * them against your toy folder (put .wav in sfx/, .mid in midi/, like the
 * shipped toys). ar_play_sound plays a sound effect, ar_play_music a MIDI
 * tune, ar_stop_sounds silences all effects. */
void ar_play_sound(const char *name);                            /* sel 0x09/1 [I] */
void ar_play_music(const char *name);                            /* sel 0x09/2 [I] */
void ar_stop_sounds(void);                                       /* sel 0x09/4 [I] */

/* The host session descriptor captured at open() (same pointer passed to your
 * open callback). Only meaningful/valid during open(); layout partial. */
void *ar_offer(void);

/* Timing */
void ar_mark_time(void);                                         /* sel 0x4a  [V] */

/* Input polling (the model the shipped toys use). Call from your tick/paint.
 *   ar_key_down: TRUE if virtual-key `vk` is currently down.
 *   ar_mouse:    writes the cursor position in canvas client coords through
 *                x/y (either may be NULL) and returns a bitmask of
 *                AR_MOUSE_L/AR_MOUSE_R/AR_MOUSE_M buttons currently pressed. */
#define AR_MOUSE_L 1
#define AR_MOUSE_R 2
#define AR_MOUSE_M 4
BOOL ar_key_down(int vk);
int  ar_mouse(int *x, int *y);

#ifdef __cplusplus
}
#endif
#endif /* ARCADIA_TOY_H */
