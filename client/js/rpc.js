/**
 * rpc.js - JSON-RPC 2.0 over WebSocket Client
 * Bi-directional RPC and real-time push notification dispatcher.
 */

class JsonRpcWebSocket {
    constructor(url = null) {
        this.url = url || `ws://${window.location.host}/websocket`;
        this.ws = null;
        this.rpcId = 1;
        this.pendingRequests = new Map();
        this.eventListeners = new Map();
        this.statusListeners = [];
        this.reconnectTimer = null;
        this.isConnected = false;
        this.timeoutMs = 5000;
    }

    connect() {
        if (this.ws && (this.ws.readyState === WebSocket.CONNECTING || this.ws.readyState === WebSocket.OPEN)) {
            return;
        }

        this.notifyStatus('connecting');

        try {
            this.ws = new WebSocket(this.url);
        } catch (err) {
            console.error('[RPC] Connection error:', err);
            this.scheduleReconnect();
            return;
        }

        this.ws.onopen = () => {
            console.log('[RPC] WebSocket connected to', this.url);
            this.isConnected = true;
            this.notifyStatus('connected');
            if (this.reconnectTimer) {
                clearTimeout(this.reconnectTimer);
                this.reconnectTimer = null;
            }
        };

        this.ws.onclose = () => {
            console.warn('[RPC] WebSocket disconnected');
            this.isConnected = false;
            this.notifyStatus('disconnected');
            this.rejectPendingRequests('WebSocket connection closed');
            this.scheduleReconnect();
        };

        this.ws.onerror = (err) => {
            console.error('[RPC] WebSocket error:', err);
            this.notifyStatus('error');
        };

        this.ws.onmessage = (event) => {
            try {
                const message = JSON.parse(event.data);
                this.handleMessage(message);
            } catch (err) {
                console.error('[RPC] Malformed frame received:', event.data, err);
            }
        };
    }

    scheduleReconnect() {
        if (this.reconnectTimer) return;
        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            console.log('[RPC] Attempting reconnection...');
            this.connect();
        }, 2000);
    }

    notifyStatus(status) {
        this.statusListeners.forEach(fn => fn(status));
    }

    onStatus(fn) {
        this.statusListeners.push(fn);
    }

    on(method, callback) {
        if (!this.eventListeners.has(method)) {
            this.eventListeners.set(method, []);
        }
        this.eventListeners.get(method).push(callback);
    }

    off(method, callback) {
        if (!this.eventListeners.has(method)) return;
        const list = this.eventListeners.get(method).filter(fn => fn !== callback);
        this.eventListeners.set(method, list);
    }

    handleMessage(msg) {
        // If frame has an id, it is an RPC response
        if (msg.id !== undefined && msg.id !== null) {
            const pending = this.pendingRequests.get(msg.id);
            if (pending) {
                clearTimeout(pending.timeoutId);
                this.pendingRequests.delete(msg.id);
                if (msg.error) {
                    pending.reject(new Error(msg.error.message || `RPC Error code: ${msg.error.code}`));
                } else {
                    pending.resolve(msg.result);
                }
            }
            return;
        }

        // If frame has a method, it is a server-to-client push notification
        if (msg.method) {
            const handlers = this.eventListeners.get(msg.method);
            if (handlers && handlers.length > 0) {
                handlers.forEach(fn => fn(msg.params));
            }
        }
    }

    call(method, params = {}) {
        return new Promise((resolve, reject) => {
            if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
                return reject(new Error('WebSocket is not connected'));
            }

            const id = this.rpcId++;
            const request = {
                jsonrpc: "2.0",
                id: id,
                method: method,
                params: params
            };

            const timeoutId = setTimeout(() => {
                if (this.pendingRequests.has(id)) {
                    this.pendingRequests.delete(id);
                    reject(new Error(`RPC timeout for method '${method}' after ${this.timeoutMs}ms`));
                }
            }, this.timeoutMs);

            this.pendingRequests.set(id, { resolve, reject, timeoutId });

            try {
                this.ws.send(JSON.stringify(request));
            } catch (err) {
                clearTimeout(timeoutId);
                this.pendingRequests.delete(id);
                reject(err);
            }
        });
    }

    rejectPendingRequests(reason) {
        this.pendingRequests.forEach((pending) => {
            clearTimeout(pending.timeoutId);
            pending.reject(new Error(reason));
        });
        this.pendingRequests.clear();
    }
}
