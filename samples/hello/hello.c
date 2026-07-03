/*
 * hello — a minimal Arcadia toy built with the Toy SDK.
 *
 * It paints a bouncing ball and a greeting into the Arcadia canvas window,
 * animates on the host tick, writes a line to the chat window when it opens,
 * and demonstrates the network serialize/packet hooks. Build it with CMake
 * and the i686-w64-mingw32 toolchain (see sdk/README.md), then drop the staged
 * folder into Arcadia/Toys/.
 */
#include <arcadia/toy.h>

static int   s_x = 40, s_y = 40;   /* our ball position */
static int   s_dx = 3, s_dy = 2;   /* our ball velocity */
static DWORD s_last;
static DWORD s_last_send;
static int   s_peer_x = -1, s_peer_y = -1;  /* last position heard from a peer */
static int   s_got_peer = 0;

#define NET_CHANNEL 0              /* broadcast channel for ar_send/packet */

static int on_open(ArContext *ctx, HWND hwnd)
{
    (void)ctx; (void)hwnd;
    ar_print("hello: the SDK sample toy is up. Type and it echoes over the net.");
    s_last = GetTickCount();
    return 0; /* success */
}

static void on_close(ArContext *ctx)
{
    (void)ctx;
    ar_print("hello: goodbye.");
}

static void on_tick(ArContext *ctx, unsigned now_ms)
{
    HWND hwnd = ar_window();
    RECT rc;
    (void)ctx;

    /* advance ~60 Hz regardless of tick cadence */
    if (now_ms - s_last < 16) return;
    s_last = now_ms;

    if (!hwnd || !GetClientRect(hwnd, &rc)) return;

    /* Left-click grabs the ball (demonstrates input polling). */
    if (ar_mouse(NULL, NULL) & AR_MOUSE_L) {
        int mx, my;
        ar_mouse(&mx, &my);
        s_x = mx; s_y = my;
    }

    s_x += s_dx; s_y += s_dy;
    if (s_x < 8 || s_x > rc.right  - 8) s_dx = -s_dx;
    if (s_y < 8 || s_y > rc.bottom - 8) s_dy = -s_dy;

    /* Broadcast our position to peers a few times a second. */
    if (now_ms - s_last_send >= 200) {
        int msg[2];
        s_last_send = now_ms;
        msg[0] = s_x; msg[1] = s_y;
        ar_send(NET_CHANNEL, msg, (int)sizeof msg);
    }

    /* No repaint call needed: the SDK draws a frame after every tick. */
}

static void on_paint(ArContext *ctx, HDC dc)
{
    HWND    hwnd = ar_window();
    RECT    rc;
    HBRUSH  bg, ball;
    HGDIOBJ old;
    (void)ctx;

    if (!hwnd || !GetClientRect(hwnd, &rc)) return;

    bg = CreateSolidBrush(RGB(16, 24, 48));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(220, 220, 240));
    TextOutA(dc, 12, 10, "hello, Arcadia", 14);

    /* our ball (yellow) */
    ball = CreateSolidBrush(RGB(255, 200, 64));
    old  = SelectObject(dc, ball);
    Ellipse(dc, s_x - 8, s_y - 8, s_x + 8, s_y + 8);
    SelectObject(dc, old);
    DeleteObject(ball);

    /* the peer's ball (cyan), if we've heard from one — proof of round-trip */
    if (s_got_peer) {
        ball = CreateSolidBrush(RGB(64, 220, 255));
        old  = SelectObject(dc, ball);
        Ellipse(dc, s_peer_x - 8, s_peer_y - 8, s_peer_x + 8, s_peer_y + 8);
        SelectObject(dc, old);
        DeleteObject(ball);
        TextOutA(dc, 12, 28, "cyan = peer (via ar_send)", 25);
    }
}

/* Full-state sync path (host pulls this on open / ar_flush). */
static int on_serialize(ArContext *ctx, int channel, void *buf, int cap)
{
    (void)ctx; (void)channel;
    if (cap < (int)(2 * sizeof(int))) return 0;
    ((int *)buf)[0] = s_x;
    ((int *)buf)[1] = s_y;
    return 2 * (int)sizeof(int);
}

/* Discrete receive: a peer's ar_send() arrives here. Track their ball. */
static void on_packet(ArContext *ctx, int channel, const void *data, int len)
{
    (void)ctx; (void)channel;
    if (len >= (int)(2 * sizeof(int))) {
        const int *p = (const int *)data;
        if (!s_got_peer) ar_printf("hello: first packet from a peer at (%d,%d)", p[0], p[1]);
        s_peer_x = p[0]; s_peer_y = p[1];
        s_got_peer = 1;
    }
}

static void on_chat(ArContext *ctx, int channel, const char *text)
{
    (void)ctx; (void)channel;
    if (text) ar_printf("hello heard: %s", text);
}

void ArcadiaToyRegister(ArToy *toy)
{
    toy->name         = "hello";
    toy->open         = on_open;
    toy->close        = on_close;
    toy->tick         = on_tick;
    toy->paint        = on_paint;
    toy->serialize    = on_serialize;
    toy->packet       = on_packet;
    toy->chat         = on_chat;
    /* toy->negotiate = 0  -> SDK uses AR_NEGOTIATE_DEFAULT */
}
