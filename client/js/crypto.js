/**
 * crypto.js - Client-Side End-to-End Encryption
 * Uses native Web Crypto API (window.crypto.subtle) - Zero external dependencies.
 * Algorithm: AES-256-CBC with PKCS#7 padding.
 */

// Shared 32-byte transport key (matches g_transport_key on the server)
const TRANSPORT_KEY_STRING = "robot-vacuum-secure-transit-key!";

function arrayBufferToBase64(buffer) {
    let binary = '';
    const bytes = new Uint8Array(buffer);
    const len = bytes.byteLength;
    for (let i = 0; i < len; i++) {
        binary += String.fromCharCode(bytes[i]);
    }
    return window.btoa(binary);
}

function base64ToArrayBuffer(base64) {
    const binary = window.atob(base64);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) {
        bytes[i] = binary.charCodeAt(i);
    }
    return bytes.buffer;
}

let cachedCryptoKey = null;

async function getTransportKey() {
    if (cachedCryptoKey) return cachedCryptoKey;
    const encoder = new TextEncoder();
    const keyData = encoder.encode(TRANSPORT_KEY_STRING);
    cachedCryptoKey = await window.crypto.subtle.importKey(
        "raw",
        keyData,
        { name: "AES-CBC" },
        false,
        ["encrypt", "decrypt"]
    );
    return cachedCryptoKey;
}

/**
 * Encrypts username and password with a fresh random IV.
 * @param {string} username 
 * @param {string} password 
 * @returns {Promise<{encrypted: string, iv: string}>} Base64 ciphertext and IV
 */
async function encryptCredentials(username, password) {
    const key = await getTransportKey();
    const iv = window.crypto.getRandomValues(new Uint8Array(16));

    const payload = JSON.stringify({
        username: username,
        password: password,
        timestamp: Date.now()
    });

    const encoder = new TextEncoder();
    const data = encoder.encode(payload);

    const encryptedBuffer = await window.crypto.subtle.encrypt(
        { name: "AES-CBC", iv: iv },
        key,
        data
    );

    return {
        encrypted: arrayBufferToBase64(encryptedBuffer),
        iv: arrayBufferToBase64(iv)
    };
}
