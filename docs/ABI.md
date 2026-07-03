# Arcadia Toy ABI — Reverse-Engineered Specification

This document describes the binary interface between **Arcadia.exe** (the host,
by Synthetic Reality, ~2007) and a **toy** (a downloadable mini-game). It was
recovered by static analysis of `Arcadia.exe` and the twelve shipped toys with
`capstone`/`pefile` (see `sdk/re/`). RVAs below refer to the shipped
`Arcadia.exe` (`ImageBase 0x00400000`) and to `Toys/toy1/TTPool.dll`
(`ImageBase 0x10000000`) unless noted.

Confidence is marked per item: **[V]** verified from both sides of the call,
**[I]** inferred from strong evidence, **[?]** best guess / partially decoded.

---

## 1. What a toy is

A toy is a **32-bit MFC42 DLL** (`pei-i386`). Every shipped toy links
`MFC42.DLL` (by ordinal), `MSVCRT.dll`, and the usual Win32 libraries, and
**imports nothing from `Arcadia.exe`, `SRNet.dll`, or `Sr3d.dll`.** All host
services are reached through a single function pointer handed to the toy at
load time. This is what makes a clean third-party SDK possible: you do not need
MFC or any host import library — only the ability to emit a 32-bit DLL that
exports six functions.

A toy on disk is a folder under `Arcadia/Toys/toyNN/` containing:

```
toyNN/
  <name>.dll        the toy (see toy.ini dll=)
  toy.ini           [toy_info] name=, desc=, dll=, preview=, longdesc=
  version.ini       [toy_info] version=0.00NN
  <preview>.jpg      thumbnail shown in the launcher
  <assets...>        sfx/, art/, midi/, models, etc. (toy-defined)
```

`toy.ini` / `version.ini` are Windows-profile (`GetPrivateProfileString`) INI
files. Only `[toy_info]` is used. `dll=` is the DLL path relative to the toy
folder.

**Discovery (verified live).** On entering a channel the host runs
`FindFirstFile("<Toys>\*.*")` and, for every **subdirectory** (attribute
`0x10`, skipping `.`/`..`), reads `<sub>\toy.ini`. So any folder with a valid
`toy.ini` is picked up automatically — "Check for New Toys" is only for
*downloading* from synthetic-reality.com and is not needed for local toys.
**However**, the enumerator computes `atoi(folderName + 3)` and **skips the
folder unless that value is > 0** (host RVA `0x22fb1`/`0x2303c`). In practice the
folder must be named **`toy<N>`** with `N` a positive integer (the first three
characters are simply skipped; `hello` → `atoi("lo")` = 0 → ignored). `N`
becomes the toy's identity and its registry namespace
(`HKCU\...\Arcadia\TOYS\TOY<N>`). The 12 official toys use 1..12, so
third-party toys should use **N ≥ 13**.

---

## 2. The six exports (host → toy)

Arcadia resolves these six names with `GetProcAddress` after `LoadLibrary`
(host loader at RVA `0x19040`; names stored at `0x4587a0..0x4587b4`). All six
are **`__cdecl`, undecorated names** (the host cleans the stack: `add esp, N`
after each call). Three are required (`MPOpenOffer`, `MPCloseOffer`,
`MPNegotiationRequired`); the host aborts the load if any of those three is
missing (RVA `0x190ee`).

| Ordinal-order name       | Host call site      | Args | Meaning |
|--------------------------|---------------------|------|---------|
| `MPRegisterCallback`     | `0x19155`           | 1    | Hand the toy the host dispatch fn. **[V]** |
| `MPNegotiationRequired`  | indirect            | 0    | Return a capability/version bitmask. **[I]** |
| `MPOpenOffer`            | `0x191e8`           | 6    | Begin a game/session. **[V]** |
| `MPCloseOffer`           | `0x18e75`           | 3    | End the session. **[V]** |
| `MPIncomingPacket`       | indirect (`0x17b39`)| 3    | Deliver a received network packet. **[V]** |
| `MPSendGEDMessage`       | 16 sites            | 5    | Master host→toy event pump. **[V]** |

