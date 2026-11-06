// server.h — servidor HTTP + WebSocket mínimo (sin dependencias) para la pantalla de Freon Machine.
// Sirve los archivos de la carpeta ui/ y habla por WebSocket con mensajes de texto.
#pragma once
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace web {

// --- SHA-1 y base64 (solo para el saludo del WebSocket)
inline std::string sha1(const std::string& in) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = in; const uint64_t bits = static_cast<uint64_t>(in.size()) * 8;
    m += static_cast<char>(0x80); while (m.size() % 64 != 56) m += static_cast<char>(0);
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xFF);
    auto rol = [](uint32_t x, int c) { return (x << c) | (x >> (32 - c)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4])) << 24) | (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 1])) << 16) |
                                            (static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 2])) << 8) | static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 3]));
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string out; for (uint32_t x : h) for (int i = 3; i >= 0; --i) out += static_cast<char>((x >> (i * 8)) & 0xFF);
    return out;
}
inline std::string base64(const std::string& s) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; uint32_t val = 0; int bits = -6;
    for (unsigned char c : s) { val = ((val << 8) + c) & 0xFFFFFFu; bits += 8; while (bits >= 0) { o += T[(val >> bits) & 0x3F]; bits -= 6; } }
    if (bits > -6) o += T[((val << 8) >> (bits + 8)) & 0x3F];
    while (o.size() % 4) o += '=';
    return o;
}

class Server {
public:
    std::function<void(int client, const std::string&)> onMessage;
    std::function<void(int client)> onOpen;
    std::string root = "ui";

