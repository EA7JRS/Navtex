// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Manuel - EA7JRS
// El archivo: I/Q de 16 bits, big endian, a 12 kS/s, centrado en el canal (una grabación de NAVTEX propia).
// KiwiSDR de pega para Navtex: en el canal de 518 kHz manda una grabación real de NAVTEX (I/Q a 12 kS/s, centrada);
// en los demás, ruido débil. Uso: node kiwi_falso_navtex.js <puerto> <archivo.be>
const net = require('net'), crypto = require('crypto'), fs = require('fs');
const RATE = 12000, PORT = +process.argv[2] || 8073, IQ = fs.readFileSync(process.argv[3]);
function frame(buf, op = 2) { const n = buf.length; const h = n < 126 ? Buffer.from([0x80 | op, n]) : Buffer.from([0x80 | op, 126, n >> 8, n & 255]); return Buffer.concat([h, buf]); }
const msg = (t) => frame(Buffer.from('MSG ' + t));
let chans = 0;
net.createServer((s) => {
  let buf = Buffer.alloc(0), up = false, timer = null, i = 0, seq = 0, khz = 0; const said = [];
  s.on('error', () => {}); s.on('close', () => { clearInterval(timer); chans--; console.log('canal fuera', khz, JSON.stringify(said)); });
  s.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    if (!up) {
      const e = buf.indexOf('\r\n\r\n'); if (e < 0) return;
      const head = buf.slice(0, e).toString(); buf = buf.slice(e + 4); console.log(head.split('\r\n')[0]);
      const key = /Sec-WebSocket-Key: (.+)/.exec(head)[1].trim();
      s.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64') + '\r\n\r\n');
      up = true; chans++;
    }
    for (;;) {
      if (buf.length < 6) return;
      let n = buf[1] & 127, h = 2; if (n === 126) { n = buf.readUInt16BE(2); h = 4; }
      if (buf.length < h + 4 + n) return;
      const mask = buf.slice(h, h + 4), p = Buffer.from(buf.slice(h + 4, h + 4 + n)); for (let k = 0; k < n; k++) p[k] ^= mask[k & 3];
      buf = buf.slice(h + 4 + n);
      const t = p.toString(); if (t !== 'SET keepalive') said.push(t);
      if (t.startsWith('SET auth')) { s.write(msg('audio_init=0 audio_rate=12000')); s.write(msg('sample_rate=' + RATE)); }
      const m = /SET mod=iq .*freq=([0-9.]+)/.exec(t);
      if (m && !timer) {
        khz = +m[1]; const t0 = Date.now();
        timer = setInterval(() => {
          const want = Math.floor((Date.now() - t0) / 1000 * RATE);
          while (i + 512 <= want) {
            const b = Buffer.alloc(3 + 17 + 4 * 512); b.write('SND'); b[3] = 0x08; b.writeUInt32LE(seq++, 4); b.writeUInt16BE(670, 8);
            if (khz === 518 && (i + 512) * 4 <= IQ.length) IQ.copy(b, 20, i * 4, (i + 512) * 4);
            else for (let k = 0; k < 1024; k++) b.writeInt16BE(Math.round((Math.random() - 0.5) * 200), 20 + 2 * k);
            i += 512; s.write(frame(b));
          }
        }, 20);
      }
    }
  });
}).listen(PORT, '127.0.0.1', () => console.log('KiwiSDR de pega (Navtex) en', PORT));