### 2.1 `int MPRegisterCallback(void *dispatch)` **[V]**
The host passes a single pointer — the address of its **dispatch function**
(host RVA `0x17b94`). The toy caches it in a global and returns 0. Every
host service the toy uses later goes through this one pointer
(`call dword ptr [g_dispatch]`). TTPool body (RVA `0x8df0`):

```
mov eax,[esp+4] ; dispatch
mov [g],eax     ; cache
xor eax,eax
ret             ; cdecl
```

### 2.2 `int MPNegotiationRequired(void)` **[I]**
Returns a constant bitmask; TTPool returns `0x08000000` (RVA `0x8db0`). Read by
the host during offer setup to decide required protocol features. Treat as
"return your required-features flags"; `0x08000000` is a safe default (it is
what the reference billiards toy returns).

### 2.3 `int MPOpenOffer(a1..a6)` **[V/I]**
Called when the toy is launched or a networked game is joined. The host pushes
six dwords (RVA `0x191d4..0x191e8`); its own debug string documents the three
it cares about: `pMPOpenOffer(hW:0x%X, pS:0x%X, len:%d) returned %d`
(RVA `0x592e0`), i.e. an **HWND**, a **pointer to an offer struct**, and a
**length**. From the host it is effectively:

```
MPOpenOffer(void *ctx,        // *(toy+0x72c) host session context
            HWND  hWnd,       // Arcadia canvas window the toy draws into
            void *offer,      // offer/description struct (see below)
            void *outBuf,     // 512-byte scratch/return buffer (toy fills)
            int   outCap,     // 0x200
            int   reserved)
```

The toy caches the HWND (this is the window it renders into and reads input
from), inspects `offer`, may write a display string into `outBuf`, and returns
a status. In the reference toys the `offer` struct carries a config value at
`+0x0c` and a name/path string at `+0x38` (TTPool RVA `0x8c20`).

**Verified live** (SDK trace, toy launched from the Solo channel): the six args
are e.g. `hwnd=0x000f068e, 0x00ef7fc0, offer=0x004b0938, 0x00ef7d1c,
outBuf=0x001af4a4, 0x200`. `arg1` is the toy's canvas HWND; `arg3` points into
host `.data`; `arg6` = `0x200` = the `outBuf` capacity. **Return value: `0`
means success.** The host checks it at RVA `0x1926f`: a **non-zero** return
takes the failure path (`TOY<N>` set to −1, `sub_18db4` unloads the toy). This
is the opposite of the usual convention, so return 0 to keep the toy loaded.

> The exact field layout of `offer` beyond `+0x0c`/`+0x38` is only partially
> decoded **[?]**. The SDK treats it opaquely and exposes the fields we trust.

### 2.4 `int MPCloseOffer(a1, a2, a3)` **[V]**
Host tears down the session (RVA `0x18e59`): `MPCloseOffer(*(toy+0x72c),
*(toy+0x730), *(toy+0x734))` then `FreeLibrary`. Toy should free its resources.

### 2.5 `int MPIncomingPacket(int channel, void *data, int len)` **[V]**
A network payload addressed to this toy arrived. Delivered from the host's
receive path (RVA `0x17af5` builds the buffer, `0x17b39` calls the pointer).
The reference toy ignores packets with `len < 4` (TTPool RVA `0x2058`). Payloads
are toy-defined.

### 2.6 `int MPSendGEDMessage(int code, int channel, void *buf, void *p4, int p5)` **[V]**
Despite the name, this is the **host→toy event pump** ("GED" = game-event
dispatch). The host calls it for many events with `code` in `2..0x14`
(TTPool switch at RVA `0x2310`: `code-2`, range-checked `<= 0xb`, jump table at
`0x27c8`). Recovered codes (host push constants at the 16 call sites):

