# Tape-Top — convenience targets from the repo root, so nobody has to remember
# which subdirectory holds what.
#
#   make test     build and run every DSP/recorder assertion (no dependencies)
#   make render   render the four tape stocks + the transport pass to ui/audio/
#   make ui       serve the panel prototype on :8080
#   make plugin   build the JUCE plugin (macOS; needs Xcode CLT + network once)

.PHONY: all test render ui plugin clean

all: test

test:
	$(MAKE) -C dsp test

render:
	$(MAKE) -C dsp render

ui: render
	@echo "panel: http://localhost:8080"
	cd ui && python3 -m http.server 8080

plugin:
	./scripts/build-macos.sh

clean:
	$(MAKE) -C dsp clean
	rm -rf plugin/build
