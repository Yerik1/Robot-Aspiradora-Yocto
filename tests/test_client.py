#!/usr/bin/env python3
"""
test_client.py - Automated End-to-End Test Suite
Tests:
1. HTTP static file serving (index.html, style.css, app.js, crypto.js, rpc.js)
2. WebSocket handshake upgrade at /websocket
3. Client-side AES-256 encrypted login with default credentials (admin / admin123)
4. Client-side AES-256 encrypted registration for a new user, and subsequent login
5. RPC calls: robot.set_mode, robot.move, robot.vacuum, audio.play, audio.notification
6. Route Map retrieval (map.get) and verification of grid structure
7. Streaming telemetry reception (robot.telemetry, robot.map_update)
"""

import socket
import struct
import os
import json
import base64
import time
import urllib.request
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import padding

TRANSPORT_KEY = b"robot-vacuum-secure-transit-key!"

def encrypt_payload(username, password):
    iv = os.urandom(16)
    payload = json.dumps({
        "username": username,
        "password": password,
        "timestamp": int(time.time() * 1000)
    }).encode("utf-8")

    padder = padding.PKCS7(128).padder()
    padded = padder.update(payload) + padder.finalize()

    cipher = Cipher(algorithms.AES(TRANSPORT_KEY), modes.CBC(iv))
    encryptor = cipher.encryptor()
    ct = encryptor.update(padded) + encryptor.finalize()

    return base64.b64encode(ct).decode(), base64.b64encode(iv).decode()

class SimpleWebSocketClient:
    def __init__(self, host="127.0.0.1", port=8080):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(4.0)

    def connect(self):
        self.sock.connect((self.host, self.port))
        key = base64.b64encode(os.urandom(16)).decode()
        handshake = (
            f"GET /websocket HTTP/1.1\r\n"
            f"Host: {self.host}:{self.port}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n"
        )
        self.sock.sendall(handshake.encode())
        resp = self.sock.recv(1024).decode()
        if "101 Switching Protocols" not in resp:
            raise RuntimeError(f"WebSocket upgrade failed:\n{resp}")

    def send_frame(self, text):
        data = text.encode("utf-8")
        mask = os.urandom(4)
        length = len(data)

        header = bytearray([0x81])  # FIN + Text
        if length < 126:
            header.append(0x80 | length)
        elif length < 65536:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", length))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", length))

        header.extend(mask)
        masked = bytearray(b ^ mask[i % 4] for i, b in enumerate(data))
        self.sock.sendall(header + masked)

    def recv_frame(self):
        b1, b2 = self.sock.recv(2)
        length = b2 & 0x7F
        if length == 126:
            length = struct.unpack("!H", self.sock.recv(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.sock.recv(8))[0]

        payload = b""
        while len(payload) < length:
            chunk = self.sock.recv(length - len(payload))
            if not chunk:
                break
            payload += chunk

        return payload.decode("utf-8")

    def call_rpc(self, req_id, method, params):
        msg = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "params": params
        }
        self.send_frame(json.dumps(msg))

        # Read frames until matching response id is found (skipping push notifications)
        start = time.time()
        while time.time() - start < 4.0:
            frame_str = self.recv_frame()
            parsed = json.loads(frame_str)
            if parsed.get("id") == req_id:
                return parsed
        raise TimeoutError(f"RPC response for id {req_id} timed out")

    def close(self):
        self.sock.close()

