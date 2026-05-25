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
var jpeg       = require('jpeg-js');
var ART_W      = 200;
var ART_H      = 146;
var CHUNK_SIZE = 1700;

// sRGB → linear LUT — avoids repeated Math.pow in the inner loop
var SRGB_TO_LIN = (function () {
  var t = new Array(256);
  for (var i = 0; i < 256; i++) {
    var c = i / 255;
    t[i] = c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
  }
  return t;
}());

function linToSrgb255(c) {
  if (c <= 0) return 0;
  if (c >= 1) return 255;
  return Math.round(255 * (c <= 0.0031308 ? 12.92 * c : 1.055 * Math.pow(c, 1 / 2.4) - 0.055));
}

// Center-fill crop + linear-light bilinear downsample + Floyd-Steinberg dither → Pebble 8-bit.
function decodeAndResize(jpegBytes) {
  var img  = jpeg.decode(jpegBytes, { useTArray: true });
  var srcW = img.width, srcH = img.height, rgba = img.data;

  var srcAspect = srcW / srcH, dstAspect = ART_W / ART_H;
  var sx, sy, sw, sh;
  if (srcAspect > dstAspect) {
    sh = srcH; sw = Math.round(sh * dstAspect);
    sx = Math.round((srcW - sw) / 2); sy = 0;
  } else {
    sw = srcW; sh = Math.round(sw / dstAspect);
    sx = 0; sy = Math.round((srcH - sh) / 2);
  }

  // Bilinear downsample into float buffers so F-S can accumulate error
  var n = ART_W * ART_H;
  var fr = new Array(n), fg = new Array(n), fb = new Array(n);
  for (var dy = 0; dy < ART_H; dy++) {
    for (var dx = 0; dx < ART_W; dx++) {
      var srcX = sx + dx * sw / ART_W;
      var srcY = sy + dy * sh / ART_H;
      var x0 = Math.floor(srcX), x1 = Math.min(x0 + 1, srcW - 1);
      var y0 = Math.floor(srcY), y1 = Math.min(y0 + 1, srcH - 1);
      var wu = srcX - x0, wv = srcY - y0;
      var w00=(1-wu)*(1-wv), w10=wu*(1-wv), w01=(1-wu)*wv, w11=wu*wv;
      var i00=(y0*srcW+x0)*4, i10=(y0*srcW+x1)*4, i01=(y1*srcW+x0)*4, i11=(y1*srcW+x1)*4;
      var idx = dy*ART_W+dx;
      fr[idx] = linToSrgb255(SRGB_TO_LIN[rgba[i00  ]]*w00 + SRGB_TO_LIN[rgba[i10  ]]*w10 + SRGB_TO_LIN[rgba[i01  ]]*w01 + SRGB_TO_LIN[rgba[i11  ]]*w11);
      fg[idx] = linToSrgb255(SRGB_TO_LIN[rgba[i00+1]]*w00 + SRGB_TO_LIN[rgba[i10+1]]*w10 + SRGB_TO_LIN[rgba[i01+1]]*w01 + SRGB_TO_LIN[rgba[i11+1]]*w11);
      fb[idx] = linToSrgb255(SRGB_TO_LIN[rgba[i00+2]]*w00 + SRGB_TO_LIN[rgba[i10+2]]*w10 + SRGB_TO_LIN[rgba[i01+2]]*w01 + SRGB_TO_LIN[rgba[i11+2]]*w11);
    }
  }

  // Floyd-Steinberg: quantise then spread error to right/below neighbours
  var pixels = new Uint8Array(n);
  for (var dy = 0; dy < ART_H; dy++) {
    for (var dx = 0; dx < ART_W; dx++) {
      var idx = dy*ART_W+dx;
      var rv = Math.min(255, Math.max(0, fr[idx]));
      var gv = Math.min(255, Math.max(0, fg[idx]));
      var bv = Math.min(255, Math.max(0, fb[idx]));

      var rq = Math.min(3, Math.max(0, Math.round(rv / 85)));
      var gq = Math.min(3, Math.max(0, Math.round(gv / 85)));
      var bq = Math.min(3, Math.max(0, Math.round(bv / 85)));

      pixels[idx] = 0xC0 | (rq << 4) | (gq << 2) | bq;

      var er = rv - rq*85, eg = gv - gq*85, eb = bv - bq*85;
      if (dx+1 < ART_W) {
        fr[idx+1] += er*7/16; fg[idx+1] += eg*7/16; fb[idx+1] += eb*7/16;
      }
      if (dy+1 < ART_H) {
        if (dx > 0) { fr[idx+ART_W-1] += er*3/16; fg[idx+ART_W-1] += eg*3/16; fb[idx+ART_W-1] += eb*3/16; }
        fr[idx+ART_W] += er*5/16; fg[idx+ART_W] += eg*5/16; fb[idx+ART_W] += eb*5/16;
        if (dx+1 < ART_W) { fr[idx+ART_W+1] += er/16; fg[idx+ART_W+1] += eg/16; fb[idx+ART_W+1] += eb/16; }
      }
    }
  }
  return pixels;
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

function fetchAndSendImage(artUrl) {
  if (!artUrl) return;
  getBinary(artUrl, function (err, bytes) {
    if (err) { console.log('Image fetch error: ' + err); return; }
    try {
      sendImageChunks(decodeAndResize(bytes));
    } catch (e) {
      console.log('Image decode error: ' + e);
    }
  });
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
  console.log('Scryble JS ready');
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