| code | host site | Meaning (evidence) |
|-----:|-----------|--------------------|
| 6    | (per-frame) | **Per-frame update / frame tick** — the host fires this ~32×/s while the toy is active; `chan` = `GetTickCount()`, `p3` = local player id, `p5` = toy number. This is the render/update drive. Confirmed by live trace (138+ consecutive calls). **[V]** |
| 2    | `0xa065`,`0xa5a3` | Enumerate/describe the toy's UI buttons; toy fills `buf` with a label + flags. **[I]** |
| 7    | `0x18e45`,`0x19245` | **Local-player identity** on open (`p3` small int, `p4` = local player id, e.g. `0x77fff9e6`) — *not* shutdown (corrected by live trace). **[V]** |
| 8    | `0x18d67` | **Serialize outgoing state for channel `p2` (0–9)** into `p3`; write byte length to `*p4`, and return length (>0 ⇒ host transmits it on that channel). The host loops all 10 channels (`sub_18d40`) at open and on `ar_flush` (0x49). This is the full-state / late-joiner sync path (MPChess uses it; most action toys use `ar_send`/0x0f instead). **[V]** |
| 0xb  | `0x18df9` | Opened / session ready. **[?]** |
| 0xc  | `0x19254` | Sent right after the open sequence (all-zero args). **[V]** |
| 0xd  | `0x1bb4b` | **Player-entered** notification (emitted next to `"* %s has entered"` / `gong.wav`) — *not* a periodic tick (corrected by live trace). **[V]** |
| 0xe  | `0x2555b` | Incoming chat/system line for a channel (`code, chan, p1, p2, p3`). **[I]** |
| 0xf  | `0x28c8f`,`0x28e9f` | Player action/roster event; carries `(localId, name, val, val)` (host passes a player name, e.g. `"Biggles"`). **[I]** |
| 0x10 | `0x28ab8` | Roster/state update. **[?]** |
| 0x11 | `0xa2a6` | Activation/focus/enable toggle (boolean-ish arg). **[?]** |
| 0x13 | `0x2602e` | (decoded partially). **[?]** |
| 0x14 | `0xaa20`  | (decoded partially). **[?]** |

The reference billiards toy only implements codes 2, 6, 0xb, 0xc, 0xd
meaningfully (`code-2` jump table indices `[0,5,5,5,1,5,5,5,5,2,3,4]`); all
others fall through to a no-op returning 0. **A toy is free to ignore any code
it does not care about** — return 0.

Calling convention note: the *export* is `__cdecl`; internally the reference
toys immediately trampoline into a `thiscall` method on their singleton game
object (`mov ecx, <gameobj>; call ...`). The SDK shim keeps everything `__cdecl`
at the boundary and hands you plain C callbacks.

---

## 3. The host dispatch function (toy → host)

`MPRegisterCallback` gives the toy one `__cdecl` function:

```c
int __cdecl HostDispatch(int selector, /* selector-specific args */ ...);
```

Host body at RVA `0x17b94`: `selector -= 9; if ((unsigned)selector > 0x43)
return -1; jmp [0x18bf7 + selector*4]`. So valid selectors are **9..0x4c** via a
68-entry jump table. Arguments are read at `[ebp+0x0c]` (arg1), `[ebp+0x10]`
(arg2), `[ebp+0x14]` (arg3)… i.e. `HostDispatch(selector, arg1, arg2, …)`.
Selectors `0x0a,0x0b,0x0d,0x10..0x17,0x19,0x1a,0x1c,0x1f,0x20` share the
"return −1" default (reserved/unimplemented).

Decoded selectors (handlers dumped in `sdk/re/out/handlers.txt`):

### Session / players
| sel | args | name in SDK | meaning |
|----:|------|-------------|---------|
| 0x0c | 0 | `ar_host_num_players` | returns global `[0x4e6220]` (player/slot count). **[?]** |
| 0x18 | 2 (max,out) | `ar_get_player_list` | fills `out[]` with active player ids from the player table (stride `0x1450`, base `0x4e5d78`); returns count. **[I]** |
| 0x1b | 2 (idx,out) | `ar_get_player_info` | fills a **0x6c-byte** struct: `out[0]` must be `0x6c` on entry; name (31 bytes) at `+0x08`; flags/bot/team at `+0x24..`. **[V]** |
| 0x1e | 0 | `ar_local_player_id` | returns global `[0x458d1c]` = local player index. **[V]** |
| 0x3b | 0 | `ar_local_player_field` | returns local player's field `[+4]` from the player table. **[I]** |

