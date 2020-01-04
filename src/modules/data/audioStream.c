#include "data/audioStream.h"
#include "data/blob.h"
#include "data/soundData.h"
#include "core/ref.h"
#include "core/util.h"
#include "lib/stb/stb_vorbis.h"
#include <stdlib.h>
#include <memory.h>

AudioStream* lovrAudioStreamInit(AudioStream* stream, Blob* blob, size_t bufferSize) {
  stb_vorbis* decoder = stb_vorbis_open_memory(blob->data, (int) blob->size, NULL, NULL);
  lovrAssert(decoder, "Could not create audio stream for '%s'", blob->name);

  stb_vorbis_info info = stb_vorbis_get_info(decoder);

  stream->bitDepth = 16;
  stream->channelCount = info.channels;
  stream->sampleRate = info.sample_rate;
  stream->samples = stb_vorbis_stream_length_in_samples(decoder);
  stream->decoder = decoder;
  stream->bufferSize = stream->channelCount * bufferSize * sizeof(int16_t);
  stream->buffer = malloc(stream->bufferSize);
  lovrAssert(stream->buffer, "Out of memory");
  stream->blob = blob;
  lovrRetain(blob);
  return stream;
}

AudioStream* lovrAudioStreamInitRaw(AudioStream* stream, int channelCount, int sampleRate, size_t bufferSize, size_t queueLimitInSamples) {
  stream->bitDepth = 16;
  stream->channelCount = channelCount;
  stream->sampleRate = sampleRate;
  stream->samples = SIZE_MAX;
  stream->decoder = NULL;
  stream->bufferSize = stream->channelCount * bufferSize * sizeof(int16_t);
  stream->buffer = malloc(stream->bufferSize);
  lovrAssert(stream->buffer, "Out of memory");
  stream->blob = NULL;
  arr_init(&stream->queuedRawBuffers);
  stream->queueLengthInSamples = 0;
  stream->queueLimitInSamples = queueLimitInSamples;
  return stream;
}

void lovrAudioStreamDestroy(void* ref) {
  AudioStream* stream = ref;
  stb_vorbis_close(stream->decoder);
  lovrRelease(Blob, stream->blob);
  free(stream->buffer);
}

static size_t dequeue_raw(AudioStream* stream, int16_t* destination, size_t sampleCount)
{
  if (stream->queuedRawBuffers.length == 0) {
    return 0;
  }
  size_t byteCount = sampleCount * sizeof(int16_t);
  Blob* blob = stream->queuedRawBuffers.data[0];
  if (blob->size <= byteCount) {
    size_t blobSize = blob->size;
    // blob fits in destination in its entirety. Copy over, free it and remove from start of array.
    memcpy(destination, blob->data, blobSize);
    lovrRelease(Blob, blob);
    arr_splice(&stream->queuedRawBuffers, 0, 1);
    return blobSize / sizeof(int16_t);
  } else {
    // blob is too big. copy all that fits, and put remainder in a new blob in its place.
    memcpy(destination, blob->data, byteCount);
    void* oldData = blob->data;
    size_t remainingLength = blob->size - byteCount;
    void* copiedRemainder = malloc(remainingLength);
    memcpy(copiedRemainder, (char*)blob->data + byteCount, remainingLength);
    lovrBlobInit(blob, copiedRemainder, remainingLength, "raw audiostream remainder");
    free(oldData);
    return sampleCount;
  }
}

size_t lovrAudioStreamDecode(AudioStream* stream, int16_t* destination, size_t size) {
  stb_vorbis* decoder = (stb_vorbis*) stream->decoder;
  int16_t* buffer = destination ? destination : (int16_t*) stream->buffer;
  size_t capacity = destination ? size : (stream->bufferSize / sizeof(int16_t));
  uint32_t channelCount = stream->channelCount;
  size_t samples = 0;

  while (samples < capacity) {
    size_t count = 0;
    if (decoder) {
      count = stb_vorbis_get_samples_short_interleaved(decoder, channelCount, buffer + samples, (int)(capacity - samples));
    } else {
      count = dequeue_raw(stream, buffer + samples, (int)(capacity - samples));
      stream->queueLengthInSamples -= count;
    }
    if (count == 0) break;
    samples += count * channelCount;
  }

  return samples;
}

bool lovrAudioStreamAppendRawBlob(AudioStream* stream, struct Blob* blob)
{
  lovrAssert(lovrAudioStreamIsRaw(stream), "Raw PCM data can only be appended to a raw AudioStream (see constructor that takes channel count and sample rate)")
  if (stream->queueLimitInSamples != 0 && stream->queueLengthInSamples + blob->size/sizeof(int16_t) >= stream->queueLimitInSamples) {
    return false;
  }
  lovrRetain(blob);
  arr_push(&stream->queuedRawBuffers, blob);
  stream->queueLengthInSamples += blob->size / sizeof(int16_t);
  return true;
}

bool lovrAudioStreamAppendRawSound(AudioStream* stream, struct SoundData* sound)
{
  lovrAssert(sound->channelCount == stream->channelCount && sound->bitDepth == stream->bitDepth && sound->sampleRate == stream->sampleRate, "SoundData and AudioStream formats must match");
  lovrAudioStreamAppendRawBlob(stream, &sound->blob);
  return true;
}

size_t lovrAudioStreamGetQueueLength(AudioStream* stream)
{
  lovrAssert(lovrAudioStreamIsRaw(stream), "Queue length is only available on raw streams");
  return stream->queueLengthInSamples;
}

bool lovrAudioStreamIsRaw(AudioStream* stream)
{
  return stream->decoder == NULL;
}

void lovrAudioStreamRewind(AudioStream* stream) {
  lovrAssert(!lovrAudioStreamIsRaw(stream), "Can't rewind raw stream");
  stb_vorbis* decoder = (stb_vorbis*) stream->decoder;
  stb_vorbis_seek_start(decoder);
}

void lovrAudioStreamSeek(AudioStream* stream, size_t sample) {
  lovrAssert(!lovrAudioStreamIsRaw(stream), "Can't seek raw stream");
  stb_vorbis* decoder = (stb_vorbis*) stream->decoder;
  stb_vorbis_seek(decoder, (int) sample);
}

size_t lovrAudioStreamTell(AudioStream* stream) {
  lovrAssert(!lovrAudioStreamIsRaw(stream), "No position available in raw stream");
  stb_vorbis* decoder = (stb_vorbis*) stream->decoder;
  return stb_vorbis_get_sample_offset(decoder);
}
