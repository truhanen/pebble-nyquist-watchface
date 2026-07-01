.PHONY: build
build:
	pebble build && pebble build

.PHONY: install_cloudpebble
install_cloudpebble:
	pebble install --cloudpebble

.PHONY: install_emulator_emery
install_emulator_emery:
	pebble install --emulator=emery

.PHONY: install_emulator_gabbro
install_emulator_gabbro:
	pebble install --emulator=gabbro

.PHONY: build_and_install_emulator_emery
build_and_install_emulator_emery: build install_emulator_emery

.PHONY: build_and_install_emulator_gabbro
build_and_install_emulator_gabbro: build install_emulator_gabbro

.PHONY: build_and_install_cloudpebble
build_and_install_cloudpebble: build install_cloudpebble

.PHONY: kill_emulator
kill_emulator:
	-pebble kill
	pebble wipe

.PHONY: start_emulator_with_logs_emery
start_emulator_with_logs_emery: kill_emulator
	pebble logs --emulator=emery

.PHONY: start_emulator_with_logs_gabbro
start_emulator_with_logs_gabbro: kill_emulator
	pebble logs --emulator=gabbro

.PHONY: create_minute_screenshots_emery
create_minute_screenshots_emery: kill_emulator build install_emulator_emery
	./scripts/create_minute_screenshots.sh --emulator emery

.PHONY: create_minute_screenshots_gabbro
create_minute_screenshots_gabbro: kill_emulator build install_emulator_gabbro
	./scripts/create_minute_screenshots.sh --emulator gabbro

.PHONY: create_hand_coordinates
create_hand_coordinates:
	uv run scripts/create_hand_coordinates.py --platform=gabbro