### Persistence & registry
| sel | args | name | meaning |
|----:|------|------|---------|
| 0x24 | 3 | `ar_store_write` | write a blob to per-toy persistent storage (`sub_2b7ec`). **[I]** |
| 0x25 | 3 | `ar_store_read`  | read that blob back (`sub_2b9e5`). **[V]** (used by every toy at open) |
| 0x22 | 3 (name,val,ty) | `ar_reg_set` | write `HKCU\Software\Synthetic Reality\Arcadia\TOYS\TOY%d\<name>`. **[V]** |
| 0x23 | 5 | `ar_reg_get` | read that value. **[V]** |
| 0x45 | 3 (name,out,n) | `ar_reg_get_ints` | read int array `GamePadFunctions-%d` via `atoi`. **[V]** |
| 0x46 | 3 (name,in,n)  | `ar_reg_set_ints` | write that int array. **[V]** |

### Off-screen surfaces (DIB handles)
The host keeps a 1000-slot handle table at `0x4af930`. Each handle is a C++
object wrapping an 8-bit DIB (fields: `+4` DIB/bitmap, `+8` bits pointer,
`+0x10` HDC/palette source). Handlers range-check `0 <= h < 1000` and
`table[h] != NULL`.

| sel | args | name | meaning |
|----:|------|------|---------|
| 0x29 | 3 | `ar_surface_create` | allocate a surface object, return its handle (slot) or −1. **[V]** |
| 0x2a | 1 | `ar_surface_destroy` | free a surface handle. **[V]** |
| 0x2b | 2 | `ar_surface_load`   | (op with a pointer arg; e.g. load bits). **[?]** |
| 0x2c | 2 | `ar_surface_op2`    | second load/op variant. **[?]** |
| 0x2d | 8 | `ar_surface_blit`   | blit `src`→`dst` `(dst,src,x,y,w,h,a7,a8)`; `a7==a8==0` uses fast path (vtbl `+0x18`). **[V]** |
| 0x2e | 4 | `ar_surface_blit_to_dc` | blit surface `h` to a **raw HDC** you own `(h, hdc, x, y)` (host wraps hdc via MFC `sub_49e3a`). **[V]** |
| 0x2f | 4 | `ar_surface_blit_rect`  | blit `(dst,src,p3,p4)` via vtbl `+0x24`. **[V]** |
| 0x30 | 1 | `ar_surface_get_dc` | wraps `[+0x10]` as a CDC and returns its **HDC** (`[+4]`). This is how the shipped toys get a device context to draw into with plain GDI — none of them import `GetDC`/`BeginPaint`. **[V]** |
| 0x31 | 3 | `ar_surface_get_size` | writes width/height through the two out-pointers. **[I]** |
| 0x32 | 3 | `ar_surface_pixel_addr` | returns address of pixel `(x,y)` in the DIB bits (bottom-up 8bpp, `stride=(w+3)&~3`). Direct pixel poking. **[V]** |
| 0x33 | 1 | `ar_surface_valid`  | 1 if the surface has bits, else 0. **[V]** |
| 0x34 | 3 | `ar_surface_setmode`| set a mode flag (`sub_1d9a0`, arg→2 or 3). **[?]** |
| 0x35 | 1 | `ar_surface_bits`   | returns `[+4]` (DIB struct / base). **[I]** |
| 0x36 | 1 | `ar_surface_field8` | returns `[+8]` (bits pointer). **[I]** |
| 0x37 | 1 | `ar_surface_pitch`  | returns row stride `((w)+3)&~3`. **[V]** |
| 0x41 | 6 | `ar_surface_blit_ex`| extended two-surface blit (`sub_1d193`). **[?]** |
| 0x4b | 2 | `ar_surface_palette`| returns packed RGB of palette entry `idx`. **[V]** |

