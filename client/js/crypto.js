/**
 * crypto.js - Client-Side End-to-End Encryption
 * Algorithm: AES-256-CBC with PKCS#7 padding.
 * 
 * Works in all browser contexts (secure HTTPS, localhost, and non-secure LAN IP HTTP),
 * eliminating the "Cannot read properties of undefined (reading 'importKey')" error
 * caused by browsers restricting window.crypto.subtle in non-secure contexts.
 */

// Shared 32-byte transport key (matches g_transport_key on the server)
const TRANSPORT_KEY_STRING = "robot-vacuum-secure-transit-key!";

// AES Substitution-box (SBOX)
const AES_SBOX = [
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
];

// Round constant table (RCON)
const AES_RCON = [0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36];

function xtime(a) {
    return ((a << 1) ^ (a & 0x80 ? 0x11b : 0)) & 0xff;
}

function subWord(w) {
    return ((AES_SBOX[(w >>> 24) & 0xff] << 24) |
            (AES_SBOX[(w >>> 16) & 0xff] << 16) |
            (AES_SBOX[(w >>> 8)  & 0xff] << 8)  |
            (AES_SBOX[w & 0xff])) >>> 0;
}

function rotWord(w) {
    return ((w << 8) | (w >>> 24)) >>> 0;
}

function keyExpansion(keyBytes) {
    const w = new Uint32Array(60);
    for (let i = 0; i < 8; i++) {
        w[i] = ((keyBytes[4 * i] << 24) |
                (keyBytes[4 * i + 1] << 16) |
                (keyBytes[4 * i + 2] << 8) |
                keyBytes[4 * i + 3]) >>> 0;
    }
    for (let i = 8; i < 60; i++) {
        let temp = w[i - 1];
        if (i % 8 === 0) {
            temp = (subWord(rotWord(temp)) ^ (AES_RCON[i / 8] << 24)) >>> 0;
        } else if (i % 8 === 4) {
            temp = subWord(temp);
        }
        w[i] = (w[i - 8] ^ temp) >>> 0;
    }
    return w;
}

function encryptBlock(block, roundKeys) {
    let s = new Uint8Array(block);

    // Initial AddRoundKey
    for (let c = 0; c < 4; c++) {
        const k = roundKeys[c];
        s[c * 4]     ^= (k >>> 24) & 0xff;
        s[c * 4 + 1] ^= (k >>> 16) & 0xff;
        s[c * 4 + 2] ^= (k >>> 8) & 0xff;
        s[c * 4 + 3] ^= k & 0xff;
    }

    // 14 Rounds for AES-256
    for (let round = 1; round <= 14; round++) {
        // SubBytes
        for (let i = 0; i < 16; i++) {
            s[i] = AES_SBOX[s[i]];
        }

        // ShiftRows
        const t1 = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t1;
        const t2 = s[2], t6 = s[6]; s[2] = s[10]; s[6] = s[14]; s[10] = t2; s[14] = t6;
        const t3 = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t3;

        // MixColumns (rounds 1 to 13)
        if (round < 14) {
            for (let c = 0; c < 4; c++) {
                const i = c * 4;
                const a0 = s[i], a1 = s[i + 1], a2 = s[i + 2], a3 = s[i + 3];
                s[i]     = xtime(a0 ^ a1) ^ a1 ^ a2 ^ a3;
                s[i + 1] = xtime(a1 ^ a2) ^ a2 ^ a3 ^ a0;
                s[i + 2] = xtime(a2 ^ a3) ^ a3 ^ a0 ^ a1;
                s[i + 3] = xtime(a3 ^ a0) ^ a0 ^ a1 ^ a2;
            }
        }

        // AddRoundKey
        for (let c = 0; c < 4; c++) {
            const k = roundKeys[round * 4 + c];
            s[c * 4]     ^= (k >>> 24) & 0xff;
            s[c * 4 + 1] ^= (k >>> 16) & 0xff;
            s[c * 4 + 2] ^= (k >>> 8) & 0xff;
            s[c * 4 + 3] ^= k & 0xff;
        }
    }
    return s;
}

