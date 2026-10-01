#include "signal_classifier.h"

#include <math.h>
#include <string.h>

int scanner_classifier_run(const ScannerClassifierPlugin *plugins, size_t plugin_count,
                           const ScannerDspFeatures *features, ScannerClassification *result)
{
    size_t index;
    ScannerClassification best;
    if (features == NULL || result == NULL || (plugin_count > 0 && plugins == NULL)
        || plugin_count > SCANNER_CLASSIFIER_MAX_PLUGINS)
        return 0;

    best.signal_class = SCANNER_SIGNAL_UNKNOWN;
    best.confidence = 0.0;
    best.classifier_name = "none";
    for (index = 0; index < plugin_count; ++index)
    {
        ScannerClassification candidate;
        if (plugins[index].classify == NULL)
            continue;
        candidate.signal_class = SCANNER_SIGNAL_UNKNOWN;
        candidate.confidence = 0.0;
        candidate.classifier_name = plugins[index].name;
        if (!plugins[index].classify(features, &candidate, plugins[index].context))
            continue;
        if (candidate.signal_class <= SCANNER_SIGNAL_UNKNOWN
            || candidate.signal_class > SCANNER_SIGNAL_WIDEBAND_NOISELIKE || !isfinite(candidate.confidence)
            || candidate.confidence < 0.0 || candidate.confidence > 1.0)
            continue;
        if (candidate.classifier_name == NULL)
            candidate.classifier_name = plugins[index].name;
        if (candidate.confidence > best.confidence)
            best = candidate;
    }
    *result = best;
    return 1;
}

int scanner_classifier_generic(const ScannerDspFeatures *features, ScannerClassification *candidate, void *context)
{
    (void)context;
    if (features == NULL || candidate == NULL)
        return 0;
    candidate->signal_class = SCANNER_SIGNAL_UNKNOWN;
    candidate->confidence = 0.0;
    candidate->classifier_name = "generic-spectrum-rules";

    if (features->peak_above_floor_db >= 12.0 && features->occupied_fraction <= 0.02)
    {
        candidate->signal_class = SCANNER_SIGNAL_NARROWBAND_TONE;
        candidate->confidence = fmin(0.95, 0.55 + features->peak_above_floor_db / 80.0);
        return 1;
    }
    if (features->signal_band_count >= 4 && features->occupied_fraction >= 0.03)
    {
        candidate->signal_class = SCANNER_SIGNAL_MULTICARRIER;
        candidate->confidence = fmin(0.85, 0.5 + features->occupied_fraction);
        return 1;
    }
    if (features->spectral_flatness >= 0.45 && features->occupied_fraction >= 0.15)
    {
        candidate->signal_class = SCANNER_SIGNAL_WIDEBAND_NOISELIKE;
        candidate->confidence = fmin(0.8, 0.45 + features->spectral_flatness / 2.0);
        return 1;
    }
    return 1;
}

const char *scanner_signal_class_name(ScannerSignalClass signal_class)
{
    switch (signal_class)
    {
    case SCANNER_SIGNAL_NARROWBAND_TONE:
        return "narrowband-tone";
    case SCANNER_SIGNAL_MULTICARRIER:
        return "multicarrier-like";
    case SCANNER_SIGNAL_WIDEBAND_NOISELIKE:
        return "wideband-noiselike";
    case SCANNER_SIGNAL_UNKNOWN:
    default:
        return "unknown";
    }
}
