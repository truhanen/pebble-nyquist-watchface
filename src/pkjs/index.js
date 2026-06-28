var weather = require('./weather');
var Clay = require('pebble-clay');
var clayConfig = require('./config');
new Clay(clayConfig);

Pebble.addEventListener('ready', function(e) {
  console.log('Nyquist JS ready');
  weather.updateWeather();
});

Pebble.addEventListener('appmessage', function(msg) {
  console.log('Received message, refreshing weather');
  weather.updateWeather();
});
