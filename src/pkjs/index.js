var weather = require('./weather');

function getWatchPlatform() {
  if (!Pebble.getActiveWatchInfo) return null;
  var info = Pebble.getActiveWatchInfo();
  return info ? info.platform : null;
}

function loadSettings() {
  return {
    ShowTopLeft: window.localStorage.getItem('ShowTopLeft') !== '0',
    ShowTopRight: window.localStorage.getItem('ShowTopRight') !== '0',
    ShowBottomLeft: window.localStorage.getItem('ShowBottomLeft') !== '0',
    ShowBottomRight: window.localStorage.getItem('ShowBottomRight') !== '0',
    InvertColors: window.localStorage.getItem('InvertColors') === '1'
  };
}

function saveSettings(settings) {
  window.localStorage.setItem('ShowTopLeft', settings.ShowTopLeft ? '1' : '0');
  window.localStorage.setItem('ShowTopRight', settings.ShowTopRight ? '1' : '0');
  window.localStorage.setItem('ShowBottomLeft', settings.ShowBottomLeft ? '1' : '0');
  window.localStorage.setItem('ShowBottomRight', settings.ShowBottomRight ? '1' : '0');
  window.localStorage.setItem('InvertColors', settings.InvertColors ? '1' : '0');
}

function sendSettingsToWatch(settings) {
  Pebble.sendAppMessage({
    ShowTopLeft: settings.ShowTopLeft ? 1 : 0,
    ShowTopRight: settings.ShowTopRight ? 1 : 0,
    ShowBottomLeft: settings.ShowBottomLeft ? 1 : 0,
    ShowBottomRight: settings.ShowBottomRight ? 1 : 0,
    InvertColors: settings.InvertColors ? 1 : 0
  });
}