def run_tests():
    print("==================================================")
    print("   Running Robot Server & Client Test Suite       ")
    print("==================================================")

    # 1. Test HTTP Static File Serving
    print("\n[TEST 1] Testing HTTP static file delivery...")
    try:
        req = urllib.request.urlopen("http://127.0.0.1:8080/", timeout=3)
        html = req.read().decode("utf-8")
        assert "ROBOT ASPIRADORA AUTÓNOMO" in html
        assert "map-canvas" in html
        print("  ✓ Successfully retrieved index.html from server")
    except Exception as e:
        print(f"  ✗ HTTP check failed: {e}")
        return False

    # 2. Test WebSocket Handshake
    print("\n[TEST 2] Testing WebSocket connection and handshake...")
    client = SimpleWebSocketClient("127.0.0.1", 8080)
    try:
        client.connect()
        print("  ✓ WebSocket connected and upgraded at /websocket")
    except Exception as e:
        print(f"  ✗ WebSocket handshake failed: {e}")
        return False

    # 3. Test Encrypted Authentication (Default Admin)
    print("\n[TEST 3] Testing client-side AES-256 encrypted login (admin / admin123)...")
    try:
        enc_data, iv = encrypt_payload("admin", "admin123")
        res = client.call_rpc(1, "auth.login", {"encrypted": enc_data, "iv": iv})
        assert "result" in res, f"Login failed: {res}"
        token = res["result"]["token"]
        print(f"  ✓ Login successful! Received token: {token[:12]}...")
    except Exception as e:
        print(f"  ✗ Encrypted login failed: {e}")
        client.close()
        return False

    # 4. Test Encrypted Registration of a New User
    print("\n[TEST 4] Testing encrypted registration of a new user...")
    test_user = f"user_{int(time.time()) % 10000}"
    test_pass = "secretPass99"
    try:
        enc_reg, reg_iv = encrypt_payload(test_user, test_pass)
        reg_res = client.call_rpc(2, "auth.register", {"encrypted": enc_reg, "iv": reg_iv})
        assert "result" in reg_res and reg_res["result"].get("success") is True
        print(f"  ✓ User '{test_user}' registered successfully on server")

        # Now log in with newly registered user
        enc_login, login_iv = encrypt_payload(test_user, test_pass)
        log_res = client.call_rpc(3, "auth.login", {"encrypted": enc_login, "iv": login_iv})
        assert "result" in log_res
        print(f"  ✓ Successfully logged in with new user '{test_user}'")
    except Exception as e:
        print(f"  ✗ Encrypted registration failed: {e}")
        client.close()
        return False

    # 5. Test Mode Switching
    print("\n[TEST 5] Testing operational mode switching...")
    try:
        res = client.call_rpc(4, "robot.set_mode", {"mode": "autonomous"})
        assert res["result"]["mode"] == "autonomous"
        print("  ✓ Switched robot to AUTONOMOUS mode")

        res = client.call_rpc(5, "robot.set_mode", {"mode": "manual"})
        assert res["result"]["mode"] == "manual"
        print("  ✓ Switched robot back to MANUAL mode")
    except Exception as e:
        print(f"  ✗ Mode switching failed: {e}")
        client.close()
        return False

    # 6. Test Manual Traction & Suction Controls
    print("\n[TEST 6] Testing manual movement & suction controls...")
    try:
        res = client.call_rpc(6, "robot.move", {"direction": "forward", "speed": 75, "radius": 50})
        assert res["result"]["success"] is True
        print("  ✓ robot.move (forward @ 75%) succeeded")

        res = client.call_rpc(7, "robot.vacuum", {"enabled": True})
        assert res["result"]["vacuum"] is True
        print("  ✓ robot.vacuum (suction ON) succeeded")

        res = client.call_rpc(8, "robot.stop", {})
        assert res["result"]["success"] is True
        print("  ✓ robot.stop succeeded")
    except Exception as e:
        print(f"  ✗ Movement controls failed: {e}")
        client.close()
        return False

    # 7. Test Audio Controls & Notifications
    print("\n[TEST 7] Testing audio playback & notification sounds...")
    try:
        res = client.call_rpc(9, "audio.play", {"file": "ambient_chill.mp3"})
        assert res["result"]["success"] is True
        print("  ✓ audio.play succeeded")

        res = client.call_rpc(10, "audio.volume", {"volume": 85})
        assert res["result"]["volume"] == 85
        print("  ✓ audio.volume (85%) succeeded")

        # Notification sound event 2 (Obstacle detected)
        res = client.call_rpc(11, "audio.notification", {"event": 2})
        assert res["result"]["success"] is True
        print("  ✓ audio.notification (Event 2 - Obstacle) succeeded")
    except Exception as e:
        print(f"  ✗ Audio controls failed: {e}")
        client.close()
        return False

    # 8. Test 2D Route Map Retrieval
    print("\n[TEST 8] Testing route map retrieval (map.get)...")
    try:
        res = client.call_rpc(12, "map.get", {})
        grid_data = res["result"]
        assert grid_data["width"] == 25
        assert grid_data["height"] == 25
        assert len(grid_data["grid"]) == 625
        print(f"  ✓ 2D Route Map retrieved: {grid_data['width']}x{grid_data['height']} grid, robot at ({grid_data['robot']['x']:.1f}, {grid_data['robot']['y']:.1f})")
    except Exception as e:
        print(f"  ✗ Map retrieval failed: {e}")
        client.close()
        return False

    # 9. Test Telemetry Push Stream
    print("\n[TEST 9] Testing incoming telemetry push notifications...")
    try:
        received_telemetry = False
        start = time.time()
        while time.time() - start < 3.0:
            frame = client.recv_frame()
            parsed = json.loads(frame)
            if parsed.get("method") == "robot.telemetry":
                params = parsed["params"]
                print(f"  ✓ Received telemetry push: mode={params['mode']}, front={params['sensors']['front_cm']}cm, vacuum={params['vacuum']}")
                received_telemetry = True
                break
        assert received_telemetry, "No telemetry notification received"
    except Exception as e:
        print(f"  ✗ Telemetry stream failed: {e}")
        client.close()
        return False

    client.close()
    print("\n==================================================")
    print("   ALL TESTS PASSED SUCCESSFULLY! (9/9)          ")
    print("==================================================")
    return True

if __name__ == "__main__":
    run_tests()
