// mixserver — an all-in-one Synthetic Reality MIX test server.
//
// One process provides everything Arcadia needs for "Any Public MIX Game Server":
//   - HTTP on :80 serving synreal.ini (host-aware), pointing each game's master
//     at this machine.
//   - A per-game UDP listener (ports 20999-23999) that answers BOTH the client's
//     master server-list query (binary 0x0202 list) and the client's P/Q/R server
//     pings (server info / user list).
//   - A per-game TCP listener (same port) implementing the line-oriented MIX game
//     relay: slot assignment (:SR?/:SR!), scene/player routing (:MIX0/1/3/4), and
//     broadcast of :SR game packets to the other connected clients.
//
// It is intentionally stateless on the master side (no registrations) and hosts
// the servers itself, so two Arcadia clients can meet in a toy with no mix.exe or
// ReMix. Protocol recovered from SRNet.dll (client side) and ReMix (server side).
package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"strings"
	"sync"
	"time"
)

type Game struct {
	Tag  string // synreal.ini section + SRNet game tag
	Port int    // master UDP + game TCP port
	Name string // server name shown in the browser
}

var games = []Game{
	{"WoS", 23999, "Test WoS"},
	{"W97", 22999, "Test Warpath"},
	{"TOY", 21999, "Test Arcadia"},
	{"RC", 20999, "Test RealmChat"},
}

// verbose logs every inbound packet; invaluable while tuning the wire format.
const verbose = true

// greeting, if non-empty, is sent as a :SR@M admin message when a client joins.
// Empty by default — the client shows it in a pop-up dialog you'd have to
// dismiss on every join.
const greeting = ""

func main() {
	log.SetFlags(log.Ltime | log.Lmicroseconds)
	go serveHTTP()
	for _, g := range games {
		g := g
		// One relay per game, shared so the UDP master can report the live
		// connected-client count as the server's population.
		r := &relay{clients: map[*client]bool{}, nextSer: 0x1000}
		go serveUDP(g, r)
		go serveTCP(g, r)
	}
	log.Printf("mixserver up: HTTP :80, master/game ports %v", func() []int {
		p := []int{}
		for _, g := range games {
			p = append(p, g.Port)
		}
		return p
	}())
	select {}
}

// ---------------------------------------------------------------------------
// HTTP: serve synreal.ini. SRNet does a raw-socket GET and its own HTTP parse,
// so reply HTTP/1.0 with explicit Content-Length and Connection: close.
// ---------------------------------------------------------------------------

func serveHTTP() {
	ln, err := net.Listen("tcp", ":80")
	if err != nil {
		log.Fatalf("HTTP listen :80 failed: %v (port 80 in use? stop the other service or free it)", err)
	}
	log.Printf("HTTP serving synreal.ini on :80")
	for {
		c, err := ln.Accept()
		if err != nil {
			continue
		}
		go handleHTTP(c)
	}
}

