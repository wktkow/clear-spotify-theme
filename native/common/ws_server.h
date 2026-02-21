// ws_server.h — Minimal single-client WebSocket server for the visualizer.
// Handles the HTTP upgrade handshake, sends binary/text frames,
// and reads incoming text commands from the client.  Header-only.
// No external dependencies beyond POSIX sockets + <cstdint>.
#ifndef VIS_WS_SERVER_H
#define VIS_WS_SERVER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <functional>

// ---- Platform socket abstraction ----
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  static void sock_close(sock_t s) { closesocket(s); }
  static void sock_init() { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); }
  static void sock_cleanup() { WSACleanup(); }
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <errno.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  static void sock_close(sock_t s) { close(s); }
  static void sock_init() {}
  static void sock_cleanup() {}
#endif

// ---- Minimal SHA-1 (for Sec-WebSocket-Accept) ----
static void sha1(const uint8_t* msg, size_t len, uint8_t hash[20]) {
    uint32_t h0=0x67452301, h1=0xEFCDAB89, h2=0x98BADCFE,
             h3=0x10325476, h4=0xC3D2E1F0;

    // Pre-processing: pad to 64-byte blocks
    size_t ml = len * 8;
    size_t padded = ((len + 8) / 64 + 1) * 64;
    uint8_t* buf = (uint8_t*)calloc(padded, 1);
    memcpy(buf, msg, len);
    buf[len] = 0x80;
    for (int i = 0; i < 8; i++) buf[padded - 1 - i] = (uint8_t)(ml >> (i*8));

    for (size_t offset = 0; offset < padded; offset += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)buf[offset+i*4]<<24) | ((uint32_t)buf[offset+i*4+1]<<16) |
                   ((uint32_t)buf[offset+i*4+2]<<8) | buf[offset+i*4+3];
        }
        for (int i = 16; i < 80; i++) {
            uint32_t t = w[i-3]^w[i-8]^w[i-14]^w[i-16];
            w[i] = (t<<1)|(t>>31);
        }
        uint32_t a=h0, b=h1, c=h2, d=h3, e=h4;
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if      (i < 20) { f=(b&c)|((~b)&d);   k=0x5A827999; }
            else if (i < 40) { f=b^c^d;             k=0x6ED9EBA1; }
            else if (i < 60) { f=(b&c)|(b&d)|(c&d); k=0x8F1BBCDC; }
            else              { f=b^c^d;             k=0xCA62C1D6; }
            uint32_t tmp = ((a<<5)|(a>>27)) + f + e + k + w[i];
            e=d; d=c; c=(b<<30)|(b>>2); b=a; a=tmp;
        }
        h0+=a; h1+=b; h2+=c; h3+=d; h4+=e;
    }
    free(buf);

    uint32_t hv[5] = {h0,h1,h2,h3,h4};
    for (int i = 0; i < 5; i++) {
        hash[i*4]   = (uint8_t)(hv[i]>>24);
        hash[i*4+1] = (uint8_t)(hv[i]>>16);
        hash[i*4+2] = (uint8_t)(hv[i]>>8);
        hash[i*4+3] = (uint8_t)(hv[i]);
    }
}

// ---- Base64 encode ----
static std::string base64Encode(const uint8_t* data, size_t len) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = ((uint32_t)data[i]) << 16;
        if (i+1 < len) n |= ((uint32_t)data[i+1]) << 8;
        if (i+2 < len) n |= data[i+2];
        out += t[(n>>18)&63];
        out += t[(n>>12)&63];
        out += (i+1 < len) ? t[(n>>6)&63] : '=';
        out += (i+2 < len) ? t[n&63] : '=';
    }
    return out;
}

// ---- WebSocket server ----
class WsServer {
public:
    WsServer() : listenSock(SOCK_INVALID), clientSock(SOCK_INVALID) {}
    ~WsServer() { stop(); }

    // Optional callback for text messages from the client.
    // Set before calling poll().
    std::function<void(const std::string&)> onText;

