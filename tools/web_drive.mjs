// Drives headless Chrome over the DevTools protocol (no npm packages; Node 20 needs --experimental-websocket).
// usage: node --experimental-websocket tools/web_drive.mjs <url> '<steps json>' <chrome profile dir>
// steps: [{wait:sec},{shot:"file.png"},{key:"w",hold:ms},{click:[x,y]},{eval:"js"},{waitfor:"js expression",timeout:sec}]
import { spawn } from 'node:child_process';
import fs from 'node:fs';
const [url, stepsJson, profile] = process.argv.slice(2);
const steps = JSON.parse(stepsJson);
const port = 9333 + Math.floor(Math.random() * 500);
const chrome = spawn('xvfb-run', ['-a', '-s', '-screen 0 1280x800x24', 'google-chrome', '--headless=new', '--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader',
  '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', `--user-data-dir=${profile}`, `--remote-debugging-port=${port}`, '--window-size=1280,720', 'about:blank'], { stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let targets;
for (let i = 0; i < 60; i++) {
  try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); if (targets.length) break; } catch {}
  await sleep(500);
}
const page = targets.find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => (ws.onopen = r));
let id = 0; const pending = new Map();
const logs = [];
ws.onmessage = (m) => {
  const d = JSON.parse(m.data);
  if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); }
  else if (d.method === 'Runtime.consoleAPICalled') logs.push(`[${d.params.type}] ` + d.params.args.map((a) => a.value ?? a.description ?? '').join(' '));
  else if (d.method === 'Runtime.exceptionThrown') logs.push('[EXCEPTION] ' + (d.params.exceptionDetails.exception?.description ?? d.params.exceptionDetails.text));
  else if (d.method === 'Log.entryAdded') logs.push(`[log:${d.params.entry.level}] ${d.params.entry.text} ${d.params.entry.url ?? ''}`);
};
const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
await send('Page.enable'); await send('Runtime.enable'); await send('Log.enable');
await send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 720, deviceScaleFactor: 1, mobile: false });
await send('Page.navigate', { url });
for (const s of steps) {
  if (s.wait) await sleep(s.wait * 1000);
  else if (s.shot) { const r = await send('Page.captureScreenshot', { format: 'png' }); fs.writeFileSync(s.shot, Buffer.from(r.result.data, 'base64')); console.log('shot', s.shot); }
  else if (s.key) {
    const k = s.key; const code = k.length === 1 ? 'Key' + k.toUpperCase() : k;
    await send('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code, text: k.length === 1 ? k : undefined, windowsVirtualKeyCode: k.length === 1 ? k.toUpperCase().charCodeAt(0) : s.vk });
    await sleep(s.hold ?? 100);
    await send('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code, windowsVirtualKeyCode: k.length === 1 ? k.toUpperCase().charCodeAt(0) : s.vk });
  } else if (s.click) {
    const [x, y] = s.click;
    await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y });
    await send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 });
    await send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 });
  } else if (s.move) {
    await send('Input.dispatchMouseEvent', { type: 'mouseMoved', x: s.move[0], y: s.move[1], movementX: s.move[2] ?? 0, movementY: s.move[3] ?? 0 });
  } else if (s.eval) { const r = await send('Runtime.evaluate', { expression: s.eval, returnByValue: true }); console.log('eval ->', JSON.stringify(r.result?.result?.value ?? r.result)); }
  else if (s.waitfor) {
    const end = Date.now() + (s.timeout ?? 60) * 1000; let ok = false;
    while (Date.now() < end) { const r = await send('Runtime.evaluate', { expression: s.waitfor, returnByValue: true }); if (r.result?.result?.value) { ok = true; break; } await sleep(1000); }
    console.log('waitfor', s.waitfor, ok ? 'ok' : 'TIMEOUT');
  }
}
console.log('--- console (' + logs.length + ') ---');
for (const l of logs.slice(0, 120)) console.log(l.slice(0, 300));
ws.close(); chrome.kill('SIGTERM'); spawn('pkill', ['-f', `remote-debugging-port=${port}`]);
await sleep(500); process.exit(0);