func handleHTTP(c net.Conn) {
	defer c.Close()
	c.SetDeadline(time.Now().Add(10 * time.Second))
	br := bufio.NewReader(c)
	reqLine, _ := br.ReadString('\n')
	host := "localhost"
	for {
		line, err := br.ReadString('\n')
		if err != nil {
			break
		}
		if line == "\r\n" || line == "\n" {
			break
		}
		if strings.HasPrefix(strings.ToLower(line), "host:") {
			h := strings.TrimSpace(line[len("host:"):])
			if i := strings.IndexByte(h, ':'); i >= 0 {
				h = h[:i]
			}
			if h != "" {
				host = h
			}
		}
	}
	body := synrealINI(host)
	fmt.Fprintf(c, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", len(body), body)
	if verbose {
		log.Printf("[http] %s host=%s -> served synreal.ini (%d bytes)", strings.TrimSpace(reqLine), host, len(body))
	}
}

func synrealINI(host string) string {
	// SRNet parses the master address with inet_addr, which does NOT resolve
	// hostnames ("localhost" -> INADDR_NONE and no query is ever sent). Emit a
	// dotted IPv4 instead.
	ip := toDottedIPv4(host)
	var b strings.Builder
	for _, g := range games {
		fmt.Fprintf(&b, "[%s]\r\nmaster=%s:%d\r\n\r\n", g.Tag, ip, g.Port)
	}
	return b.String()
}

// toDottedIPv4 returns host as a dotted IPv4 string; localhost/empty -> 127.0.0.1.
func toDottedIPv4(host string) string {
	if host == "" || host == "localhost" {
		return "127.0.0.1"
	}
	if ip := net.ParseIP(host); ip != nil {
		if v4 := ip.To4(); v4 != nil {
			return v4.String()
		}
		return "127.0.0.1"
	}
	if ips, err := net.LookupIP(host); err == nil {
		for _, ip := range ips {
			if v4 := ip.To4(); v4 != nil {
				return v4.String()
			}
		}
	}
	return "127.0.0.1"
}

// ---------------------------------------------------------------------------
// UDP: master server-list query + P/Q/R server pings, on the game's port.
// ---------------------------------------------------------------------------

func serveUDP(g Game, r *relay) {
	conn, err := net.ListenUDP("udp", &net.UDPAddr{Port: g.Port})
	if err != nil {
		log.Printf("[%s] UDP listen %d failed: %v", g.Tag, g.Port, err)
		return
	}
	buf := make([]byte, 4096)
	for {
		n, raddr, err := conn.ReadFromUDP(buf)
		if err != nil {
			continue
		}
		pkt := make([]byte, n)
		copy(pkt, buf[:n])
		if verbose {
			log.Printf("[%s udp] %d bytes from %s: %q", g.Tag, n, raddr, printable(pkt, 200))
		}
		var resp []byte
		if n > 0 && (pkt[0] == 'P' || pkt[0] == 'Q' || pkt[0] == 'R' || pkt[0] == 'G') {
			resp = pingResponse(g, pkt)
		} else {
			// Advertise ourselves at the IP that routes to THIS client: loopback
			// for local clients, our LAN IP for remote ones. A hardcoded
			// 127.0.0.1 would tell a remote client to ping its own loopback.
			resp = masterList(g, localIPFor(raddr), r.count())
		}
		if resp != nil {
			conn.WriteToUDP(resp, raddr)
			if verbose {
				log.Printf("[%s udp] -> %d bytes to %s: hex:% x", g.Tag, len(resp), raddr, clip(resp, 40))
			}
		}
	}
}

// masterList builds the master's server-list response, exactly matching the
// wire format captured from the live community master:
//
//   Header (12 bytes):
//     [0:2]  magic   0x0202
//     [2:4]  version (uint16, cosmetic "ver %X")
//     [4:6]  count   (uint16, number of records)
//     [6:8]  recSize (uint16, = 0x0C; records start here)
//     [8:12] master dword (cosmetic)
//   Record (12 bytes each, starting at offset 0x0C):
//     [0:4]  IP   (network byte order, for inet_ntoa)
//     [4:6]  port (uint16, little-endian)
//     [6:8]  0
//     [8:10] population (uint16)
//     [10:12] 0x0024 (per-record flags/version constant)
func masterList(g Game, ip net.IP, pop int) []byte {
	const rec = 0x0C
	count := 1
	if pop < 0 {
		pop = 0
	} else if pop > 0xffff {
		pop = 0xffff
	}
	buf := make([]byte, rec+count*rec)
	binary.LittleEndian.PutUint16(buf[0:], 0x0202)        // magic
	binary.LittleEndian.PutUint16(buf[2:], 0xa124)        // version
	binary.LittleEndian.PutUint16(buf[4:], uint16(count)) // count
	binary.LittleEndian.PutUint16(buf[6:], rec)           // record size / records-start offset
	// buf[8:12] master dword left 0 (cosmetic)
	r := buf[rec:]        // record 0
	copy(r[0:4], ip.To4())                               // IP (network byte order)
	binary.LittleEndian.PutUint16(r[4:], uint16(g.Port)) // port
	binary.LittleEndian.PutUint16(r[8:], uint16(pop))    // population = connected clients
	binary.LittleEndian.PutUint16(r[10:], 0x0024)        // flags/version
	return buf
}

// localIPFor returns the local IPv4 that the OS would use to reach raddr — i.e.
// the address this client should use to reach us. Falls back to loopback.
func localIPFor(raddr *net.UDPAddr) net.IP {
	if c, err := net.DialUDP("udp", nil, raddr); err == nil {
		defer c.Close()
		if la, ok := c.LocalAddr().(*net.UDPAddr); ok {
			if v4 := la.IP.To4(); v4 != nil {
				return v4
			}
		}
	}
	return net.IPv4(127, 0, 0, 1).To4()
}

// pingResponse answers a client's direct ping to a listed server.
// 'P' -> server info line; 'Q'/'R' -> user list (empty here).
func pingResponse(g Game, req []byte) []byte {
	switch req[0] {
	case 'P':
		// Mirrors ReMix: #name=<name> //Rules: <rules> //ID:<id> //TM:<hextime> //US:<usage>
		resp := fmt.Sprintf("#name=%s //Rules:  //ID:%s //TM:%08X //US:0/0/0 //mixserver",
			g.Name, "00000001", uint32(time.Now().Unix()))
		return []byte(resp)
	case 'Q', 'R':
		return []byte{} // no users online
	case 'G':
		return nil
	}
	return nil
}

// ---------------------------------------------------------------------------
// TCP: the MIX game relay.
// ---------------------------------------------------------------------------

type pktTarget int

const (
	targetAll pktTarget = iota
	targetPlayer
	targetScene
)

type client struct {
	conn      net.Conn
	sernum    int
	slot      int
	sceneHost int
	// per-next-:SR-packet target, set by preceding :MIX packets
	tType  pktTarget
	tSer   int
	tScene int
}

type relay struct {
	mu      sync.Mutex
	clients map[*client]bool
	nextSer int
}

// count returns the number of currently connected clients.
func (r *relay) count() int {
	r.mu.Lock()
	defer r.mu.Unlock()
	return len(r.clients)
}

func serveTCP(g Game, r *relay) {
	ln, err := net.Listen("tcp", fmt.Sprintf(":%d", g.Port))
	if err != nil {
		log.Printf("[%s] TCP listen %d failed: %v", g.Tag, g.Port, err)
		return
	}
	log.Printf("[%s] TCP game relay on :%d", g.Tag, g.Port)
	for {
		c, err := ln.Accept()
		if err != nil {
			continue
		}
		go r.handle(g, c)
	}
}

func (r *relay) handle(g Game, conn net.Conn) {
	cl := &client{conn: conn, tType: targetAll}
	r.mu.Lock()
	r.nextSer++
	cl.sernum = r.nextSer
	r.clients[cl] = true
	n := len(r.clients)
	r.mu.Unlock()
	log.Printf("[%s tcp] connect %s (assigned sernum %08X, %d online)", g.Tag, conn.RemoteAddr(), cl.sernum, n)

	// Optional admin greeting — off by default; the client shows it as a pop-up
	// dialog that has to be dismissed on every join.
	if greeting != "" {
		writePkt(conn, ":SR@M"+greeting)
	}

	defer func() {
		r.mu.Lock()
		delete(r.clients, cl)
		r.mu.Unlock()
		conn.Close()
		log.Printf("[%s tcp] disconnect %s", g.Tag, conn.RemoteAddr())
	}()

	br := bufio.NewReader(conn)
	for {
		line, err := readPacket(br)
		if err != nil {
			return
		}
		if len(line) == 0 {
			continue
		}
		if verbose {
			log.Printf("[%s tcp] < %q", g.Tag, printable(line, 80))
		}
		r.dispatch(g, cl, line)
	}
}

func (r *relay) dispatch(g Game, cl *client, pkt []byte) {
	s := string(pkt)
	switch {
	case strings.HasPrefix(s, ":MIX"):
		if len(s) < 5 {
			return
		}
		op := s[4]
		arg := ""
		if len(s) > 5 {
			arg = s[5:]
		}
		ser := hex8(arg)
		switch op {
		case '0': // next :SR -> SCENE
			cl.tType = targetScene
			cl.tScene = ser
		case '1': // register sceneHost
			cl.sceneHost = ser
		case '3': // attune
			cl.sernum = ser
		case '4': // next :SR -> PLAYER
			cl.tType = targetPlayer
			cl.tSer = ser
		}
	case strings.HasPrefix(s, ":SR?"):
		// slot request: :SR?<sernum>
		ser := hex8(s[4:])
		if ser != 0 {
			cl.sernum = ser
		}
		r.mu.Lock()
		cl.slot++
		if cl.slot < 1 {
			cl.slot = 1
		}
		slot := cl.slot
		r.mu.Unlock()
		writePkt(cl.conn, fmt.Sprintf(":SR!%08X%02X%08X", cl.sernum, slot&0xff, cl.sernum))
	case strings.HasPrefix(s, ":SR"):
		// game data packet -> relay to targets, then reset target to ALL.
		r.relayPacket(g, cl, pkt)
		cl.tType = targetAll
		cl.tSer = 0
		cl.tScene = 0
	}
}

func (r *relay) relayPacket(g Game, from *client, pkt []byte) {
	out := append(append([]byte{}, pkt...), '\r', '\n')
	r.mu.Lock()
	defer r.mu.Unlock()
	for c := range r.clients {
		if c == from {
			continue
		}
		send := false
		switch from.tType {
		case targetAll:
			send = true
		case targetPlayer:
			send = from.tSer == c.sernum
		case targetScene:
			send = from.tScene == c.sernum || from.tScene == c.sceneHost
		}
		if send {
			c.conn.Write(out)
		}
	}
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// readPacket reads one newline-delimited packet (\r\n, \n, or \r), stripping the
// terminator. It returns as soon as the first terminator byte arrives — it must
// NOT peek ahead for a paired \r\n, because Peek blocks until the next byte is
// available, which would hold every packet back until the following one is sent
// (a one-packet-behind delay). A paired terminator simply yields an empty packet
// on the next call, which the dispatcher ignores.
func readPacket(br *bufio.Reader) ([]byte, error) {
	var out []byte
	for {
		b, err := br.ReadByte()
		if err != nil {
			return out, err
		}
		if b == '\n' || b == '\r' {
			return out, nil
		}
		out = append(out, b)
	}
}

func writePkt(conn net.Conn, s string) {
	conn.Write([]byte(s + "\r\n"))
}

// hex8 parses up to 8 leading hex digits.
func hex8(s string) int {
	v := 0
	for i := 0; i < len(s) && i < 8; i++ {
		c := s[i]
		var d int
		switch {
		case c >= '0' && c <= '9':
			d = int(c - '0')
		case c >= 'a' && c <= 'f':
			d = int(c-'a') + 10
		case c >= 'A' && c <= 'F':
			d = int(c-'A') + 10
		default:
			return v
		}
		v = v*16 + d
	}
	return v
}

func printable(b []byte, max int) string {
	if len(b) > max {
		b = b[:max]
	}
	var sb strings.Builder
	for _, c := range b {
		if c >= 32 && c < 127 {
			sb.WriteByte(c)
		} else {
			sb.WriteByte('.')
		}
	}
	return sb.String()
}

func clip(b []byte, max int) []byte {
	if len(b) > max {
		return b[:max]
	}
	return b
}