    bool start(int port) {
        sock_init();
        listenSock = socket(AF_INET, SOCK_STREAM, 0);
        if (listenSock == SOCK_INVALID) return false;

        int opt = 1;
        setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            fprintf(stderr, "[ws] bind failed on port %d\n", port);
            sock_close(listenSock);
            listenSock = SOCK_INVALID;
            return false;
        }
        listen(listenSock, 1);

        // Non-blocking listen socket so we can poll
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(listenSock, FIONBIO, &mode);
#else
        fcntl(listenSock, F_SETFL, O_NONBLOCK);
#endif
        fprintf(stderr, "[ws] listening on 127.0.0.1:%d\n", port);
        return true;
    }

    void stop() {
        if (clientSock != SOCK_INVALID) { sock_close(clientSock); clientSock = SOCK_INVALID; }
        if (listenSock != SOCK_INVALID) { sock_close(listenSock); listenSock = SOCK_INVALID; }
        sock_cleanup();
    }

    // Call each frame: accepts new client if none connected,
    // performs handshake if needed.  Also drains any incoming client
    // data (close/pong frames) so the TCP receive buffer doesn't fill.
    void poll() {
        if (clientSock != SOCK_INVALID) {
            drainClient();
            return;
        }
        if (listenSock == SOCK_INVALID) return;

        struct sockaddr_in ca{};
        socklen_t cl = sizeof(ca);
        sock_t s = accept(listenSock, (struct sockaddr*)&ca, &cl);
        if (s == SOCK_INVALID) return;

        // Client socket must be BLOCKING for reliable WebSocket framing.
        // (listen socket is non-blocking for polling, but accepted sockets
        //  inherit that — partial sends would corrupt the WS stream.)
#ifdef _WIN32
        { u_long mode = 0; ioctlsocket(s, FIONBIO, &mode); }
#else
        { int flags = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, flags & ~O_NONBLOCK); }
#endif
        // Disable Nagle so the 4-byte header and 280-byte payload sent in
        // two send() calls go out immediately without coalescing delay.
        { int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one)); }

        // Read HTTP upgrade request
        char buf[4096];
        int n = recv(s, buf, sizeof(buf)-1, 0);
        if (n <= 0) { sock_close(s); return; }
        buf[n] = '\0';

        // Extract Sec-WebSocket-Key
        const char* keyHdr = strstr(buf, "Sec-WebSocket-Key: ");
        if (!keyHdr) { sock_close(s); return; }
        keyHdr += 19;
        const char* keyEnd = strstr(keyHdr, "\r\n");
        if (!keyEnd) { sock_close(s); return; }
        std::string key(keyHdr, keyEnd - keyHdr);

        // Compute accept value
        std::string concat = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint8_t hash[20];
        sha1((const uint8_t*)concat.c_str(), concat.size(), hash);
        std::string accept = base64Encode(hash, 20);

        // Send 101 response
        std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
        send(s, resp.c_str(), (int)resp.size(), 0);

        clientSock = s;
        fprintf(stderr, "[ws] client connected\n");
    }

    // Send a binary WebSocket frame.  Returns false if send failed
    // (client disconnected).
    bool sendBinary(const void* data, size_t len) {
        return sendFrame(0x82, data, len);
    }

    // Send a text WebSocket frame.
    bool sendText(const std::string& msg) {
        return sendFrame(0x81, msg.data(), msg.size());
    }

    bool hasClient() const { return clientSock != SOCK_INVALID; }

