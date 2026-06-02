// Signaling server for a P2P voice/video messenger.
//
// Responsibilities:
//   - Maintain a list of currently connected peers (presence).
//   - Relay WebRTC SDP and ICE messages between two named peers.
//
// Non-responsibilities:
//   - It does NOT store messages, history, or media.
//   - It does NOT see audio/video — those flow directly between peers (or
//     through coturn as a TURN relay when NAT traversal fails).
package main

import (
	"encoding/json"
	"flag"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// Envelope is the wire format for every message. `To` is empty for
// client→server control messages (hello) and server→client broadcasts
// (presence).
type Envelope struct {
	Type    string          `json:"type"`
	From    string          `json:"from,omitempty"`
	To      string          `json:"to,omitempty"`
	Payload json.RawMessage `json:"payload,omitempty"`
}

type helloPayload struct {
	UserID string `json:"user_id"`
	Token  string `json:"token"`
}

type presencePayload struct {
	Online []string `json:"online"`
}

type peer struct {
	id   string
	conn *websocket.Conn
	send chan []byte
}

type hub struct {
	mu     sync.RWMutex
	peers  map[string]*peer
	token  string // shared secret; replaced by public-key auth later
	upgr   websocket.Upgrader
	logger *log.Logger
}

func newHub(token string, logger *log.Logger) *hub {
	return &hub{
		peers:  make(map[string]*peer),
		token:  token,
		logger: logger,
		upgr: websocket.Upgrader{
			ReadBufferSize:  4096,
			WriteBufferSize: 4096,
			CheckOrigin:     func(r *http.Request) bool { return true },
		},
	}
}

func (h *hub) register(p *peer) {
	h.mu.Lock()
	if old, ok := h.peers[p.id]; ok {
		// Same user reconnected; drop the previous socket.
		close(old.send)
		_ = old.conn.Close()
	}
	h.peers[p.id] = p
	h.mu.Unlock()
	h.broadcastPresence()
	h.logger.Printf("peer connected: %s", p.id)
}

func (h *hub) unregister(p *peer) {
	h.mu.Lock()
	if cur, ok := h.peers[p.id]; ok && cur == p {
		delete(h.peers, p.id)
		close(p.send)
	}
	h.mu.Unlock()
	h.broadcastPresence()
	h.logger.Printf("peer disconnected: %s", p.id)
}

func (h *hub) broadcastPresence() {
	h.mu.RLock()
	online := make([]string, 0, len(h.peers))
	for id := range h.peers {
		online = append(online, id)
	}
	targets := make([]*peer, 0, len(h.peers))
	for _, p := range h.peers {
		targets = append(targets, p)
	}
	h.mu.RUnlock()

	payload, _ := json.Marshal(presencePayload{Online: online})
	env := Envelope{Type: "presence", Payload: payload}
	raw, _ := json.Marshal(env)
	for _, p := range targets {
		select {
		case p.send <- raw:
		default:
			// Drop if peer's outbound queue is full — they'll get the next update.
		}
	}
}

func (h *hub) relay(env Envelope) {
	h.mu.RLock()
	dst, ok := h.peers[env.To]
	h.mu.RUnlock()
	if !ok {
		h.logger.Printf("relay: peer %q not online (from=%s type=%s)", env.To, env.From, env.Type)
		return
	}
	raw, err := json.Marshal(env)
	if err != nil {
		return
	}
	select {
	case dst.send <- raw:
	default:
		h.logger.Printf("relay: dropping message to %s (queue full)", env.To)
	}
}

// readLoop runs in its own goroutine per peer and handles inbound frames.
func (h *hub) readLoop(p *peer) {
	defer func() {
		h.unregister(p)
		_ = p.conn.Close()
	}()
	p.conn.SetReadLimit(64 * 1024)
	_ = p.conn.SetReadDeadline(time.Now().Add(75 * time.Second))
	p.conn.SetPongHandler(func(string) error {
		return p.conn.SetReadDeadline(time.Now().Add(75 * time.Second))
	})
	for {
		_, data, err := p.conn.ReadMessage()
		if err != nil {
			return
		}
		var env Envelope
		if err := json.Unmarshal(data, &env); err != nil {
			h.logger.Printf("bad json from %s: %v", p.id, err)
			continue
		}
		env.From = p.id // server stamps the sender; clients can't spoof
		switch env.Type {
		case "offer", "answer", "ice", "bye":
			if env.To == "" {
				continue
			}
			h.relay(env)
		default:
			h.logger.Printf("unknown type %q from %s", env.Type, p.id)
		}
	}
}

func (h *hub) writeLoop(p *peer) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case msg, ok := <-p.send:
			if !ok {
				_ = p.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}
			_ = p.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := p.conn.WriteMessage(websocket.TextMessage, msg); err != nil {
				return
			}
		case <-ticker.C:
			_ = p.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
			if err := p.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				return
			}
		}
	}
}

// handleWS upgrades the HTTP connection and waits for the initial `hello`.
// A connection that doesn't authenticate within 10 seconds is dropped.
func (h *hub) handleWS(w http.ResponseWriter, r *http.Request) {
	conn, err := h.upgr.Upgrade(w, r, nil)
	if err != nil {
		return
	}

	_ = conn.SetReadDeadline(time.Now().Add(10 * time.Second))
	_, data, err := conn.ReadMessage()
	if err != nil {
		_ = conn.Close()
		return
	}
	var env Envelope
	if err := json.Unmarshal(data, &env); err != nil || env.Type != "hello" {
		_ = conn.Close()
		return
	}
	var hello helloPayload
	if err := json.Unmarshal(env.Payload, &hello); err != nil {
		_ = conn.Close()
		return
	}
	if hello.UserID == "" || hello.Token != h.token {
		_ = conn.WriteJSON(Envelope{Type: "error", Payload: json.RawMessage(`{"reason":"auth"}`)})
		_ = conn.Close()
		return
	}

	p := &peer{id: hello.UserID, conn: conn, send: make(chan []byte, 32)}
	h.register(p)
	go h.writeLoop(p)
	h.readLoop(p)
}

func main() {
	addr := flag.String("addr", ":8080", "listen address")
	flag.Parse()

	token := os.Getenv("SIGNAL_TOKEN")
	if token == "" {
		log.Fatal("SIGNAL_TOKEN env var must be set (shared secret for both clients)")
	}

	logger := log.New(os.Stdout, "signal ", log.LstdFlags|log.Lmicroseconds)
	h := newHub(token, logger)

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", h.handleWS)
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK)
	})

	srv := &http.Server{
		Addr:              *addr,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
	}
	logger.Printf("listening on %s", *addr)
	if err := srv.ListenAndServe(); err != nil {
		logger.Fatal(err)
	}
}
