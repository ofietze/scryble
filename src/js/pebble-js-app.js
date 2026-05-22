'use strict';

var BASE = 'https://api.scryfall.com';

// ── Action / status constants (must match comm.h) ──────────────────────────
var ACTION_RANDOM   = 0;
var ACTION_SEARCH   = 1;
var ACTION_GET_CARD = 2;

var STATUS_CARD    = 0;
var STATUS_LIST    = 1;
var STATUS_ERROR   = 3;

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
    name:        c.name            || '',
    mana_cost:   c.mana_cost       || '',
    type_line:   c.type_line       || '',
    oracle_text: c.oracle_text     || '',
    power:       c.power  != null  ? c.power  : null,
    toughness:   c.toughness != null ? c.toughness : null,
    colors:      c.colors          || []
  };
}

// ── Network helper ─────────────────────────────────────────────────────────
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
    CardName:       c.name.substring(0, 60),
    CardManaCost:   c.mana_cost.substring(0, 45),
    CardTypeLine:   c.type_line.substring(0, 60),
    CardOracleText: oracle,
    CardPT:         pt,
    CardColors:     c.colors.join('').substring(0, 10)
  }, function () {}, function (e) { console.log('send card failed: ' + e); });
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
  var range = ALPHA_RANGES[f.alpha] || ['A','Z'];
  var cmcPart = (f.cmc === 7) ? 'cmc>=7' : ('cmc=' + f.cmc);

  return [
    COLOR_QUERIES[f.color] || 'c:W',
    cmcPart,
    't:' + (TYPE_NAMES[f.type] || 'creature'),
    'name:/^[' + range[0] + '-' + range[1] + ']/'
  ].join(' ');
}

// ── Handlers ───────────────────────────────────────────────────────────────
function handleRandom() {
  get(BASE + '/cards/random', function (err, data) {
    if (err) { sendError('Random failed: ' + err); return; }
    sendCard(cacheCard(data));
  });
}

function handleSearch(f, offset) {
  if (offset === 0) {
    // Fresh search — clear cache
    cardCache = [];
    nextPageUrl = null;
    var q = buildQuery(f);
    var url = BASE + '/cards/search?q=' + encodeURIComponent(q) + '&order=name';
    console.log('Searching: ' + url);

    get(url, function (err, data) {
      if (err) { sendError('Search failed: ' + err); return; }
      if (!data.data || data.data.length === 0) { sendError('No cards found'); return; }

      cardCache = data.data.map(cacheCard);
      nextPageUrl = data.has_more ? data.next_page : null;
      sendList(0);
    });

  } else {
    // Continue from cache or fetch next Scryfall page
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
  // Check cache first (avoids a round-trip for cards already in the list)
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
      color: p.FilterColor,
      cmc:   p.FilterCmc,
      type:  p.FilterType,
      alpha: p.FilterAlpha
    }, p.ListOffset || 0);
  } else if (action === ACTION_GET_CARD) {
    handleGetCard(p.CardId);
  } else {
    console.log('Unknown action: ' + action);
  }
});