private:
    // Generic frame sender (opcode 0x81 = text, 0x82 = binary).
    bool sendFrame(uint8_t opcode, const void* data, size_t len) {
        if (clientSock == SOCK_INVALID) return false;

        uint8_t hdr[10];
        int hdrLen = 0;
        hdr[0] = opcode; // FIN + opcode
        if (len < 126) {
            hdr[1] = (uint8_t)len;
            hdrLen = 2;
        } else if (len < 65536) {
            hdr[1] = 126;
            hdr[2] = (uint8_t)(len >> 8);
            hdr[3] = (uint8_t)(len);
            hdrLen = 4;
        } else {
            // Payloads > 64K not needed for 24 floats
            hdr[1] = 127;
            hdrLen = 10;
            memset(hdr+2, 0, 8);
            hdr[6] = (uint8_t)(len >> 24);
            hdr[7] = (uint8_t)(len >> 16);
            hdr[8] = (uint8_t)(len >> 8);
            hdr[9] = (uint8_t)(len);
        }

        // Send header + payload via sendAll to handle partial sends.
        if (!sendAll((const char*)hdr, hdrLen)) { dropClient(); return false; }
        if (!sendAll((const char*)data, (int)len)) { dropClient(); return false; }

        return true;
    }
    // Send all bytes, retrying on partial sends.
    bool sendAll(const char* buf, int len) {
        int flags = 0;
#ifdef __linux__
        flags = MSG_NOSIGNAL;
#endif
        int off = 0;
        while (off < len) {
            int n = send(clientSock, buf + off, len - off, flags);
            if (n <= 0) return false;
            off += n;
        }
        return true;
    }

    // Non-blocking read + parse of incoming WebSocket frames.
    // Handles text messages (dispatched to onText callback), close, pong.
    // Detects client disconnect so we can accept a new connection.
    void drainClient() {
        if (clientSock == SOCK_INVALID) return;

        // Peek to see if there's data without blocking.
        uint8_t peek;
#ifdef _WIN32
        u_long avail = 0;
        ioctlsocket(clientSock, FIONREAD, &avail);
        if (avail == 0) return;
#else
        int n = recv(clientSock, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
        if (n == 0) { dropClient(); return; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            dropClient(); return;
        }
#endif

        // There's data — read the frame header (2 bytes minimum).
        uint8_t hdr[2];
        if (!recvAll(hdr, 2)) { dropClient(); return; }

        uint8_t opcode = hdr[0] & 0x0F;
        bool masked = (hdr[1] & 0x80) != 0;
        uint64_t payLen = hdr[1] & 0x7F;

        if (payLen == 126) {
            uint8_t ext[2];
            if (!recvAll(ext, 2)) { dropClient(); return; }
            payLen = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (payLen == 127) {
            uint8_t ext[8];
            if (!recvAll(ext, 8)) { dropClient(); return; }
            payLen = 0;
            for (int i = 0; i < 8; i++) payLen = (payLen << 8) | ext[i];
        }

        uint8_t mask[4] = {};
        if (masked) {
            if (!recvAll(mask, 4)) { dropClient(); return; }
        }

        // Read payload (cap at 4 KB — we never expect large messages)
        if (payLen > 4096) {
            // Nonsensical — drop connection
            dropClient(); return;
        }
        std::string payload((size_t)payLen, '\0');
        if (payLen > 0) {
            if (!recvAll((uint8_t*)&payload[0], (int)payLen)) { dropClient(); return; }
            if (masked) {
                for (size_t i = 0; i < payLen; i++)
                    payload[i] ^= mask[i % 4];
            }
        }

        if (opcode == 0x08) {
            // Close frame — send close back and drop
            uint8_t close[4] = {0x88, 0x00};
            sendAll((const char*)close, 2);
            dropClient();
        } else if (opcode == 0x01) {
            // Text frame — dispatch to callback
            if (onText) onText(payload);
        }
        // Pong (0x0A) and other frames are silently consumed.
    }

    // Blocking recv of exactly `len` bytes.
    bool recvAll(uint8_t* buf, int len) {
        int off = 0;
        while (off < len) {
            int n = recv(clientSock, (char*)buf + off, len - off, 0);
            if (n <= 0) return false;
            off += n;
        }
        return true;
    }

    void dropClient() {
        fprintf(stderr, "[ws] client disconnected\n");
        sock_close(clientSock);
        clientSock = SOCK_INVALID;
    }

    sock_t listenSock;
    sock_t clientSock;
};

#endif // VIS_WS_SERVER_H
