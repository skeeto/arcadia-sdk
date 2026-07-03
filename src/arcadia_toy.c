/*
 * arcadia_toy.c — Arcadia Toy SDK runtime.
 *
 * Implements the six exports Arcadia's loader binds (see sdk/docs/ABI.md) and
 * forwards them to the ArToy callbacks the toy registers via
 * ArcadiaToyRegister(). Also implements the ar_* host-service wrappers.
 *
 * Pure C, 32-bit, __cdecl. Exports are declared undecorated in arcadia_toy.def.
 */
#include "arcadia/toy.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Optional file tracing for bring-up/diagnostics. Define AR_TOY_TRACE to log
 * every export entry (and chosen HWND) to AR_TOY_TRACE_FILE. */
#ifdef AR_TOY_TRACE
#ifndef AR_TOY_TRACE_FILE
#define AR_TOY_TRACE_FILE "arcadia_toy_trace.log"
#endif
static void ar_trace(const char *fmt, ...)
{
    FILE *f = fopen(AR_TOY_TRACE_FILE, "a");
    va_list ap;
    if (!f) return;
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}
#define AR_TRACE(...) ar_trace(__VA_ARGS__)
static unsigned char ar_seen_ged[256];
static int ar_ged_calls;   /* global call counter, to cap trace volume */
#else
#define AR_TRACE(...) ((void)0)
#endif

/* The host dispatch function: int __cdecl f(int selector, ...).
 * We call it through a fixed 8-argument cdecl prototype; the host reads only
 * as many arguments as the selector needs and (being cdecl) we clean the
 * stack, so passing extra zero args is always safe. */
typedef int (__cdecl *ar_dispatch_t)(int, int, int, int, int, int, int, int, int);

struct ArContext { HWND hwnd; };

static ar_dispatch_t g_dispatch = 0;
static struct ArContext g_ctx = { 0 };
static ArToy  g_toy;
static int    g_registered = 0;

/* SDK-managed double buffer for the paint callback. */
static HDC     g_backdc  = 0;
static HBITMAP g_backbmp = 0;
static HBITMAP g_backold = 0;
static int     g_backw = 0, g_backh = 0;

/* Host stack buffer for snapshot serialization is 0x3ec bytes; stay under it. */
#define AR_SNAPSHOT_CAP 1000

static void ensure_registered(void)
{
    if (g_registered) return;
    memset(&g_toy, 0, sizeof g_toy);
    ArcadiaToyRegister(&g_toy);
    g_registered = 1;
}

/* ------------------------------------------------------------------ *
 *  Host dispatch wrappers
 * ------------------------------------------------------------------ */

int ar_host(int selector, int a1, int a2, int a3, int a4, int a5, int a6)
{
    if (!g_dispatch) return -1;
    return g_dispatch(selector, a1, a2, a3, a4, a5, a6, 0, 0);
}

HWND ar_window(void) { return g_ctx.hwnd; }

void ar_invalidate(void)
{
    if (g_ctx.hwnd) InvalidateRect(g_ctx.hwnd, NULL, FALSE);
}

HDC ar_surface_get_dc(int h)
{
    return (HDC)(INT_PTR)ar_host(0x30, h, 0, 0, 0, 0, 0);
}

/* Free the SDK back buffer. */
static void ar_backbuffer_free(void)
{
    if (g_backdc) {
        if (g_backold) SelectObject(g_backdc, g_backold);
        DeleteDC(g_backdc);
    }
    if (g_backbmp) DeleteObject(g_backbmp);
    g_backdc = 0; g_backbmp = 0; g_backold = 0; g_backw = g_backh = 0;
}

/* Draw one frame: ensure a client-sized memory buffer, call the toy's paint
 * callback into it, then blit to the visible window. Flicker-free. */
static void ar_render(void)
{
    HWND hwnd = g_ctx.hwnd;
    RECT rc;
    HDC  wdc;
    int  w, h;

    if (!hwnd || !g_toy.paint) return;
    if (!GetClientRect(hwnd, &rc)) return;
    w = rc.right - rc.left; h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    wdc = GetDC(hwnd);
    if (!wdc) return;

    if (!g_backdc || w != g_backw || h != g_backh) {
        ar_backbuffer_free();
        g_backdc  = CreateCompatibleDC(wdc);
        g_backbmp = CreateCompatibleBitmap(wdc, w, h);
        if (g_backdc && g_backbmp) {
            g_backold = (HBITMAP)SelectObject(g_backdc, g_backbmp);
            g_backw = w; g_backh = h;
        } else {
            ar_backbuffer_free();
            ReleaseDC(hwnd, wdc);
            return;
        }
    }

    g_toy.paint(&g_ctx, g_backdc);
    BitBlt(wdc, 0, 0, w, h, g_backdc, 0, 0, SRCCOPY);
    ReleaseDC(hwnd, wdc);
#ifdef AR_TOY_TRACE
    { static int once = 0; if (!once) { once = 1; AR_TRACE("ar_render first: hwnd=%p w=%d h=%d", (void*)hwnd, w, h); } }
#endif
}

