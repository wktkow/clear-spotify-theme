// ws_server.h — Minimal single-client WebSocket server for the visualizer.
// Handles the HTTP upgrade handshake, sends binary frames.  Header-only.
// No external dependencies beyond POSIX sockets + <cstdint>.
#ifndef VIS_WS_SERVER_H
#define VIS_WS_SERVER_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

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
        if (clientSock == SOCK_INVALID) return false;

        // WebSocket frame header (no mask, binary opcode)
        uint8_t hdr[10];
        int hdrLen = 0;
        hdr[0] = 0x82; // FIN + binary opcode
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

    bool hasClient() const { return clientSock != SOCK_INVALID; }

private:
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

    // Non-blocking drain of any data the browser sent us (close/pong frames).
    // We never parse it — just keep the TCP receive buffer from filling up.
    void drainClient() {
        if (clientSock == SOCK_INVALID) return;
        char junk[512];
#ifdef _WIN32
        u_long avail = 0;
        ioctlsocket(clientSock, FIONREAD, &avail);
        if (avail > 0) recv(clientSock, junk, sizeof(junk), 0);
#else
        // MSG_DONTWAIT = non-blocking one-shot read
        recv(clientSock, junk, sizeof(junk), MSG_DONTWAIT);
#endif
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