### Drawing, text, sound, misc
| sel | args | name | meaning |
|----:|------|------|---------|
| 0x0e | 3 (flags,text,chan) | `ar_print` | **Post a text line to the chat window** via `sub_92b4`→`sub_dfef` (the chat control at `[0x458794]+0x13c`). `flags=0,chan=0` = plain line on the current channel. Confirmed live. **[V]** |
| 0x0f | 3 (data,len,chan) | `ar_send` | **Broadcast `len` bytes of `data` to peers on `chan`** as net message type `0x67` (`sub_17aa3`→`sub_1c083`); peers get it via `MPIncomingPacket(chan,data,len)`. Payload is RLE+escape coded (`sub_1798c`, §3.2) — **binary-safe**. The main discrete-message path (synSpades/synRTS/TTPool). **[V]** |
| 0x09 | 3 | `ar_channel_ctl`   | sub-op `arg1∈{1,2,4,5,6}` = join/leave/reset channel `arg3`. **[I]** |
| 0x21 | 2 | `ar_post_command`  | `arg1==1` → `PostMessage(mainwnd, WM_COMMAND=0x111, 0x800f, 0)`; else `sub_19b76(arg2)`. **[I]** |
| 0x39 | 3 | `ar_draw_text`     | draw text with font `arg1&3` (font table at `0x469758`, stride `0x14`). **[I]** |
| 0x38 | 5 | `ar_play_sound`    | `sub_2556e(1, a1, a2, &g, a4, a5)` — sound/asset trigger. **[?]** |
| 0x3d | 5 | `ar_surface_player`| associate a surface with a player/avatar. **[?]** |
| 0x3f | 5 | `ar_draw_prim0`    | `sub_319a(0, …)` drawing primitive. **[?]** |
| 0x40 | 3 | `ar_draw_prim1`    | `sub_319a(1, a1, a2, 0x180, 0x100, 0, a3)`. **[?]** |
| 0x42 | 4 | `ar_misc_42`       | `sub_e890`. **[?]** |
| 0x43 | 3 | `ar_misc_43`       | `sub_681b` (arg3 must be non-null). **[?]** |
| 0x44 | 5 | `ar_misc_44`       | `sub_33b5`. **[?]** |
| 0x48 | 2 | `ar_misc_48`       | `sub_28030(arg2)`. **[?]** |
| 0x49 | 0 | `ar_flush`         | **Flush outgoing state**: host pulls your `MPSendGEDMessage(8,…)` snapshot for channels 0–9 and transmits each nonzero result (`sub_18daa`→`sub_18d40`). **[V]** |
| 0x4a | 0 | `ar_mark_time`     | `GetTickCount()` → `[0x4581a4]` (frame timing marker). **[V]** |
| 0x4c | 2 | `ar_print`         | `sub_68a4(text, fmt?)` — **NOT chat output** (live test: nothing appeared in the chat window); gated on global `[0x464280]`. Purpose TBD. **[?]** |
| 0x3a | 1 | `ar_set_flag`      | store `arg1` → `[0x45e9b4]`. **[V]** |
| 0x3e | 0 | `ar_get_3e`        | returns `[0x45efbc]`. **[?]** |
| 0x47 | 0 | `ar_get_47`        | returns `[0x45e0f0]`. **[?]** |

Selectors not listed above (within 9..0x4c) hit the reserved default and
return −1.

---

### 3.1 Networking model (verified)

Two complementary paths, both host-mediated (the toy never touches a socket):

* **Discrete messages — `ar_send` (selector 0x0f).** `ar_send(chan, data, len)`
  broadcasts a compact payload to the other players on `chan`. The host
  text-encodes it and sends it as GED net message `0x67`; every peer receives
  the decoded bytes through `MPIncomingPacket(chan, data, len)` → the SDK
  `packet()` callback. This is what real-time/action toys use for moves and
  events (synSpades, synRTS, synSpace, TTPool…). Sender/channel are preserved.

