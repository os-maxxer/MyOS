import socket
import threading
import os
import sys
import mimetypes

STATIC_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "pkg")

def handle_client(conn, addr):
    try:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = conn.recv(4096)
            if not chunk:
                return
            data += chunk
            if len(data) > 16384:
                return

        request_line = data.split(b"\r\n")[0].decode("utf-8", errors="replace")
        parts = request_line.split(" ")
        method = parts[0] if len(parts) > 0 else ""
        path = parts[1] if len(parts) > 1 else "/"

        if path == "/redirect":
            resp = (
                "HTTP/1.1 302 Found\r\n"
                "Location: http://10.0.2.2:8000/repo.json\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n"
            )
            conn.sendall(resp.encode())

        elif path == "/chunked":
            chunk_data = b"Hello, chunked world! This is a test of chunked encoding."
            resp = (
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Connection: close\r\n"
                "\r\n"
            )
            conn.sendall(resp.encode())
            conn.sendall(f"{len(chunk_data):x}\r\n".encode())
            conn.sendall(chunk_data + b"\r\n")
            conn.sendall(b"0\r\n\r\n")

        elif path == "/large":
            body = b"A" * 6000
            resp = (
                "HTTP/1.1 200 OK\r\n"
                f"Content-Length: {len(body)}\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Connection: close\r\n"
                "\r\n"
            )
            conn.sendall(resp.encode() + body)

        else:
            # Serve static files
            rel_path = path.lstrip("/")
            if not rel_path:
                rel_path = "repo.json"
            file_path = os.path.join(STATIC_DIR, rel_path)
            if os.path.isfile(file_path):
                with open(file_path, "rb") as f:
                    body = f.read()
                content_type = mimetypes.guess_type(file_path)[0] or "application/octet-stream"
                resp = (
                    "HTTP/1.1 200 OK\r\n"
                    f"Content-Length: {len(body)}\r\n"
                    f"Content-Type: {content_type}\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                )
                conn.sendall(resp.encode() + body)
            else:
                resp = (
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Length: 0\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                )
                conn.sendall(resp.encode())

    except Exception as e:
        print(f"[test-server] Error: {e}", flush=True)
    finally:
        try:
            conn.close()
        except:
            pass

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", port))
    server.listen(10)
    print(f"[test-server] Listening on port {port}, static dir: {STATIC_DIR}", flush=True)
    while True:
        conn, addr = server.accept()
        threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()

if __name__ == "__main__":
    main()
