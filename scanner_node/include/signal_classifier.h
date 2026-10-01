#ifndef SCANNER_NODE_SIGNAL_CLASSIFIER_H
#define SCANNER_NODE_SIGNAL_CLASSIFIER_H

#include "dsp.h"

#include <stddef.h>

#define SCANNER_CLASSIFIER_MAX_PLUGINS 16u

typedef int (*ScannerSignalClassifierFn)(const ScannerDspFeatures *features, ScannerClassification *candidate,
                                         void *context);

typedef struct
{
    const char *name;
    ScannerSignalClassifierFn classify;
    void *context;
} ScannerClassifierPlugin;

/* Run registered classifiers and keep the recognized candidate with highest confidence. */
int scanner_classifier_run(const ScannerClassifierPlugin *plugins, size_t plugin_count,
                           const ScannerDspFeatures *features, ScannerClassification *result);

/* Conservative generic feature rules; unsupported waveforms remain UNKNOWN. */
int scanner_classifier_generic(const ScannerDspFeatures *features, ScannerClassification *candidate, void *context);

const char *scanner_signal_class_name(ScannerSignalClass signal_class);

#endif
