# Resident Silver hook module — links at the proven resident region
# 0x09130000 (clear of the app load VA 0x09280000 and the trace buffer
# 0x09120000) so it persists while iteration probes load at 0x09280000.
APP_NAME := silver_resident
SRCS     := silver_resident.c
LINK_VA  := 0x09130000
include ../../sdk/hb_app.mk