function buildConfigPageHtml(isGabbro, settings) {
  var sectionCorners = isGabbro ? '' :
    '<label class=\"item\"><span>Top-left weather</span><span class=\"switch\"><input id=\"ShowTopLeft\" type=\"checkbox\"><span class=\"slider\"></span></span></label>' +
    '<label class=\"item\"><span>Top-right battery</span><span class=\"switch\"><input id=\"ShowTopRight\" type=\"checkbox\"><span class=\"slider\"></span></span></label>' +
    '<label class=\"item\"><span>Bottom-left time</span><span class=\"switch\"><input id=\"ShowBottomLeft\" type=\"checkbox\"><span class=\"slider\"></span></span></label>' +
    '<label class=\"item\"><span>Bottom-right date</span><span class=\"switch\"><input id=\"ShowBottomRight\" type=\"checkbox\"><span class=\"slider\"></span></span></label>';
  var gabbroNote = isGabbro ? '<p class=\"note\">Corner elements are hidden on Pebble Round 2 (Gabbro).</p>' : '';

  return '<!doctype html><html><head><meta charset=\"utf-8\">' +
    '<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">' +
    '<style>' +
    'body{margin:0;background:#f1f1f5;color:#2c2c2e;font-family:-apple-system,BlinkMacSystemFont,\"Helvetica Neue\",Helvetica,Arial,sans-serif;}' +
    '.wrap{padding:14px 14px 24px;}' +
    'h1{margin:2px 0 12px;font-size:18px;font-weight:700;letter-spacing:.2px;color:#000;}' +
    'h3{margin:16px 0 8px;font-size:11px;font-weight:700;letter-spacing:.8px;text-transform:uppercase;color:#6e6e73;}' +
    '.card{background:#fff;border-radius:12px;overflow:hidden;box-shadow:0 1px 0 rgba(0,0,0,.08);}' +
    '.item{display:flex;align-items:center;justify-content:space-between;padding:13px 14px;border-bottom:1px solid #ececf0;font-size:16px;line-height:20px;}' +
    '.card .item:last-child{border-bottom:0;}' +
    '.note{margin:10px 2px 2px;font-size:13px;line-height:18px;color:#6e6e73;}' +
    '.actions{margin-top:18px;}' +
    'button{width:100%;border:0;border-radius:12px;padding:13px 14px;background:#007aff;color:#fff;font-size:16px;font-weight:600;}' +
    '.switch{position:relative;display:inline-block;width:44px;height:27px;}' +
    '.switch input{position:absolute;opacity:0;width:0;height:0;}' +
    '.slider{position:absolute;cursor:pointer;inset:0;background:#d1d1d6;border-radius:14px;transition:.2s;}' +
    '.slider:before{content:\"\";position:absolute;height:23px;width:23px;left:2px;top:2px;background:#fff;border-radius:50%;box-shadow:0 1px 2px rgba(0,0,0,.25);transition:.2s;}' +
    '.switch input:checked + .slider{background:#34c759;}' +
    '.switch input:checked + .slider:before{transform:translateX(17px);}' +
    '</style>' +
    '</head><body>' +
    '<div class=\"wrap\">' +
    '<h1>Nyquist Settings</h1>' +
    (isGabbro ? '' : '<h3>Visible Corner Elements</h3><div class=\"card\">' + sectionCorners + '</div>') +
    gabbroNote +
    '<h3>Colors</h3>' +
    '<div class=\"card\">' +
    '<label class=\"item\"><span>Invert black/white</span><span class=\"switch\"><input id=\"InvertColors\" type=\"checkbox\"><span class=\"slider\"></span></span></label>' +
    '</div>' +
    '<div class=\"actions\"><button id=\"save\">Save</button></div>' +
    '</div>' +
    '<script>' +
    'var isGabbro=' + (isGabbro ? 'true' : 'false') + ';' +
    'var s=' + JSON.stringify(settings) + ';' +
    'if(!isGabbro){document.getElementById(\"ShowTopLeft\").checked=s.ShowTopLeft;document.getElementById(\"ShowTopRight\").checked=s.ShowTopRight;document.getElementById(\"ShowBottomLeft\").checked=s.ShowBottomLeft;document.getElementById(\"ShowBottomRight\").checked=s.ShowBottomRight;}' +
    'document.getElementById(\"InvertColors\").checked=s.InvertColors;' +
    'document.getElementById(\"save\").onclick=function(){' +
      'var out={InvertColors:document.getElementById(\"InvertColors\").checked};' +
      'if(!isGabbro){out.ShowTopLeft=document.getElementById(\"ShowTopLeft\").checked;out.ShowTopRight=document.getElementById(\"ShowTopRight\").checked;out.ShowBottomLeft=document.getElementById(\"ShowBottomLeft\").checked;out.ShowBottomRight=document.getElementById(\"ShowBottomRight\").checked;}' +
      'location.href=\"pebblejs://close#\"+encodeURIComponent(JSON.stringify(out));};' +
    '</script></body></html>';
}

Pebble.addEventListener('ready', function(e) {
  console.log('Nyquist JS ready');
  sendSettingsToWatch(loadSettings());
  weather.updateWeather();
});

Pebble.addEventListener('showConfiguration', function() {
  var platform = getWatchPlatform();
  var isGabbro = platform === 'gabbro';
  var settings = loadSettings();
  var url = 'data:text/html,' + encodeURIComponent(buildConfigPageHtml(isGabbro, settings));
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response) return;
  var data = {};
  try {
    data = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    return;
  }
  var settings = loadSettings();
  if (typeof data.ShowTopLeft === 'boolean') settings.ShowTopLeft = data.ShowTopLeft;
  if (typeof data.ShowTopRight === 'boolean') settings.ShowTopRight = data.ShowTopRight;
  if (typeof data.ShowBottomLeft === 'boolean') settings.ShowBottomLeft = data.ShowBottomLeft;
  if (typeof data.ShowBottomRight === 'boolean') settings.ShowBottomRight = data.ShowBottomRight;
  if (typeof data.InvertColors === 'boolean') settings.InvertColors = data.InvertColors;
  saveSettings(settings);
  sendSettingsToWatch(settings);
});

Pebble.addEventListener('appmessage', function(msg) {
  console.log('Received message, refreshing weather');
  weather.updateWeather();
});