* **Full-state sync — `serialize()` (GED 8) + `ar_flush` (selector 0x49).**
  The host asks the toy to serialize its authoritative state per channel (GED
  code 8, channels 0–9); the toy fills the buffer and returns a length (>0 ⇒
  transmit). The host pulls this at toy open (initial sync, e.g. for a joining
  player) and whenever the toy calls `ar_flush`. MPChess is the one shipped toy
  that relies solely on this path.

Chat/text output is separate: `ar_print` (selector 0x0e) appends a line to the
Arcadia chat window on the current channel.

**Live multiplayer test (2026-07-01): PASSED — `ar_send` round-trip confirmed
between two clients.** Setup that worked on a single machine:

1. Two separate installs (so each has its own serial). The install serial lives
   in `sernums/GROUP_*.ind`, is minted randomly on first run (not machine-
   derived, not in the registry), so a fresh copy gets a distinct id. A single
   install run twice fails with "Someone else … is using the same serial
   number." Here: install-1 = `77FFF9E6`, install-2 = `55B0685D`.
2. A **direct peer-to-peer** session (no MIX master needed): install-1 chooses
   Multiplayer → TELNET/Generic → **Wait For Call** (listens TCP 8000);
   install-2 → TELNET/Generic → Address Book → dials `<host-ip>:8000` →
   Connect → Play Game. Both land in the same channel and see each other in the
   roster. (A single TCP stream carries everything, so the two clients don't
   fight over a UDP port.)
3. Load the Hello toy on both. Each toy `ar_send`s its ball position ~5×/s; the
   peer receives it via `MPIncomingPacket` → `packet()` and draws it.

Result: both clients render their own ball **and** the peer's ball, and both
log "first packet from a peer at (x,y)". Bidirectional traffic confirmed in the
net meter. This exercises the full `ar_send`(0x0f) → wire codec →
`MPIncomingPacket` → `packet()` path end to end between real clients.

The MIX-server path (vs. direct P2P) additionally requires a MIX **master**
server for discovery — a lone game server does not answer the client's
server-list query, and the generic TELNET transport does not sustain the MIX
protocol. For LAN/loopback testing, the direct "Wait For Call" path above is
the simplest route.

### 3.2 GED payload wire codec (verified)

`ar_send` payloads are transformed by `sub_1798c` (encode) / `sub_17a36`
(decode) into a **NUL-terminated** stream with three markers:

| marker | meaning |
|--------|---------|
| `0x82` | literal `0x00` |
| `0x80 <b+0x20>` | literal byte `b`, used for `b < 0x20` or `b ∈ {0x80,0x81,0x82}` |
| `0x81 <N+0x20>` | run: repeat the following decoded byte `N` times (`N ≤ 128`) |
| any other byte | literal (`0x20–0x7F`, `0x83–0xFF`) |

It is a lossless RLE+escape codec: **every byte 0x00–0xFF round-trips**, so
`ar_send`/`packet` carry arbitrary binary. Worst case is 2 bytes out per input
byte plus the terminator (the `len*3+2` buffer is a safe over-allocation);
repeated bytes compress. A verified reference implementation with a fuzz
self-test lives in `sdk/re/tools/ged_codec.py`.

## 4. Rendering & input model

**How the shipped toys render (verified).** A shipped toy imports **no**
`GetDC`, `BeginPaint`, `CreateWindow`, `RegisterClass` or `SetWindowLong` — it
never creates or subclasses a window. Instead it:

1. Creates one or more **off-screen 8-bit DIB surfaces** with `ar_surface_create`
   (selector 0x29).
2. Obtains a **memory HDC** for a surface with `ar_surface_get_dc`
   (selector 0x30) and draws into it with ordinary GDI (`Rectangle`, `Ellipse`,
   `Polygon`, `BitBlt`, and host-loaded TrueType fonts such as Castellar/Verdana),
   or pokes pixels directly via `ar_surface_pixel_addr` (selector 0x32).
