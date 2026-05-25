'use strict';

// ── XMLHttpRequest mock (backed by native fetch) ───────────────────────────
class XMLHttpRequest {
  constructor() {
    this.status = 0;
    this.responseText = '';
    this.onload = null;
    this.onerror = null;
    this._url = '';
  }
  open(_method, url) { this._url = url; }
  send() {
    fetch(this._url, {
      headers: { 'User-Agent': 'MTGWiki-PebbleHarness/1.0' }
    })
      .then(r => { this.status = r.status; return r.text(); })
      .then(body => { this.responseText = body; if (this.onload) this.onload(); })
      .catch(err => { if (this.onerror) this.onerror(err); });
  }
}
global.XMLHttpRequest = XMLHttpRequest;

// ── Pebble mock ────────────────────────────────────────────────────────────
const _listeners = {};
let _capturedCardId = null;

const STATUS_TAG = { 0: 'CARD', 1: 'LIST', 3: 'ERROR', 4: 'IMAGE_CHUNK' };

function printMessage(msg) {
  const tag = STATUS_TAG[msg.Status] ?? `status=${msg.Status}`;
  console.log(`\n  ← [${tag}]`);
  if (msg.Status === 1 /* LIST */) {
    console.log(`    count=${msg.ListCount}  has_more=${msg.HasMore}`);
    for (let i = 0; i < msg.ListCount; i++) {
      const name = msg[`ListCard${i}Name`];
      const id   = msg[`ListCard${i}Id`];
      console.log(`    [${i}] ${name}  (${id})`);
      if (i === 0 && !_capturedCardId) _capturedCardId = id;
    }
  } else if (msg.Status === 0 /* CARD */) {
    console.log(`    name:   ${msg.CardName}`);
    console.log(`    mana:   ${msg.CardManaCost}`);
    console.log(`    type:   ${msg.CardTypeLine}`);
    console.log(`    colors: ${msg.CardColors}`);
    if (msg.CardPT) console.log(`    P/T:    ${msg.CardPT}`);
    const text = (msg.CardOracleText || '').substring(0, 140);
    console.log(`    text:   ${text}${msg.CardOracleText?.length > 140 ? '…' : ''}`);
  } else if (msg.Status === 3 /* ERROR */) {
    console.log(`    ERROR: ${msg.CardOracleText}`);
  }
}

global.Pebble = {
  addEventListener(event, cb) { _listeners[event] = cb; },
  sendAppMessage(msg, ok) {
    printMessage(msg);
    if (ok) ok();
  }
};

function emit(event, payload) {
  if (_listeners[event]) _listeners[event]({ payload });
}

// ── waitForMessage — MUST be called before emit() ─────────────────────────
// Intercepts the next sendAppMessage call (sync or async).
function waitForMessage(timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      global.Pebble.sendAppMessage = orig;
      reject(new Error('Timed out waiting for watch message'));
    }, timeoutMs);

    const orig = global.Pebble.sendAppMessage.bind(global.Pebble);
    global.Pebble.sendAppMessage = (msg, ok) => {
      clearTimeout(timer);
      global.Pebble.sendAppMessage = orig;
      orig(msg, ok);
      resolve(msg);
    };
  });
}

function header(title) {
  console.log('\n' + '═'.repeat(60));
  console.log(title);
  console.log('═'.repeat(60));
}

// ── Load companion JS ──────────────────────────────────────────────────────
require('../src/pkjs/index.js');
emit('ready', {});

// ── Tests ──────────────────────────────────────────────────────────────────
async function runTests() {
  // 1 — Random card
  header('TEST 1 — Random card');
  let p = waitForMessage();          // interceptor up BEFORE emit
  emit('appmessage', { Action: 0 });
  await p;

  // 2 — Search: Blue / CMC=2 / Instant / A–D  (network hit, populates cache)
  header('TEST 2 — Search  (Blue / CMC=2 / Instant / A–D)');
  p = waitForMessage();
  emit('appmessage', {
    Action: 1, FilterColor: 1, FilterCmc: 2,
    FilterType: 1, FilterAlpha: 0, ListOffset: 0
  });
  await p;

  // 3 — Load more offset=5  (synchronous cache hit)
  header('TEST 3 — Load more (offset=5, cache hit)');
  p = waitForMessage();
  emit('appmessage', {
    Action: 1, FilterColor: 1, FilterCmc: 2,
    FilterType: 1, FilterAlpha: 0, ListOffset: 5
  });
  await p;

  // 4 — Get card by ID captured from TEST 2
  if (_capturedCardId) {
    header(`TEST 4 — Get card by ID  (cache hit: ${_capturedCardId})`);
    p = waitForMessage();
    emit('appmessage', { Action: 2, CardId: _capturedCardId });
    await p;
  }

  // 5 — Empty / error case: Colorless / CMC=0 / Battle / A–D
  header('TEST 5 — Empty result  (Colorless / CMC=0 / Battle / A–D)');
  p = waitForMessage();
  emit('appmessage', {
    Action: 1, FilterColor: 6, FilterCmc: 0,
    FilterType: 7, FilterAlpha: 0, ListOffset: 0
  });
  await p;

  header('All tests complete');
}

runTests().catch(err => {
  console.error('\n✗ Harness error:', err.message);
  process.exit(1);
});
