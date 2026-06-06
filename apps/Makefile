# Top-level apps Makefile — a pure builder. Device ops (install, run, trace) all
# go through `start` now; this just compiles.
#
# Targets:
#   make all        — build every app
#   make <app>      — build one app (e.g. 'make clock')
#   make clean      — clean every app
#
# Deploy/run with start (auto-detects local vs Mac+SSH transport):
#   ../start install           install every app + resident to the iPod
#   ../start install clock     install just one app (+resident)
#   ../start run <target>      build + SCSI-exec one app/harness once

# Detect every subdir that contains a Makefile. New app == new dir.
# Skip dirs starting with '_' (deploy helpers, not user-facing apps).
ALL_DIRS := $(notdir $(patsubst %/Makefile,%, $(wildcard */Makefile)))
APPS     := $(filter-out _%, $(ALL_DIRS))
HELPERS  := $(filter     _%, $(ALL_DIRS))

.PHONY: all clean help helpers $(APPS) $(HELPERS)

all: $(APPS)

helpers: $(HELPERS)

$(APPS) $(HELPERS):
	@$(MAKE) -s -C $@

clean:
	@for app in $(APPS) $(HELPERS); do $(MAKE) -s -C $$app clean; done

help:
	@echo "Build:"
	@echo "  all              build every app"
	@echo "  <app>            build one app (e.g. 'make clock')"
	@echo "  clean            clean every app"
	@echo ""
	@echo "Device ops go through start (from the repo root):"
	@echo "  ../start install [app]   install all apps (or one) + resident"
	@echo "  ../start run <target>    build + SCSI-exec one app/harness once"
	@echo "  ../start trace           dump the resident trace ring"