function aesCbcEncrypt(plaintextBytes, keyBytes, ivBytes) {
    const roundKeys = keyExpansion(keyBytes);

    // PKCS#7 Padding
    const padLen = 16 - (plaintextBytes.length % 16);
    const padded = new Uint8Array(plaintextBytes.length + padLen);
    padded.set(plaintextBytes);
    for (let i = plaintextBytes.length; i < padded.length; i++) {
        padded[i] = padLen;
    }

    const ciphertext = new Uint8Array(padded.length);
    let prev = new Uint8Array(ivBytes);

    // CBC Mode block chaining
    for (let i = 0; i < padded.length; i += 16) {
        const block = new Uint8Array(16);
        for (let j = 0; j < 16; j++) {
            block[j] = padded[i + j] ^ prev[j];
        }
        const enc = encryptBlock(block, roundKeys);
        ciphertext.set(enc, i);
        prev = enc;
    }

    return ciphertext;
}

function stringToUtf8Bytes(str) {
    if (typeof TextEncoder !== "undefined") {
        return new TextEncoder().encode(str);
    }
    const utf8 = [];
    for (let i = 0; i < str.length; i++) {
        let charcode = str.charCodeAt(i);
        if (charcode < 0x80) {
            utf8.push(charcode);
        } else if (charcode < 0x800) {
            utf8.push(0xc0 | (charcode >> 6), 0x80 | (charcode & 0x3f));
        } else if (charcode < 0xd800 || charcode >= 0xe000) {
            utf8.push(0xe0 | (charcode >> 12), 0x80 | ((charcode >> 6) & 0x3f), 0x80 | (charcode & 0x3f));
        } else {
            i++;
            charcode = 0x10000 + (((charcode & 0x3ff) << 10) | (str.charCodeAt(i) & 0x3ff));
            utf8.push(0xf0 | (charcode >> 18), 0x80 | ((charcode >> 12) & 0x3f), 0x80 | ((charcode >> 6) & 0x3f), 0x80 | (charcode & 0x3f));
        }
    }
    return new Uint8Array(utf8);
}

function arrayBufferToBase64(buffer) {
    const bytes = new Uint8Array(buffer);
    let binary = '';
    const len = bytes.byteLength;
    for (let i = 0; i < len; i++) {
        binary += String.fromCharCode(bytes[i]);
    }
    if (typeof window !== "undefined" && typeof window.btoa === "function") {
        return window.btoa(binary);
    }
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    let res = '';
    let i = 0;
    for (; i + 2 < len; i += 3) {
        res += chars[bytes[i] >> 2];
        res += chars[((bytes[i] & 3) << 4) | (bytes[i + 1] >> 4)];
        res += chars[((bytes[i + 1] & 15) << 2) | (bytes[i + 2] >> 6)];
        res += chars[bytes[i + 2] & 63];
    }
    if (i < len) {
        res += chars[bytes[i] >> 2];
        if (i + 1 < len) {
            res += chars[((bytes[i] & 3) << 4) | (bytes[i + 1] >> 4)];
            res += chars[(bytes[i + 1] & 15) << 2];
            res += '=';
        } else {
            res += chars[(bytes[i] & 3) << 4];
            res += '==';
        }
    }
    return res;
}

function base64ToArrayBuffer(base64) {
    if (typeof window !== "undefined" && typeof window.atob === "function") {
        const binary = window.atob(base64);
        const bytes = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) {
            bytes[i] = binary.charCodeAt(i);
        }
        return bytes.buffer;
    }
    return new Uint8Array(0).buffer;
}

function generateRandomIV() {
    const iv = new Uint8Array(16);
    if (typeof window !== "undefined" && window.crypto && typeof window.crypto.getRandomValues === 'function') {
        window.crypto.getRandomValues(iv);
    } else {
        for (let i = 0; i < 16; i++) {
            iv[i] = Math.floor(Math.random() * 256);
        }
    }
    return iv;
}

/**
 * Encrypts username and password with a fresh random IV using AES-256-CBC.
 * Compatible with all browsers and contexts (secure or non-secure HTTP).
 * 
 * @param {string} username 
 * @param {string} password 
 * @returns {Promise<{encrypted: string, iv: string}>} Base64 ciphertext and IV
 */
async function encryptCredentials(username, password) {
    const iv = generateRandomIV();

    const payload = JSON.stringify({
        username: username,
        password: password,
        timestamp: Date.now()
    });

    const plaintextBytes = stringToUtf8Bytes(payload);
    const keyBytes = stringToUtf8Bytes(TRANSPORT_KEY_STRING);

    const ciphertextBytes = aesCbcEncrypt(plaintextBytes, keyBytes, iv);

    return {
        encrypted: arrayBufferToBase64(ciphertextBytes),
        iv: arrayBufferToBase64(iv)
    };
}
