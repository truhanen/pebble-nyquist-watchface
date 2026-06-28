.PHONY: build
build:
	pebble build && pebble build

.PHONY: install_cloudpebble
install_cloudpebble:
	pebble install --cloudpebble

.PHONY: install_emulator
install_emulator:
	pebble install --emulator=emery

.PHONY: build_and_install_emulator
build_and_install_emulator: build install_emulator

.PHONY: build_and_install_cloudpebble
build_and_install_cloudpebble: build install_cloudpebble

.PHONY: kill_emulator
kill_emulator:
	-pebble kill
	pebble wipe

.PHONY: start_emulator_with_logs
start_emulator_with_logs: kill_emulator
	pebble logs --emulator=emery

.PHONY: create_minute_screenshots
create_minute_screenshots: kill_emulator build install_emulator
	./scripts/capture-0000-0059.sh

