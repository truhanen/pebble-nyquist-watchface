var weatherCommon = require('./weather');

module.exports.getWeatherFromCoords = getWeatherFromCoords;

var FMI_WFS_BASE = 'https://opendata.fmi.fi/wfs?service=WFS&version=2.0.0&request=getFeature';

function getWeatherFromCoords(pos, onFailure) {
  var lat = pos.coords.latitude;
  var lon = pos.coords.longitude;
  var latlon = lat + ',' + lon;

  var now = new Date();

  // Edited (meteorologist's) forecast, hourly, for today's full local calendar day.
  // This single request covers current conditions, today's high, and today's low.
  var todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate(),  0, 0, 0);
  var todayEnd   = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 23, 0, 0);

  var editedUrl = FMI_WFS_BASE +
    '&storedquery_id=fmi::forecast::edited::weather::scandinavia::point::simple' +
    '&latlon=' + latlon +
    '&parameters=Temperature,SmartSymbol' +
    '&timestep=60' +
    '&starttime=' + toISOString(todayStart) +
    '&endtime='   + toISOString(todayEnd);

  console.log('FMI edited URL: ' + editedUrl);

  weatherCommon.xhrRequest(editedUrl, 'GET', function(editedXml) {
    var ok = processWeatherData(editedXml, now);
    if (!ok && onFailure) {
      onFailure();
    }
  }, function(err) {
    console.log('FMI request failed: ' + err);
    if (onFailure) {
      onFailure();
    }
  });
}

// ── XML parsing ──────────────────────────────────────────────────────────────

function parseWfsXml(xml) {
  var results = [];
  var elementRegex = /<BsWfs:BsWfsElement[^>]*>([\s\S]*?)<\/BsWfs:BsWfsElement>/g;
  var timeRegex    = /<BsWfs:Time>(.*?)<\/BsWfs:Time>/;
  var paramRegex   = /<BsWfs:ParameterName>(.*?)<\/BsWfs:ParameterName>/;
  var valueRegex   = /<BsWfs:ParameterValue>(.*?)<\/BsWfs:ParameterValue>/;

  var block;
  while ((block = elementRegex.exec(xml)) !== null) {
    var inner      = block[1];
    var timeMatch  = timeRegex.exec(inner);
    var paramMatch = paramRegex.exec(inner);
    var valueMatch = valueRegex.exec(inner);

    if (timeMatch && paramMatch && valueMatch) {
      results.push({
        time:  new Date(timeMatch[1]),
        param: paramMatch[1],
        value: parseFloat(valueMatch[1])
      });
    }
  }
  return results;
}

// ── Core processing ──────────────────────────────────────────────────────────

