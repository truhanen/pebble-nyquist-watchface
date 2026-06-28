# Nyquist

Pebble analog watchface with bold geometric hands, weather, battery, and date.

### Makefile

The project uses a _Makefile_ for common routines (build, install, emulator logs,
and screenshot capture helpers for Emery and Gabbro).

### Platforms

The watchface targets:

- **emery** (Pebble Time 2)
- **gabbro** (Pebble Round 2)

On Gabbro, corner elements are intentionally never shown so the clock fills the
round display cleanly.

### Configuration

Settings are managed from the Pebble phone app config page. Available options:

- show/hide corner elements (Emery only)
- invert black/white colors
- time format: **24h** or **12h am/pm** (rendered as `h:mmam` / `h:mmpm`)
- temperature unit: **Celsius** or **Fahrenheit**

### Weather data

Phone-side JavaScript fetches weather data and sends updates to the watch via
AppMessage. The watchface displays current temperature and weather icon when
available.

### Build & run

```sh
pebble build
pebble install --emulator emery
pebble install --emulator gabbro
```