void ar_print(const char *text)
{
    /* selector 0x0e: dispatch(0x0e, flags, text, channel) -> chat window.
     * flags=0, channel=0 posts a plain line to the current channel. */
    if (text) ar_host(0x0e, 0, (int)(INT_PTR)text, 0, 0, 0, 0);
}

void ar_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    ar_print(buf);
}

int  ar_local_player_id(void) { return ar_host(0x1e, 0, 0, 0, 0, 0, 0); }
int  ar_num_players(void)     { return ar_host(0x0c, 0, 0, 0, 0, 0, 0); }

int  ar_get_player_info(int id, ArPlayerInfo *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    out->size = (int)sizeof *out;   /* host requires size == 0x6c */
    return ar_host(0x1b, id, (int)(INT_PTR)out, 0, 0, 0, 0);
}

int  ar_get_player_list(int max, int *out_ids)
{
    return ar_host(0x18, max, (int)(INT_PTR)out_ids, 0, 0, 0, 0);
}

int  ar_store_write(int id, const void *buf, int len)
{
    return ar_host(0x24, id, (int)(INT_PTR)buf, len, 0, 0, 0);
}
int  ar_store_read(int id, void *buf, int cap)
{
    return ar_host(0x25, id, (int)(INT_PTR)buf, cap, 0, 0, 0);
}

int  ar_reg_set(const char *name, const void *val, int type_or_len)
{
    return ar_host(0x22, (int)(INT_PTR)name, (int)(INT_PTR)val, type_or_len, 0, 0, 0);
}
int  ar_reg_get(const char *name, void *out, int cap, int type)
{
    return ar_host(0x23, (int)(INT_PTR)name, (int)(INT_PTR)out, cap, type, 0, 0);
}

int  ar_surface_create(int a, int b, int c) { return ar_host(0x29, a, b, c, 0, 0, 0); }
void ar_surface_destroy(int h)              { ar_host(0x2a, h, 0, 0, 0, 0, 0); }
int  ar_surface_valid(int h)                { return ar_host(0x33, h, 0, 0, 0, 0, 0); }
int  ar_surface_pitch(int h)                { return ar_host(0x37, h, 0, 0, 0, 0, 0); }
void*ar_surface_pixel_addr(int h, int x, int y)
{
    return (void *)(INT_PTR)ar_host(0x32, h, x, y, 0, 0, 0);
}
int  ar_surface_get_size(int h, int *w, int *height)
{
    return ar_host(0x31, h, (int)(INT_PTR)w, (int)(INT_PTR)height, 0, 0, 0);
}
int  ar_surface_palette(int h, int index)   { return ar_host(0x4b, h, index, 0, 0, 0, 0); }
void ar_surface_blit(int dst, int src, int x, int y, int w, int height, int a7, int a8)
{
    /* selector 0x2d takes eight payload args (dst,src,x,y,w,h,a7,a8) */
    if (g_dispatch) g_dispatch(0x2d, dst, src, x, y, w, height, a7, a8);
}
void ar_surface_blit_to_dc(int h, HDC dc, int x, int y)
{
    ar_host(0x2e, h, (int)(INT_PTR)dc, x, y, 0, 0);
}
void ar_surface_blit_rect(int dst, int src, int p3, int p4)
{
    ar_host(0x2f, dst, src, p3, p4, 0, 0);
}

void ar_send(int channel, const void *data, int len)
{
    /* selector 0x0f: dispatch(0x0f, dataPtr, byteLen, channel) */
    ar_host(0x0f, (int)(INT_PTR)data, len, channel, 0, 0, 0);
}
void ar_flush(void) { ar_host(0x49, 0, 0, 0, 0, 0, 0); }
int  ar_channel_ctl(int op, int arg2, int channel)
{
    return ar_host(0x09, op, arg2, channel, 0, 0, 0);
}
void ar_mark_time(void) { ar_host(0x4a, 0, 0, 0, 0, 0, 0); }

BOOL ar_key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