function processWeatherData(editedXml, now) {
  var editedData = parseWfsXml(editedXml);

  // Current temperature and icon: closest timestep to now
  var editedTemps   = editedData.filter(function(e) { return e.param === 'Temperature'; });
  var editedSymbols = editedData.filter(function(e) { return e.param === 'SmartSymbol'; });

  if (editedTemps.length === 0 || editedSymbols.length === 0) {
    console.log('FMI parse failed: missing temperature or symbol data');
    return false;
  }

  var closestTemp = findClosest(editedTemps, now);
  var currentTemp = closestTemp ? Math.round(closestTemp.value) : 0;

  var currentSymbolEntry = findClosest(editedSymbols, now);
  var weatherSymbol = currentSymbolEntry ? Math.round(currentSymbolEntry.value) : 0;

  // SmartSymbol night variants have +100; use that for night detection on the current icon.
  var isNight = weatherSymbol >= 100;
  var currentIcon = getIconForSmartSymbol(weatherSymbol);

  // Day high/low: min/max across all hourly edited forecast entries for today
  var forecastHigh = currentTemp;
  var forecastLow  = currentTemp;
  if (editedTemps.length > 0) {
    forecastHigh = -Infinity;
    forecastLow  =  Infinity;
    editedTemps.forEach(function(e) {
      if (e.value > forecastHigh) { forecastHigh = e.value; }
      if (e.value < forecastLow)  { forecastLow  = e.value; }
    });
    forecastHigh = Math.round(forecastHigh);
    forecastLow  = Math.round(forecastLow);
  }

  // Forecast icon: most common SmartSymbol across all 24 hourly entries today,
  // but if the total number of rainy hours exceeds that count, use the most
  // common rain condition instead.
  var symbolCounts = {};
  var rainCounts = {};
  var totalRainHours = 0;
  editedSymbols.forEach(function(e) {
    var s = Math.round(e.value);
    symbolCounts[s] = (symbolCounts[s] || 0) + 1;
    if (isRainSymbol(s)) {
      rainCounts[s] = (rainCounts[s] || 0) + 1;
      totalRainHours++;
    }
  });

  var modalSymbol = weatherSymbol;
  var maxCount = 0;
  Object.keys(symbolCounts).forEach(function(s) {
    if (symbolCounts[s] > maxCount) { maxCount = symbolCounts[s]; modalSymbol = parseInt(s); }
  });

  if (totalRainHours > maxCount) {
    var modalRainSymbol = weatherSymbol;
    var maxRainCount = 0;
    Object.keys(rainCounts).forEach(function(s) {
      if (rainCounts[s] > maxRainCount) { maxRainCount = rainCounts[s]; modalRainSymbol = parseInt(s); }
    });
    modalSymbol = modalRainSymbol;
  }

  var forecastIcon = getIconForSmartSymbol(modalSymbol);

  console.log('FMI current temp: ' + currentTemp + ', symbol: ' + weatherSymbol + ', isNight: ' + isNight);
  console.log('FMI forecast high/low: ' + forecastHigh + '/' + forecastLow);

  var dictionary = {
    'WeatherTemperature':      currentTemp,
    'WeatherCondition':        currentIcon,
    'WeatherForecastHighTemp': forecastHigh,
    'WeatherForecastLowTemp':  forecastLow,
    'WeatherForecastCondition': forecastIcon,
    'WeatherUVIndex':          99
  };

  console.log(JSON.stringify(dictionary));
  weatherCommon.sendWeatherToPebble(dictionary);
  return true;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

function findClosest(entries, targetTime) {
  if (!entries || entries.length === 0) { return null; }
  var best     = entries[0];
  var bestDiff = Math.abs(entries[0].time - targetTime);
  for (var i = 1; i < entries.length; i++) {
    var diff = Math.abs(entries[i].time - targetTime);
    if (diff < bestDiff) { bestDiff = diff; best = entries[i]; }
  }
  return best;
}

function toISOString(date) {
  function pad(n) { return n < 10 ? '0' + n : '' + n; }
  return date.getUTCFullYear()      + '-' +
         pad(date.getUTCMonth() + 1) + '-' +
         pad(date.getUTCDate())       + 'T' +
         pad(date.getUTCHours())      + ':' +
         pad(date.getUTCMinutes())    + ':' +
         pad(date.getUTCSeconds())    + 'Z';
}

// Returns true if a SmartSymbol code represents any rain-type condition.
function isRainSymbol(sym) {
  var s = Math.round(sym) % 100;
  return s === 11 || s === 14 || s === 17 ||
         s === 21 || s === 24 || s === 27 ||
         (s >= 31 && s <= 39) ||
         (s >= 41 && s <= 49);
}

// FMI SmartSymbol → app icon.
// Night variants have +100 added; strip with % 100 to get the base code.
// Reference: https://github.com/sevesalm/eInk-weather-display/blob/master/weather_icon_codes.md
function getIconForSmartSymbol(sym) {
  var isNight = sym >= 100;
  var s = Math.round(sym) % 100;
  switch (s) {
    case 1:
    case 2:
      return isNight ? weatherCommon.icons.CLEAR_NIGHT : weatherCommon.icons.CLEAR_DAY;
    case 4:
      return isNight ? weatherCommon.icons.PARTLY_CLOUDY_NIGHT : weatherCommon.icons.PARTLY_CLOUDY;
    case 6:
    case 7:
      return weatherCommon.icons.CLOUDY_DAY;
    case 9:   // fog
      return weatherCommon.icons.CLOUDY_DAY;
    case 11:  // drizzle
    case 14:  // freezing drizzle
    case 17:  // freezing rain
    case 21:  // light rain showers
    case 24:  // sleet showers
    case 31:  // light rain
    case 32:  // rain
    case 34:  // light sleet
    case 35:  // sleet
      return weatherCommon.icons.LIGHT_RAIN;
    case 27:  // heavy rain showers
    case 33:  // heavy rain
    case 36:  // heavy sleet
    case 38:  // heavy rain (cont.)
    case 39:
      return weatherCommon.icons.HEAVY_RAIN;
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:  // sleet variants → mixed
      return weatherCommon.icons.RAINING_AND_SNOWING;
    case 51:
    case 52:
    case 54:
    case 55:  // light/moderate snow
      return weatherCommon.icons.LIGHT_SNOW;
    case 53:
    case 56:
    case 57:
    case 58:
    case 59:  // heavy snow
      return weatherCommon.icons.HEAVY_SNOW;
    case 61:
    case 64:
    case 67:
    case 71:
    case 74:
    case 77:  // thunderstorms
      return weatherCommon.icons.THUNDERSTORM;
    default:
      return weatherCommon.icons.WEATHER_GENERIC;
  }
}
