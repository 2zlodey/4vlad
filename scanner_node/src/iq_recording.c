#include "iq_recording.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define IQ_RECORD_FILE_MAGIC "SCIQREC1"
#define IQ_RECORD_FILE_MAGIC_SIZE 8u
#define IQ_RECORD_HEADER_SIZE 40u
#define IQ_RECORD_MAGIC "IQRF"

static void write_u32_le(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void write_u64_le(uint8_t *output, uint64_t value)
{
    unsigned int index;
    for (index = 0; index < 8u; ++index)
        output[index] = (uint8_t)(value >> (index * 8u));
}

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0)
        snprintf(error, error_size, "%s", message);
}

int scanner_iq_recording_open(const char *path, FILE **file, char *error, size_t error_size)
{
    FILE *stream;
    long file_size;
    char magic[IQ_RECORD_FILE_MAGIC_SIZE];
    if (path == NULL || path[0] == '\0' || file == NULL)
    {
        set_error(error, error_size, "Invalid IQ recording path");
        return 0;
    }
    *file = NULL;
    stream = fopen(path, "ab+");
    if (stream == NULL)
    {
        set_error(error, error_size, "Cannot open IQ recording file");
        return 0;
    }
    if (fseek(stream, 0, SEEK_END) != 0 || (file_size = ftell(stream)) < 0)
    {
        fclose(stream);
        set_error(error, error_size, "Cannot inspect IQ recording file");
        return 0;
    }
    if (file_size == 0)
    {
        if (fwrite(IQ_RECORD_FILE_MAGIC, 1, IQ_RECORD_FILE_MAGIC_SIZE, stream) != IQ_RECORD_FILE_MAGIC_SIZE)
        {
            fclose(stream);
            set_error(error, error_size, "Cannot write IQ recording header");
            return 0;
        }
    }
    else
    {
        if (file_size < (long)IQ_RECORD_FILE_MAGIC_SIZE || fseek(stream, 0, SEEK_SET) != 0
            || fread(magic, 1, IQ_RECORD_FILE_MAGIC_SIZE, stream) != IQ_RECORD_FILE_MAGIC_SIZE
            || memcmp(magic, IQ_RECORD_FILE_MAGIC, IQ_RECORD_FILE_MAGIC_SIZE) != 0 || fseek(stream, 0, SEEK_END) != 0)
        {
            fclose(stream);
            set_error(error, error_size, "Existing file is not a supported SCIQREC1 recording");
            return 0;
        }
    }
    *file = stream;
    return 1;
}

int scanner_iq_recording_write(FILE *file, const ScannerRadioFrontend *frontend, uint8_t channel,
                               uint16_t complex_samples, uint8_t sample_format, const uint8_t *iq, size_t iq_size,
                               char *error, size_t error_size)
{
    uint8_t header[IQ_RECORD_HEADER_SIZE] = { 0 };
    size_t bytes_per_sample;
    size_t expected_size;
    if (file == NULL || frontend == NULL || iq == NULL || complex_samples == 0
        || (sample_format != SCANNER_RADIO_IQ_FORMAT_S8 && sample_format != SCANNER_RADIO_IQ_FORMAT_S16))
    {
        set_error(error, error_size, "Invalid IQ recording entry");
        return 0;
    }
    bytes_per_sample = sample_format == SCANNER_RADIO_IQ_FORMAT_S8 ? 2u : 4u;
    expected_size = (size_t)complex_samples * bytes_per_sample;
    if (iq_size != expected_size || !frontend->frequency_configured || !frontend->sample_rate_configured
        || !frontend->bandwidth_configured)
    {
        set_error(error, error_size, "IQ recording metadata does not match captured samples");
        return 0;
    }

    memcpy(header, IQ_RECORD_MAGIC, 4u);
    header[4] = (uint8_t)frontend->backend;
    header[5] = channel;
    header[6] = sample_format;
    write_u64_le(header + 8u, frontend->configured_frequency_hz);
    write_u32_le(header + 16u, frontend->configured_sample_rate_hz);
    write_u32_le(header + 20u, frontend->configured_bandwidth_hz);
    write_u32_le(header + 24u, complex_samples);
    write_u32_le(header + 28u, (uint32_t)iq_size);
    write_u64_le(header + 32u, (uint64_t)time(NULL));

    if (fwrite(header, 1, sizeof(header), file) != sizeof(header) || fwrite(iq, 1, iq_size, file) != iq_size
        || fflush(file) != 0)
    {
        set_error(error, error_size, "Failed writing IQ capture record");
        return 0;
    }
    return 1;
}

void scanner_iq_recording_close(FILE **file)
{
    if (file == NULL || *file == NULL)
        return;
    (void)fclose(*file);
    *file = NULL;
}
