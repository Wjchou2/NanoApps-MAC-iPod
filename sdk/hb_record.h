/*
 * hb_record.h — system-wide screen recorder.
 */
#ifndef HB_RECORD_H
#define HB_RECORD_H

#include <stdint.h>

/* 1 while a recording is in progress. */
int hb_record_active(void);

/* Begin recording: size a ring from the OS heap, capture the keyframe. Returns 1
 * on success, 0 if it couldn't start (no memory / unsupported display format). */
int hb_record_start(void);

/* Capture + delta-encode one frame. Auto-stops (and flushes) when the ring fills.
 * Cheap enough to call from the capture timer (~30 Hz). */
void hb_record_tick(void);

/* Stop + flush frames to /recNNNN/frameNNNN.bmp, free the ring. Returns the number
 * of frames written. No-op (returns 0) if not recording. */
int hb_record_stop(void);

#endif /* HB_RECORD_H */