3. The **host owns the visible window and its palette** (it `GetDC`s the canvas
   only to run `GetSystemPaletteEntries`/`RealizePalette`/`UpdateColors`) and
   **composites the toy's surface(s)** to the screen. The toy nudges refresh with
   `InvalidateRect` on the `HWND` it was handed at `MPOpenOffer`.

Frame timing is host-driven: GED code `0xd` fires from the host's timer (right
after `GetTickCount`), and `ar_mark_time` (0x4a) records a frame marker.

**How the SDK renders (what this SDK gives you).** Because a *new* toy is not
restricted to the shipped toys' import set, the SDK offers a simpler, guaranteed
path in addition to the native surface API:

* The SDK maintains a **double-buffered memory DC** sized to the canvas client
  area and calls your `paint(ctx, hdc)` callback into it once per host tick,
  then `BitBlt`s it onto the Arcadia window. Flicker-free, no palette juggling.
* You may ignore that and use the native pipeline directly
  (`ar_surface_create` → `ar_surface_get_dc` → GDI, present with
  `ar_surface_blit_to_dc`) for palette-perfect integration.

**Bring-up status (live test, 2026-07-01): WORKING.** The SDK sample toy is
discovered, loads, resolves all six exports, receives `MPRegisterCallback`
(dispatch = `0x00417b94`, as reversed) and `MPOpenOffer` with the correct args
(`open()` returns 0 = success), then gets the open GED burst (`8`×N-channels,
`7`, `0xc`) followed by a continuous **`6`** per-frame tick. Routing GED `6` to
the SDK's update+paint made the toy take over the canvas (the "Select Toy"
launcher is replaced) and render "hello, Arcadia" plus a bouncing ball,
animating at ~32 Hz. **The direct double-buffered window-DC paint works fine —
no host surface is required** (the earlier concern that the compositor would
overwrite it did not materialise; drawing to the toy's canvas HWND is
authoritative). Input via `GetAsyncKeyState`/`GetCursorPos` polling also works,
though a single fast click can fall between 32 Hz samples. The host DIB-surface
API (0x29/0x30/0x2d/0x32/…) remains available for palette-perfect compositing
but is optional.

**Input.** The shipped toys **poll**: `GetAsyncKeyState` for keys and
`GetCursorPos` + `ScreenToClient` for the mouse (with `SetCapture`/`PtInRect`
for hit-testing), all against the canvas `HWND`. Do the same from your `tick`
callback via `ar_window()`. (Some pointer/among-players events also arrive as
GED codes 0x0e/0x0f, exposed as the `chat`/`player_event` callbacks, but polling
is the reliable, verified route.)

---

## 5. Calling-convention summary

* All six exports: `__cdecl`, undecorated names, resolved by `GetProcAddress`.
* Host dispatch: one `__cdecl` fn, `int(int selector, …)`, caller-cleaned.
* Everything is 32-bit x86. Build with a 32-bit toolchain (`i686-w64-mingw32`
  or MSVC `/MACHINE:X86`). No MFC or host import library is required.

## 6. Source of truth / how to re-derive

`sdk/re/tools/` contains the analysis scripts:

* `srdis.py` — export/imports/strings/xref/linear disasm.
* `iat.py` — IAT-annotated xref.
* `find_ref.py` — byte-accurate reference finder (handles data-in-code).
* `scan_host.py` — enumerate a toy's host-dispatch selectors + arg counts.
* `annot.py` — disassembler that resolves string operands and IAT calls.
* `dump_handlers.py` — dump all 68 dispatch handlers, annotated.

Regenerate the raw evidence:

```
sdk/re/.venv/Scripts/python sdk/re/tools/dump_handlers.py > sdk/re/out/handlers.txt
sdk/re/.venv/Scripts/python sdk/re/tools/scan_host.py Toys/toy3/MPChess.dll sites
```
