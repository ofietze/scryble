'use strict';

var BASE = 'https://api.scryfall.com';

// ── Action / status constants (must match comm.h) ──────────────────────────
var ACTION_RANDOM   = 0;
var ACTION_SEARCH   = 1;
var ACTION_GET_CARD = 2;

var STATUS_CARD        = 0;
var STATUS_LIST        = 1;
var STATUS_ERROR       = 3;
var STATUS_IMAGE_CHUNK = 4;

// ── Filter mappings ────────────────────────────────────────────────────────
var COLOR_QUERIES = ['c:W', 'c:U', 'c:B', 'c:R', 'c:G', 'c>=2', 'c:C'];

var TYPE_NAMES = [
  'creature', 'instant', 'sorcery', 'enchantment',
  'artifact', 'land', 'planeswalker', 'battle'
];

// Alpha bucket index → [startChar, endChar]
var ALPHA_RANGES = [
  ['A','D'], ['E','H'], ['I','L'], ['M','P'], ['Q','T'], ['U','Z']
];

// ── Image proxy (optional, used on iOS) ────────────────────────────────────
// The watch displays cropped artwork at exactly 200×75 in Pebble's 2-bit-per-channel
// palette. On Android the Pebble JS environment is a WebView and has Canvas + Image,
// so we render & quantize locally (see fetchAndSendImage below). On iOS the JS
// environment is JavaScriptCore with no DOM — neither Image nor Canvas exist.
//
// The recommended iOS approach is a tiny stateless image-proxy worker (Cloudflare
// Workers, fly.io, or Vercel — all free for this kind of traffic) that:
//   1. Takes ?url=<scryfall_art_crop> as input
//   2. Fetches, resizes to 200×75, quantizes each pixel to 0xC0|(R<<4)|(G<<2)|B
//   3. Returns raw bytes as application/octet-stream
//
// Drop your worker URL below to enable iOS art. Leave empty to fall back to
// the diagonal-hatch placeholder used by card_view_window.
var IMAGE_PROXY_URL = '';   // e.g. 'https://mtg-pebble.example.workers.dev/quantize'

// ── Card cache ─────────────────────────────────────────────────────────────
var cardCache = [];
var nextPageUrl = null;

function cacheCard(c) {
  return {
    id:          c.id,
    name:        c.name              || '',
    mana_cost:   c.mana_cost         || '',
    type_line:   c.type_line         || '',
    oracle_text: c.oracle_text       || '',
    power:       c.power  != null    ? c.power     : null,
    toughness:   c.toughness != null ? c.toughness : null,
    colors:      c.colors            || [],
    art_crop:    (c.image_uris && c.image_uris.art_crop) ? c.image_uris.art_crop : ''
  };
}

// ── Network helpers ────────────────────────────────────────────────────────
function get(url, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.onload = function () {
    if (xhr.status === 200) {
      try { cb(null, JSON.parse(xhr.responseText)); }
      catch (e) { cb('JSON parse error'); }
    } else {
      cb('HTTP ' + xhr.status + ' for ' + url);
    }
  };
  xhr.onerror = function () { cb('Network error'); };
  xhr.send();
}

function getBinary(url, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.responseType = 'arraybuffer';
  xhr.onload = function () {
    if (xhr.status === 200) cb(null, new Uint8Array(xhr.response));
    else cb('HTTP ' + xhr.status + ' for ' + url);
  };
  xhr.onerror = function () { cb('Network error'); };
  xhr.send();
}

// ── Image pipeline ─────────────────────────────────────────────────────────
var ART_W = 200;
var ART_H = 75;
var CHUNK_SIZE = 1700;

function quantizePixel(r, g, b) {
  var rq = Math.min(3, Math.round(r / 85));
  var gq = Math.min(3, Math.round(g / 85));
  var bq = Math.min(3, Math.round(b / 85));
  return 0xC0 | (rq << 4) | (gq << 2) | bq;
}