    bool listenOn(const std::string& host, int port) {
        lfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (lfd_ < 0) return false;
        int one = 1; setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(static_cast<uint16_t>(port));
        a.sin_addr.s_addr = host == "0.0.0.0" ? INADDR_ANY : inet_addr(host.c_str());
        if (bind(lfd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 || listen(lfd_, 8) < 0) { ::close(lfd_); lfd_ = -1; return false; }
        return true;
    }
    // Atiende la red durante como mucho «ms» milisegundos. Nunca se bloquea escribiendo:
    // cada cliente tiene su cola de salida y, si deja de leer (un móvil dormido), se le desconecta.
    static constexpr size_t kMaxIn = 64 * 1024, kMaxHeader = 16 * 1024, kMaxOut = 512 * 1024;
    void poll(int ms) {
        std::vector<pollfd> p; p.push_back({lfd_, POLLIN, 0});
        for (auto& c : cl_) p.push_back({c.fd, static_cast<short>(POLLIN | (c.out.size() > c.outOff ? POLLOUT : 0)), 0});
        if (::poll(p.data(), static_cast<nfds_t>(p.size()), ms) <= 0) return;
        if (p[0].revents & POLLIN) {
            const int fd = accept(lfd_, nullptr, nullptr);
            if (fd >= 0) {
#ifdef SO_NOSIGPIPE
                int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
                if (cl_.size() >= 16) ::close(fd); else cl_.push_back({fd, false, {}, ++ids_, {}, 0, false});
            }
        }
        for (size_t i = 1; i < p.size(); ++i) {
            Client* c = find(p[i].fd); if (!c) continue;
            if (p[i].revents & POLLOUT) flush(*c);
            if (c->fd < 0 || !(p[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[8192]; const ssize_t n = ::recv(c->fd, buf, sizeof buf, 0);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) { drop(c->fd); continue; }
            if (n < 0) continue;
            c->in.append(buf, static_cast<size_t>(n));
            if (c->in.size() > (c->ws ? kMaxIn + 16 : kMaxHeader)) { drop(c->fd); continue; }   // mensaje o cabecera demasiado grandes
            if (!c->ws) handleHttp(*c); else handleFrames(*c);
        }
        for (auto& c : cl_) if (c.fd >= 0 && c.closeAfter && c.outOff >= c.out.size()) drop(c.fd);
        cl_.erase(std::remove_if(cl_.begin(), cl_.end(), [](const Client& c) { return c.fd < 0; }), cl_.end());
    }
    void send(int client, const std::string& msg) { for (auto& c : cl_) if (c.ws && c.id == client) frame(c, msg); }
    void broadcast(const std::string& msg, int except = -1) { for (auto& c : cl_) if (c.ws && c.id != except) frame(c, msg); }
    bool anyClient() const { for (auto& c : cl_) if (c.ws && c.fd >= 0) return true; return false; }

private:
    struct Client { int fd; bool ws; std::string in; int id; std::string out; size_t outOff; bool closeAfter; };
    Client* find(int fd) { for (auto& c : cl_) if (c.fd == fd && fd >= 0) return &c; return nullptr; }
    void drop(int fd) { for (auto& c : cl_) if (c.fd == fd && fd >= 0) { ::close(c.fd); c.fd = -1; } }
    void flush(Client& c) {
        while (c.fd >= 0 && c.outOff < c.out.size()) {
#ifdef MSG_NOSIGNAL
            const ssize_t n = ::send(c.fd, c.out.data() + c.outOff, c.out.size() - c.outOff, MSG_NOSIGNAL);
#else
            const ssize_t n = ::send(c.fd, c.out.data() + c.outOff, c.out.size() - c.outOff, 0);
#endif
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n <= 0) { drop(c.fd); return; }
            c.outOff += static_cast<size_t>(n);
        }
        if (c.outOff >= c.out.size()) { c.out.clear(); c.outOff = 0; }
        else if (c.outOff > 65536) { c.out.erase(0, c.outOff); c.outOff = 0; }
    }
    void rawSend(Client& c, const std::string& s) {
        if (c.fd < 0) return;
        if (c.out.size() - c.outOff + s.size() > kMaxOut) { drop(c.fd); return; }   // no lee: fuera
        c.out += s; flush(c);
    }
    void frame(Client& c, const std::string& msg) {
        if (c.fd < 0) return;
        std::string h; h += static_cast<char>(0x81);
        if (msg.size() < 126) h += static_cast<char>(msg.size());
        else if (msg.size() < 65536) { h += static_cast<char>(126); h += static_cast<char>((msg.size() >> 8) & 0xFF); h += static_cast<char>(msg.size() & 0xFF); }
        else { h += static_cast<char>(127); for (int i = 7; i >= 0; --i) h += static_cast<char>((static_cast<uint64_t>(msg.size()) >> (i * 8)) & 0xFF); }
        rawSend(c, h + msg);
    }
    static std::string header(const std::string& req, const std::string& name) {
        std::istringstream is(req); std::string line;
        while (std::getline(is, line)) {
            const auto colon = line.find(':'); if (colon == std::string::npos) continue;
            std::string k = line.substr(0, colon); for (char& ch : k) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (k == name) { std::string v = line.substr(colon + 1); while (!v.empty() && (v.front() == ' ')) v.erase(0, 1); while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back(); return v; }
        }
        return "";
    }
    void handleHttp(Client& c) {
        const auto end = c.in.find("\r\n\r\n"); if (end == std::string::npos) return;
        const std::string req = c.in.substr(0, end); c.in.erase(0, end + 4);
        const std::string key = header(req, "sec-websocket-key");
        if (!key.empty()) {
            // el WebSocket solo se acepta desde una página de esta misma Freon Machine
            const std::string origin = header(req, "origin"), host = header(req, "host");
            if (!origin.empty() && origin != "http://" + host && origin != "https://" + host) {
                rawSend(c, "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); c.closeAfter = true; return;
            }
            const std::string acc = base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
            rawSend(c, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n");
            c.ws = true; if (onOpen) onOpen(c.id); return;
        }
        std::string path = req.substr(req.find(' ') + 1); path = path.substr(0, path.find(' ')); path = path.substr(0, path.find('?'));
        if (path == "/") path = "/index.html";
        if (path.find("..") != std::string::npos) path = "/404";
        std::ifstream f(root + path, std::ios::binary);
        if (!f) { rawSend(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); c.closeAfter = true; return; }
        std::stringstream ss; ss << f.rdbuf(); const std::string body = ss.str();
        const std::string ext = path.substr(path.rfind('.') + 1);
        const std::string type = ext == "html" ? "text/html; charset=utf-8" : ext == "js" ? "text/javascript" : ext == "css" ? "text/css" : ext == "svg" ? "image/svg+xml" : ext == "png" ? "image/png" : "application/octet-stream";
        const std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        c.out += resp; flush(c); c.closeAfter = true;   // la página puede ser mayor que el límite de cola: se encola sin él
    }
    void handleFrames(Client& c) {
        for (;;) {
            if (c.in.size() < 2) return;
            const unsigned char b0 = static_cast<unsigned char>(c.in[0]), b1 = static_cast<unsigned char>(c.in[1]);
            const int op = b0 & 0x0F; const bool masked = b1 & 0x80; uint64_t len = b1 & 0x7F; size_t pos = 2;
            if (len == 126) { if (c.in.size() < 4) return; len = (static_cast<uint64_t>(static_cast<unsigned char>(c.in[2])) << 8) | static_cast<unsigned char>(c.in[3]); pos = 4; }
            else if (len == 127) { if (c.in.size() < 10) return; len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | static_cast<unsigned char>(c.in[2 + i]); pos = 10; }
            if (len > kMaxIn) { drop(c.fd); return; }   // mensaje demasiado grande
            const size_t need = pos + (masked ? 4 : 0) + static_cast<size_t>(len);
            if (c.in.size() < need) return;
            std::string payload = c.in.substr(pos + (masked ? 4 : 0), static_cast<size_t>(len));
            if (masked) for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ c.in[pos + (i & 3)]);
            c.in.erase(0, need);
            if (op == 0x8) { drop(c.fd); return; }
            if (op == 0x9) { if (payload.size() > 125) payload.resize(125); std::string h; h += static_cast<char>(0x8A); h += static_cast<char>(payload.size()); rawSend(c, h + payload); continue; }
            if (op == 0x1 && onMessage) onMessage(c.id, payload);
        }
    }
    int lfd_ = -1, ids_ = 0;
    std::vector<Client> cl_;
};

}  // namespace web
