# mixserver

An all-in-one **Synthetic Reality MIX** test server for Arcadia (and the other SR
games). One Go process provides everything a client needs for **"Any Public MIX
Game Server"**, so two clients can meet in a toy with no `mix.exe`, no ReMix, and
no external master — everything runs locally.

It does three jobs at once:

1. **HTTP `:80`** — serves `synreal.ini` (host-aware), pointing every game's
   master at this machine.
2. **UDP master + ping** (ports `20999`–`23999`, one per game) — answers the
   client's master server-list query with a canned list of one server (itself),
   and answers the client's `P`/`Q`/`R` server pings.
3. **TCP game relay** (same ports) — the line-oriented MIX session: slot
   assignment, scene/player routing, and broadcast of game packets to the other
   connected clients.

It is deliberately **stateless on the master side** (it accepts no registrations;
it just advertises itself) and it **hosts the game servers itself**.

## Usage

```sh
go build -o mixserver.exe .
./mixserver.exe          # needs port 80 free (stop IIS / "World Wide Web Publishing" if bound)
```

Then patch each client's `SRNet.dll` to fetch `synreal.ini` from localhost and run
the game:

1. In `SRNet.dll`, replace the URL string
   `http://www.synthetic-reality.com/synreal.ini` with
   `http://localhost/synreal.ini` (shorter, so NUL-pad the remainder — same-length,
   in-place patch). Keep a backup.
2. Launch Arcadia → **Multiplayer Game** → network **"Any Public MIX Game Server"**
   → **Play Game**. The server list shows **"Test Arcadia"**; select it and
   **Connect**.

Two clients need two **installs** (the serial number is per-install; two clients
sharing one install collide with *"someone else is using the same serial number"*).
Copy the Arcadia folder to a second location — a fresh copy mints its own serial —
and patch its `SRNet.dll` too.

Ports: WoS `23999`, Warpath `22999`, **Arcadia (TOY) `21999`**, RealmChat `20999`.

## The MIX protocol (reverse-engineered)

Recovered from `SRNet.dll` (client side), ReMix (server side), and a captured
live master response. Everything is one machine's worth of UDP + TCP.

### 1. Find the master — `synreal.ini` over HTTP

The client GETs `synreal.ini` and reads `[<GAME>] master=<ip>:<port>`. The address
is parsed with `inet_addr`, which does **not** resolve hostnames, so the value must
be a dotted IPv4 (`127.0.0.1`, not `localhost`). Sections: `[WoS]`, `[W97]`,
`[TOY]` (Arcadia), `[RC]`.

### 2. Get the server list — UDP master query/response

Client → master (UDP, on the game's port), an ASCII datagram:

```
?alias=<>,name=<>,email=<>,loc=<>,sernum=<hex>,HHMM=<>,d=<hex>,v=<hex>,w=<hex>.
```

(No game field — the master keys on which port the query arrived on.)

Master → client, a little-endian binary blob:

```
Header (12 bytes):
  [0:2]   magic   0x0202
  [2:4]   version (uint16, shown as "ver %X")
  [4:6]   count   (uint16, number of records)
  [6:8]   recSize (uint16, = 0x0C; records start at this offset)
  [8:12]  master dword (cosmetic)
Record (12 bytes each, starting at 0x0C):
  [0:4]   IP   (network byte order, for inet_ntoa)
  [4:6]   port (uint16, little-endian)
  [6:8]   0
  [8:10]  population (uint16)
  [10:12] 0x0024 (per-record flags/version)
```

### 3. Ping each server — UDP

The client then pings every listed server at its `IP:port`:

- `P<identity fields>` → server replies `#name=<name> //Rules: <rules> //ID:<id> //TM:<hextime> //US:<usage>`
- `Q` / `R` → server replies with the user list
- `M`+`int opcode, uint32 pubIP, int pubPort` is the master→server check-in ack
  (STUN-like; used by real game servers, not needed here)

### 4. Play — TCP game session

Newline-delimited (`\r\n`) text packets. Two families:

- **`:MIX<n>…`** control: `:MIX1<ser>` register in a scene, `:MIX0<ser>` target
  next packet at a scene, `:MIX4<ser>` target next packet at a player, `:MIX3<ser>`
  attune, `:MIX8/9` server-stored variables.
- **`:SR…`** game data: `:SR?<ser>` requests a slot; server replies
  `:SR!<serHex(8)><slotHex(2)><lastSerHex(8)>`. Other `:SR` packets are relayed to
  the target set by the preceding `:MIX` (default: all other players). Server
  greeting is `:SR@M<text>`, rules `:SR$<rules>`.

## Files

- `main.go` — the whole server (HTTP + UDP master/ping + TCP relay).

## Caveats (it's a *test* server)

- Advertises a single server (itself); trivially extended to N records.
- No anti-spoofing / serial validation / auth that the real servers have.
- Minimal SSV and scene handling — enough for broadcast-style toys.