function sendImageChunks(pixels) {
  var offset = 0;
  function next() {
    if (offset >= pixels.length) return;
    var end  = Math.min(offset + CHUNK_SIZE, pixels.length);
    var done = (end >= pixels.length) ? 1 : 0;
    var chunk = [];
    for (var i = offset; i < end; i++) chunk.push(pixels[i]);
    Pebble.sendAppMessage({
      Status:           STATUS_IMAGE_CHUNK,
      ImageChunkOffset: offset,
      ImageWidth:       ART_W,
      ImageHeight:      ART_H,
      ImageDone:        done,
      ImageData:        chunk
    }, function () { offset = end; next(); },
       function (e) { console.log('Image chunk failed: ' + e); });
  }
  next();
}

function hasCanvas() {
  return typeof window !== 'undefined' && typeof document !== 'undefined' &&
         typeof Image === 'function';
}

function fetchViaCanvas(artUrl) {
  var img = new Image();
  img.crossOrigin = 'Anonymous';
  img.onload = function () {
    try {
      // Center-fill crop: scale to fill ART_W×ART_H, crop excess.
      var srcAspect = img.width / img.height;
      var dstAspect = ART_W / ART_H;
      var sx, sy, sw, sh;
      if (srcAspect > dstAspect) {
        sh = img.height; sw = Math.round(sh * dstAspect);
        sx = Math.round((img.width - sw) / 2); sy = 0;
      } else {
        sw = img.width; sh = Math.round(sw / dstAspect);
        sx = 0; sy = Math.round((img.height - sh) / 2);
      }
      var canvas = document.createElement('canvas');
      canvas.width = ART_W; canvas.height = ART_H;
      var ctx = canvas.getContext('2d');
      ctx.drawImage(img, sx, sy, sw, sh, 0, 0, ART_W, ART_H);
      var rgba = ctx.getImageData(0, 0, ART_W, ART_H).data;

      var pixels = new Uint8Array(ART_W * ART_H);
      for (var i = 0; i < ART_W * ART_H; i++) {
        pixels[i] = quantizePixel(rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
      }
      sendImageChunks(pixels);
    } catch (e) {
      console.log('Image process error: ' + e);
    }
  };
  img.onerror = function () { console.log('Image load failed for: ' + artUrl); };
  img.src = artUrl;
}

function fetchViaProxy(artUrl) {
  var proxyUrl = IMAGE_PROXY_URL + '?url=' + encodeURIComponent(artUrl);
  getBinary(proxyUrl, function (err, bytes) {
    if (err) { console.log('Image proxy error: ' + err); return; }
    if (bytes.length !== ART_W * ART_H) {
      console.log('Proxy returned ' + bytes.length + ' bytes, expected ' + (ART_W * ART_H));
      return;
    }
    sendImageChunks(bytes);
  });
}

function fetchAndSendImage(artUrl) {
  if (!artUrl) return;
  if (hasCanvas()) {
    fetchViaCanvas(artUrl);
  } else if (IMAGE_PROXY_URL) {
    fetchViaProxy(artUrl);
  } else {
    console.log('Image: no Canvas (iOS) and no IMAGE_PROXY_URL configured — skipping.');
  }
}

// ── Senders ────────────────────────────────────────────────────────────────
function sendError(msg) {
  console.log('ERR: ' + msg);
  Pebble.sendAppMessage({
    Status: STATUS_ERROR,
    CardOracleText: String(msg).substring(0, 110)
  }, function () {}, function (e) { console.log('send err failed: ' + e); });
}

function sendCard(c) {
  var pt = (c.power != null) ? (c.power + '/' + c.toughness) : '';
  var oracle = c.oracle_text || '';
  if (oracle.length > 450) oracle = oracle.substring(0, 447) + '...';

  Pebble.sendAppMessage({
    Status:         STATUS_CARD,
    CardId:         (c.id || '').substring(0, 36),
    CardName:       c.name.substring(0, 60),
    CardManaCost:   c.mana_cost.substring(0, 45),
    CardTypeLine:   c.type_line.substring(0, 60),
    CardOracleText: oracle,
    CardPT:         pt,
    CardColors:     c.colors.join('').substring(0, 10)
  }, function () {
    // Kick off image after card text is ACKed
    fetchAndSendImage(c.art_crop);
  }, function (e) { console.log('send card failed: ' + e); });
}

function sendList(offset) {
  var slice = cardCache.slice(offset, offset + 5);
  var hasMore = (offset + 5 < cardCache.length) || (nextPageUrl !== null);

  var msg = {
    Status:    STATUS_LIST,
    ListCount: slice.length,
    HasMore:   hasMore ? 1 : 0
  };

  var nameKeys = ['ListCard0Name','ListCard1Name','ListCard2Name','ListCard3Name','ListCard4Name'];
  var idKeys   = ['ListCard0Id',  'ListCard1Id',  'ListCard2Id',  'ListCard3Id',  'ListCard4Id'];

  for (var i = 0; i < slice.length; i++) {
    msg[nameKeys[i]] = slice[i].name.substring(0, 60);
    msg[idKeys[i]]   = slice[i].id;
  }

  Pebble.sendAppMessage(msg, function () {}, function (e) { console.log('send list failed: ' + e); });
}

// ── Query builder ──────────────────────────────────────────────────────────
function buildQuery(f) {
  var cmcPart = (f.cmc === 7) ? 'cmc>=7' : ('cmc=' + f.cmc);
  var namePart;
  if (f.letter && f.letter.length === 1) {
    // Exact-letter narrowing wins over the bucket range.
    namePart = 'name:/^' + f.letter + '/i';
  } else {
    var range = ALPHA_RANGES[f.alpha] || ['A','Z'];
    namePart = 'name:/^[' + range[0] + '-' + range[1] + ']/i';
  }
  return [
    COLOR_QUERIES[f.color] || 'c:W',
    cmcPart,
    't:' + (TYPE_NAMES[f.type] || 'creature'),
    namePart
  ].join(' ');
}

// ── Handlers ───────────────────────────────────────────────────────────────
function handleRandom() {
  get(BASE + '/cards/random', function (err, data) {
    if (err) { sendError('No cards found'); return; }
    sendCard(cacheCard(data));
  });
}

function handleSearch(f, offset) {
  if (offset === 0) {
    cardCache = [];
    nextPageUrl = null;
    var q = buildQuery(f);
    var url = BASE + '/cards/search?q=' + encodeURIComponent(q) + '&order=name';
    console.log('Searching: ' + url);

    get(url, function (err, data) {
      if (err) { sendError('No cards found'); return; }
      if (!data.data || data.data.length === 0) { sendError('No cards found'); return; }

      cardCache = data.data.map(cacheCard);
      nextPageUrl = data.has_more ? data.next_page : null;
      sendList(0);
    });

  } else {
    if (offset < cardCache.length) {
      sendList(offset);
    } else if (nextPageUrl) {
      get(nextPageUrl, function (err, data) {
        if (err) { sendError('Load more failed: ' + err); return; }
        cardCache = cardCache.concat(data.data.map(cacheCard));
        nextPageUrl = data.has_more ? data.next_page : null;
        sendList(offset);
      });
    } else {
      sendError('No more cards');
    }
  }
}

function handleGetCard(cardId) {
  for (var i = 0; i < cardCache.length; i++) {
    if (cardCache[i].id === cardId) { sendCard(cardCache[i]); return; }
  }
  get(BASE + '/cards/' + cardId, function (err, data) {
    if (err) { sendError('Card fetch failed: ' + err); return; }
    sendCard(cacheCard(data));
  });
}

// ── Entry points ───────────────────────────────────────────────────────────
Pebble.addEventListener('ready', function () {
  console.log('MTG Wiki JS ready');
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload;
  var action = p.Action;

  if (action === ACTION_RANDOM) {
    handleRandom();
  } else if (action === ACTION_SEARCH) {
    handleSearch({
      color:  p.FilterColor,
      cmc:    p.FilterCmc,
      type:   p.FilterType,
      alpha:  p.FilterAlpha,
      letter: p.FilterLetter || ''
    }, p.ListOffset || 0);
  } else if (action === ACTION_GET_CARD) {
    handleGetCard(p.CardId);
  } else {
    console.log('Unknown action: ' + action);
  }
});