int ar_mouse(int *x, int *y)
{
    POINT pt;
    int buttons = 0;
    if (GetCursorPos(&pt) && g_ctx.hwnd) ScreenToClient(g_ctx.hwnd, &pt);
    else { pt.x = pt.y = 0; }
    if (x) *x = pt.x;
    if (y) *y = pt.y;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) buttons |= AR_MOUSE_L;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) buttons |= AR_MOUSE_R;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) buttons |= AR_MOUSE_M;
    return buttons;
}

/* ------------------------------------------------------------------ *
 *  The six exports (see arcadia_toy.def)
 * ------------------------------------------------------------------ */

int MPRegisterCallback(void *dispatch)
{
    g_dispatch = (ar_dispatch_t)dispatch;
    ensure_registered();
    AR_TRACE("MPRegisterCallback dispatch=%p", dispatch);
    return 0;
}

int MPNegotiationRequired(void)
{
    int v;
    ensure_registered();
    v = g_toy.negotiate ? (int)g_toy.negotiate : (int)AR_NEGOTIATE_DEFAULT;
    AR_TRACE("MPNegotiationRequired -> 0x%x", v);
    return v;
}

int MPOpenOffer(int a1, int a2, int a3, int a4, int a5, int a6)
{
    int args[6], i, rc = 0;
    ensure_registered();
    args[0]=a1; args[1]=a2; args[2]=a3; args[3]=a4; args[4]=a5; args[5]=a6;
    AR_TRACE("MPOpenOffer args=%08x %08x %08x %08x %08x %08x",
             a1, a2, a3, a4, a5, a6);
    /* The HWND is one of the six dwords; identify it robustly at runtime. */
    g_ctx.hwnd = 0;
    for (i = 0; i < 6; i++) {
        if (IsWindow((HWND)(INT_PTR)args[i])) { g_ctx.hwnd = (HWND)(INT_PTR)args[i]; break; }
    }
    AR_TRACE("MPOpenOffer chosen hwnd=%p (arg index %d)", (void*)g_ctx.hwnd, i < 6 ? i : -1);
    if (g_toy.open) rc = g_toy.open(&g_ctx, g_ctx.hwnd);
    AR_TRACE("MPOpenOffer open()->%d", rc);
    return rc;
}

int MPCloseOffer(int a1, int a2, int a3)
{
    (void)a1; (void)a2; (void)a3;
    ensure_registered();
    if (g_toy.close) g_toy.close(&g_ctx);
    ar_backbuffer_free();
    g_ctx.hwnd = 0;
    return 0;
}

int MPIncomingPacket(int channel, void *data, int len)
{
    ensure_registered();
    if (g_toy.packet) g_toy.packet(&g_ctx, channel, data, len);
    return 0;
}

int MPSendGEDMessage(int code, int chan, void *p3, void *p4, int p5)
{
    ensure_registered();
#ifdef AR_TOY_TRACE
    /* Log the first 150 GED calls verbatim (no dedup) to reveal the per-frame
     * loop, then only note newly-seen codes to keep the file bounded. */
    if (ar_ged_calls < 150) {
        ar_ged_calls++;
        AR_TRACE("GED[%d] code=0x%x chan=%d p3=%p p4=%p p5=%d",
                 ar_ged_calls, code, chan, p3, p4, p5);
    } else if ((unsigned)code < 256 && !ar_seen_ged[code]) {
        ar_seen_ged[code] = 1;
        AR_TRACE("GED late-new code=0x%x", code);
    }
#endif

    if (g_toy.ged) {
        int handled = g_toy.ged(&g_ctx, code, chan, p3, p4, p5);
        if (handled) return 0;
    }

    switch (code) {
    case 0x06: /* per-frame update: host sends this ~32x/s with chan=GetTickCount.
                * This is the frame drive. Update state, then draw a frame. */
        if (g_toy.tick) g_toy.tick(&g_ctx, (unsigned)chan);
        ar_render();
        return 0;

    case 0x08: /* serialize outgoing snapshot into p3, write length to *p4 */
        if (g_toy.serialize) {
            int n = g_toy.serialize(&g_ctx, chan, p3, AR_SNAPSHOT_CAP);
            if (p4) *(int *)p4 = n;
            return n;
        }
        if (p4) *(int *)p4 = 0;
        return 0;

    case 0x0e: /* chat / system line (best-effort: text is p3) */
        if (g_toy.chat) g_toy.chat(&g_ctx, chan, (const char *)p3);
        return 0;

    case 0x0f: /* player event (best-effort: name is p3) */
        if (g_toy.player_event) g_toy.player_event(&g_ctx, chan, (const char *)p3);
        return 0;

    default:
        return 0; /* unhandled events are safely ignored */
    }
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst; (void)reason; (void)reserved;
    return TRUE;
}
