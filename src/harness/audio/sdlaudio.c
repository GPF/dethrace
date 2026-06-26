#include "harness/audio.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_hints.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CDA_AUDIO_SAMPLES 4096
#define AUDIOBACKEND_MAX_VOLUME 128

struct tAudioBackend_stream_impl {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    Uint8* owned_data;
    Uint32 owned_len;
    int volume;
    int pan;
    int frequency;
    int initialized;
    int looping;
#ifdef __DREAMCAST__
    Uint32 play_end_ticks;
    Uint32 play_duration_ticks;
    int dreamcast_adpcm_sample;
#endif
};

typedef struct tAudioBackend_stream_impl tAudioBackend_stream_impl;

#ifdef __DREAMCAST__
static tAudioBackend_stream_impl g_dreamcast_smacker_stream;
static int g_dreamcast_smacker_stream_allocated;
static int g_cda_suspended;
#define DREAMCAST_SMACKER_MAX_QUEUED_AUDIO (128 * 1024)

extern int SDL_DreamcastQueueADPCMSfx(const void* data, Uint32 len);

static int AudioBackend_ReinitDreamcastAudio(const char* adpcm_hint) {
    if (SDL_WasInit(SDL_INIT_AUDIO) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    SDL_SetHint(SDL_HINT_AUDIO_ADPCM_STREAM_DC, adpcm_hint);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        printf("Dreamcast: SDL audio reinit failed (hint=%s): %s\n", adpcm_hint, SDL_GetError());
        return 0;
    }

    return 1;
}
#endif

static struct {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    SDL_RWops* rw;
    Sint64 data_start;
    Uint8 ring[256 * 1024];
    volatile Uint32 ring_read;
    volatile Uint32 ring_write;
    Uint32 len;
    int eof;
    int playing;
    int volume;
} cda;

static Uint32 AudioBackend_ReadLE32(const Uint8* ptr) {
    return ((Uint32)ptr[0]) | ((Uint32)ptr[1] << 8) | ((Uint32)ptr[2] << 16) | ((Uint32)ptr[3] << 24);
}

static Uint16 AudioBackend_ReadLE16(const Uint8* ptr) {
    return (Uint16)(((Uint16)ptr[0]) | ((Uint16)ptr[1] << 8));
}

static SDL_INLINE Uint32 AudioBackend_CDARingFree(void) {
    return (Uint32)sizeof(cda.ring) - (cda.ring_write - cda.ring_read);
}

static void AudioBackend_PumpCDA(void) {
    if (cda.rw == NULL || cda.eof) {
        return;
    }

    while (AudioBackend_CDARingFree() >= 4096u) {
        Uint32 wpos = cda.ring_write % (Uint32)sizeof(cda.ring);
        Uint32 chunk = SDL_min(4096u, (Uint32)sizeof(cda.ring) - wpos);
        size_t got = SDL_RWread(cda.rw, cda.ring + wpos, 1, chunk);

        if (got == 0) {
            if (SDL_RWseek(cda.rw, cda.data_start, RW_SEEK_SET) < 0) {
                cda.eof = 1;
                return;
            }
            continue;
        }

        SDL_MemoryBarrierRelease();
        cda.ring_write += (Uint32)got;
    }
}

void AudioBackend_ServiceCDA(void) {
    AudioBackend_PumpCDA();
}

static void AudioBackend_CloseQueuedDevice(tAudioBackend_stream_impl* stream) {
    if (stream->device != 0) {
        SDL_ClearQueuedAudio(stream->device);
        SDL_CloseAudioDevice(stream->device);
        stream->device = 0;
    }
    if (stream->owned_data != NULL) {
#ifdef __DREAMCAST__
        if (stream->dreamcast_adpcm_sample) {
            SDL_FreeWAV(stream->owned_data);
        } else
#endif
        {
            SDL_free(stream->owned_data);
        }
        stream->owned_data = NULL;
        stream->owned_len = 0;
    }
    stream->initialized = 0;
    stream->looping = 0;
#ifdef __DREAMCAST__
    stream->play_end_ticks = 0;
    stream->play_duration_ticks = 0;
    stream->dreamcast_adpcm_sample = 0;
#endif
}

#ifdef __DREAMCAST__
static void AudioBackend_WriteLE16(Uint8* ptr, Uint16 value) {
    ptr[0] = (Uint8)(value & 0xff);
    ptr[1] = (Uint8)(value >> 8);
}

static void AudioBackend_WriteLE32(Uint8* ptr, Uint32 value) {
    ptr[0] = (Uint8)(value & 0xff);
    ptr[1] = (Uint8)((value >> 8) & 0xff);
    ptr[2] = (Uint8)((value >> 16) & 0xff);
    ptr[3] = (Uint8)(value >> 24);
}

static int AudioBackend_LoadDreamcastADPCMSample(tAudioBackend_stream_impl* stream, int channels, const Uint8* data, Uint32 size, int rate) {
    Uint8* wav_data;
    SDL_RWops* rw;
    SDL_AudioSpec spec;
    Uint32 loaded_len;
    Uint32 byte_rate;
    Uint16 block_align;

    wav_data = (Uint8*)SDL_malloc(size + 44u);
    if (wav_data == NULL) {
        return 0;
    }

    SDL_memset(wav_data, 0, 44u);
    SDL_memcpy(&wav_data[0], "RIFF", 4);
    AudioBackend_WriteLE32(&wav_data[4], size + 36u);
    SDL_memcpy(&wav_data[8], "WAVE", 4);
    SDL_memcpy(&wav_data[12], "fmt ", 4);
    AudioBackend_WriteLE32(&wav_data[16], 16u);
    AudioBackend_WriteLE16(&wav_data[20], 0x0020u);
    AudioBackend_WriteLE16(&wav_data[22], (Uint16)channels);
    AudioBackend_WriteLE32(&wav_data[24], (Uint32)rate);
    byte_rate = (Uint32)((rate > 0 ? rate : 22050) * (channels > 0 ? channels : 1) / 2);
    block_align = (Uint16)((channels > 0 ? channels : 1) / 2);
    if (block_align == 0) {
        block_align = 1;
    }
    AudioBackend_WriteLE32(&wav_data[28], byte_rate);
    AudioBackend_WriteLE16(&wav_data[32], block_align);
    AudioBackend_WriteLE16(&wav_data[34], 4u);
    SDL_memcpy(&wav_data[36], "data", 4);
    AudioBackend_WriteLE32(&wav_data[40], size);
    SDL_memcpy(&wav_data[44], data, size);

    rw = SDL_RWFromMem(wav_data, (int)(size + 44u));
    if (rw == NULL) {
        SDL_free(wav_data);
        return 0;
    }

    stream->owned_data = NULL;
    stream->owned_len = 0;
    loaded_len = 0;
    if (SDL_LoadDreamcastADPCM_RW(rw, 1, &spec, &stream->owned_data, &loaded_len) == NULL) {
        SDL_free(wav_data);
        return 0;
    }

    SDL_free(wav_data);
    stream->owned_len = loaded_len;
    stream->spec = spec;
    stream->dreamcast_adpcm_sample = 1;
    return 1;
}
#endif

static int AudioBackend_OpenAndQueue(tAudioBackend_stream_impl* stream, const SDL_AudioSpec* spec, const Uint8* data, Uint32 size) {
    stream->spec = *spec;
    stream->device = SDL_OpenAudioDevice(NULL, SDL_FALSE, &stream->spec, NULL, 0);
    if (stream->device == 0) {
        printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return 0;
    }
    if (SDL_QueueAudio(stream->device, data, size) < 0) {
        printf("SDL_QueueAudio failed: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(stream->device);
        stream->device = 0;
        return 0;
    }
    SDL_PauseAudioDevice(stream->device, 0);
    stream->initialized = 1;
    return 1;
}

static int AudioBackend_OpenDreamcastADPCMStream(const char* filename, SDL_AudioSpec* spec, SDL_RWops** audio_rw, Uint32* audio_len) {
    SDL_RWops* rw;
    Uint8 riff_header[12];
    Uint8 chunk_header[8];
    Uint8 fmt_data[64];
    Uint32 sample_rate = 0;
    Uint16 channels = 0;
    Uint16 audio_format = 0;
    Uint16 bits_per_sample = 0;
    Uint32 data_size = 0;
    Sint64 data_offset = -1;
    Sint64 file_size;

    if (audio_rw == NULL || audio_len == NULL || spec == NULL) {
        SDL_InvalidParamError("audio_rw/audio_len/spec");
        return -1;
    }

    *audio_rw = NULL;
    *audio_len = 0;

    rw = SDL_RWFromFile(filename, "rb");
    if (rw == NULL) {
        return -1;
    }

    if (SDL_RWread(rw, riff_header, sizeof(riff_header), 1) != 1) {
        SDL_SetError("Failed to read WAV header");
        SDL_RWclose(rw);
        return -1;
    }

    if (SDL_memcmp(riff_header, "RIFF", 4) != 0 || SDL_memcmp(&riff_header[8], "WAVE", 4) != 0) {
        SDL_SetError("Not a RIFF/WAVE file");
        SDL_RWclose(rw);
        return -1;
    }

    file_size = SDL_RWsize(rw);
    if (file_size < 12) {
        SDL_SetError("WAV file is too small");
        SDL_RWclose(rw);
        return -1;
    }

    while (SDL_RWtell(rw) + 8 <= file_size) {
        Sint64 chunk_pos = SDL_RWtell(rw);
        Uint32 chunk_size;

        if (SDL_RWread(rw, chunk_header, sizeof(chunk_header), 1) != 1) {
            SDL_SetError("Failed to read WAV chunk header");
            SDL_RWclose(rw);
            return -1;
        }

        chunk_size = AudioBackend_ReadLE32(&chunk_header[4]);
        if (SDL_memcmp(chunk_header, "fmt ", 4) == 0) {
            Uint32 fmt_size = SDL_min(chunk_size, (Uint32)sizeof(fmt_data));
            if (SDL_RWread(rw, fmt_data, fmt_size, 1) != 1) {
                SDL_SetError("Failed to read WAV fmt chunk");
                SDL_RWclose(rw);
                return -1;
            }
            if (fmt_size < 16) {
                SDL_SetError("WAV fmt chunk too small");
                SDL_RWclose(rw);
                return -1;
            }

            audio_format = AudioBackend_ReadLE16(&fmt_data[0]);
            channels = AudioBackend_ReadLE16(&fmt_data[2]);
            sample_rate = AudioBackend_ReadLE32(&fmt_data[4]);
            bits_per_sample = AudioBackend_ReadLE16(&fmt_data[14]);

            if (chunk_size > fmt_size) {
                if (SDL_RWseek(rw, (Sint64)(chunk_size - fmt_size), RW_SEEK_CUR) < 0) {
                    SDL_SetError("Failed to skip remaining fmt data");
                    SDL_RWclose(rw);
                    return -1;
                }
            }
        } else if (SDL_memcmp(chunk_header, "data", 4) == 0) {
            data_offset = SDL_RWtell(rw);
            data_size = chunk_size;
            break;
        } else {
            if (SDL_RWseek(rw, (Sint64)chunk_size, RW_SEEK_CUR) < 0) {
                SDL_SetError("Failed to skip WAV chunk");
                SDL_RWclose(rw);
                return -1;
            }
        }

        if (chunk_size & 1) {
            if (SDL_RWseek(rw, 1, RW_SEEK_CUR) < 0) {
                SDL_SetError("Failed to skip WAV padding");
                SDL_RWclose(rw);
                return -1;
            }
        }

        if (SDL_RWtell(rw) <= chunk_pos) {
            SDL_SetError("WAV parser did not advance");
            SDL_RWclose(rw);
            return -1;
        }
    }

    if (data_offset < 0 || data_size == 0) {
        SDL_SetError("Failed to locate WAV data chunk");
        SDL_RWclose(rw);
        return -1;
    }

    if (audio_format != 0x0014 && audio_format != 0x0020) {
        SDL_SetError("Unsupported WAV format");
        SDL_RWclose(rw);
        return -1;
    }
    if (channels == 0 || sample_rate == 0 || bits_per_sample == 0) {
        SDL_SetError("Invalid WAV audio parameters");
        SDL_RWclose(rw);
        return -1;
    }

    if (bits_per_sample != 4) {
        printf("Dreamcast CDA warning: expected 4-bit ADPCM but found %u-bit samples in %s\n",
               bits_per_sample, filename);
    }

    if (SDL_RWseek(rw, data_offset, RW_SEEK_SET) < 0) {
        SDL_SetError("Failed to seek to WAV data");
        SDL_RWclose(rw);
        return -1;
    }

    *audio_len = data_size;

    SDL_zero(*spec);
    spec->freq = (int)sample_rate;
    spec->format = AUDIO_S16;
    spec->channels = (Uint8)channels;
    spec->samples = 4096;
    spec->silence = 0x00;
    spec->size = *audio_len;

    *audio_rw = rw;
    return 0;
}

static void SDLCALL AudioBackend_CDAFill(void* userdata, Uint8* stream, int len) {
    (void)userdata;
    SDL_memset(stream, cda.spec.silence, len);
    if (!cda.playing) {
        return;
    }

    while (len > 0) {
        Uint32 avail = cda.ring_write - cda.ring_read;

        if (avail == 0) {
            return;
        }

        Uint32 rpos = cda.ring_read % (Uint32)sizeof(cda.ring);
        Uint32 copy_len = SDL_min((Uint32)len, SDL_min(avail, (Uint32)sizeof(cda.ring) - rpos));

        SDL_memcpy(stream, cda.ring + rpos, copy_len);
        SDL_MemoryBarrierRelease();
        cda.ring_read += copy_len;

        stream += copy_len;
        len -= (int)copy_len;
    }
}

tAudioBackend_error_code AudioBackend_Init(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        printf("SDL audio init error: %s\n", SDL_GetError());
        return eAB_error;
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_InitCDA(void) {
#ifdef __DREAMCAST__
    SDL_SetHint(SDL_HINT_AUDIO_ADPCM_STREAM_DC, "1");
#endif
    if (access("MUSIC/Track02.wav", F_OK) == -1) {
        printf("Music not found\n");
        return eAB_error;
    }
    return eAB_success;
}

void AudioBackend_UnInit(void) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioBackend_UnInitCDA(void) {
    AudioBackend_StopCDA();
}

tAudioBackend_error_code AudioBackend_StopCDA(void) {
    if (cda.device != 0) {
        SDL_LockAudioDevice(cda.device);
        cda.playing = 0;
        cda.len = 0;
        cda.ring_read = 0;
        cda.ring_write = 0;
        SDL_UnlockAudioDevice(cda.device);
        SDL_CloseAudioDevice(cda.device);
        cda.device = 0;
    }
    if (cda.rw != NULL) {
        SDL_RWclose(cda.rw);
        cda.rw = NULL;
    }
    cda.data_start = 0;
    cda.ring_read = 0;
    cda.ring_write = 0;
    cda.eof = 0;
    cda.playing = 0;
    cda.len = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_PlayCDA(int track) {
    char path[256];
    SDL_AudioSpec spec;
    SDL_RWops* rw;
    Uint32 len;

#ifdef __DREAMCAST__
    if (track >= 9600 && track <= 9607) {
        track = track - 9600 + 2;
    }
#endif

    snprintf(path, sizeof(path), "MUSIC/Track%02d.wav", track);
    if (access(path, F_OK) == -1) {
        printf("Dreamcast CDA missing: %s\n", path);
        return eAB_error;
    }

    printf("Dreamcast CDA start: %s\n", path);
    AudioBackend_StopCDA();
    rw = NULL;
    len = 0;
    printf("Dreamcast CDA loading ADPCM stream\n");
    if (AudioBackend_OpenDreamcastADPCMStream(path, &spec, &rw, &len) < 0) {
        printf("Failed to load music %s: %s\n", path, SDL_GetError());
        return eAB_error;
    }
    printf("Dreamcast CDA stream loaded: freq=%d channels=%u len=%lu\n", spec.freq, spec.channels, (unsigned long)len);

    cda.rw = rw;
    cda.data_start = SDL_RWtell(rw);
    cda.len = len;
    cda.ring_read = 0;
    cda.ring_write = 0;
    cda.eof = 0;
    cda.playing = 1;
    AudioBackend_PumpCDA();

    spec.callback = AudioBackend_CDAFill;
#ifdef __DREAMCAST__
    SDL_SetHint(SDL_HINT_AUDIO_ADPCM_STREAM_DC, "1");
#endif
    cda.spec = spec;
    printf("Dreamcast CDA opening audio device\n");
    cda.device = SDL_OpenAudioDevice(NULL, SDL_FALSE, &cda.spec, NULL, 0);
    if (cda.device == 0) {
        printf("Failed to open music audio device: %s\n", SDL_GetError());
        AudioBackend_StopCDA();
        return eAB_error;
    }
    printf("Dreamcast CDA device opened: %u\n", (unsigned int)cda.device);
    printf("Dreamcast CDA queued and unlocked\n");
    SDL_PauseAudioDevice(cda.device, 0);
    printf("Dreamcast CDA playback started\n");
    return eAB_success;
}

int AudioBackend_CDAIsPlaying(void) {
    if (cda.playing) {
        AudioBackend_PumpCDA();
    }
    return cda.playing && cda.rw != NULL && cda.device != 0 && SDL_GetAudioDeviceStatus(cda.device) == SDL_AUDIO_PLAYING;
}

tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume) {
    cda.volume = volume;
    return eAB_success;
}

void* AudioBackend_AllocateSampleTypeStruct(void) {
    tAudioBackend_stream_impl* sample_struct = malloc(sizeof(tAudioBackend_stream_impl));
    if (sample_struct != NULL) {
        memset(sample_struct, 0, sizeof(tAudioBackend_stream_impl));
        sample_struct->volume = AUDIOBACKEND_MAX_VOLUME;
    }
    return sample_struct;
}

tAudioBackend_error_code AudioBackend_PlaySample(void* type_struct_sample, int channels, void* data, int size, int rate, int loop) {
#ifndef __DREAMCAST__
    SDL_AudioSpec spec;
#endif
    int effective_rate;
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;

    assert(stream != NULL);
    if (data == NULL || size <= 0) {
        return eAB_error;
    }

    effective_rate = stream->frequency > 0 ? stream->frequency : rate;
    AudioBackend_CloseQueuedDevice(stream);

#ifdef __DREAMCAST__
    if (!AudioBackend_LoadDreamcastADPCMSample(stream, channels, (const Uint8*)data, (Uint32)size, effective_rate)) {
        return eAB_error;
    }

    if (SDL_DreamcastQueueADPCMSfx(stream->owned_data, stream->owned_len) <= 0) {
        printf("SDL_DreamcastQueueADPCMSfx failed: %s\n", SDL_GetError());
        AudioBackend_CloseQueuedDevice(stream);
        return eAB_error;
    }

    stream->looping = loop ? 1 : 0;
    stream->play_duration_ticks = effective_rate > 0
        ? ((Uint32)size * 2u * 1000u / (Uint32)((channels > 0 ? channels : 1) * effective_rate)) + 50u
        : 50u;
    stream->play_end_ticks = SDL_GetTicks() + stream->play_duration_ticks;
    stream->initialized = 1;
    return eAB_success;
#else
    (void)loop;
    SDL_zero(spec);
    spec.freq = effective_rate;
    spec.format = AUDIO_S16LSB;
    spec.channels = (Uint8)channels;
    spec.samples = 4096;

    if (!AudioBackend_OpenAndQueue(stream, &spec, (const Uint8*)data, (Uint32)size)) {
        return eAB_error;
    }
    return eAB_success;
#endif
}

int AudioBackend_SoundIsPlaying(void* type_struct_sample) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
#ifdef __DREAMCAST__
    if (stream->dreamcast_adpcm_sample) {
        if (stream->looping) {
            if (SDL_TICKS_PASSED(SDL_GetTicks(), stream->play_end_ticks)) {
                if (stream->owned_data != NULL && stream->owned_len > 0) {
                    if (SDL_DreamcastQueueADPCMSfx(stream->owned_data, stream->owned_len) <= 0) {
                        printf("SDL_DreamcastQueueADPCMSfx failed: %s\n", SDL_GetError());
                        AudioBackend_CloseQueuedDevice(stream);
                        return 0;
                    }
                    stream->play_end_ticks = SDL_GetTicks() + stream->play_duration_ticks;
                }
            }
            return 1;
        }
        if (SDL_TICKS_PASSED(SDL_GetTicks(), stream->play_end_ticks) == SDL_FALSE) {
            return 1;
        }
        AudioBackend_CloseQueuedDevice(stream);
        return 0;
    }
#endif
    return stream->device != 0 && SDL_GetQueuedAudioSize(stream->device) > 0;
}

tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    stream->volume = volume;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    stream->pan = pan;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    (void)original_rate;
    assert(stream != NULL);
    stream->frequency = new_rate;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetVolumeSeparate(void* type_struct_sample, int left_volume, int right_volume) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    stream->volume = (left_volume + right_volume) / 2;
    stream->pan = right_volume - left_volume;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)type_struct_sample;
    assert(stream != NULL);
    AudioBackend_CloseQueuedDevice(stream);
    return eAB_success;
}

tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate) {
    tAudioBackend_stream_impl* stream;
    SDL_AudioSpec spec;

    if (bit_depth != 8 && bit_depth != 16) {
        return NULL;
    }

    SDL_zero(spec);
    spec.freq = (int)sample_rate;
    spec.format = bit_depth == 8 ? AUDIO_U8 : AUDIO_S16LSB;
    spec.channels = (Uint8)channels;
    spec.samples = 2048;

#ifdef __DREAMCAST__
    if (g_dreamcast_smacker_stream_allocated) {
        stream = &g_dreamcast_smacker_stream;
        if (stream->spec.freq != spec.freq || stream->spec.format != spec.format || stream->spec.channels != spec.channels) {
            printf("Dreamcast Smacker audio format changed; skipping stream to avoid SDL audio close\n");
            return NULL;
        }
        return (tAudioBackend_stream*)stream;
    }

    /* CloseDevice tears down the KOS stream layer. If CDA was active, suspend
     * it and bounce SDL audio so snd_stream_init() runs again on reopen. */
    g_cda_suspended = 0;
    if (cda.device != 0) {
        AudioBackend_StopCDA();
        g_cda_suspended = 1;
        printf("Dreamcast: CDA suspended for Smacker stream\n");
    }
    stream = &g_dreamcast_smacker_stream;
    memset(stream, 0, sizeof(*stream));
    stream->volume = AUDIOBACKEND_MAX_VOLUME;
    stream->spec = spec;

    if (!AudioBackend_ReinitDreamcastAudio("0")) {
        if (g_cda_suspended) {
            AudioBackend_ReinitDreamcastAudio("1");
            g_cda_suspended = 0;
        }
        memset(stream, 0, sizeof(*stream));
        return NULL;
    }
    printf("Dreamcast: SDL audio restarted in PCM mode for Smacker\n");

    g_dreamcast_smacker_stream_allocated = 1;
#else
    stream = AudioBackend_AllocateSampleTypeStruct();
    if (stream == NULL) {
        return NULL;
    }
#endif

    stream->spec = spec;
    return (tAudioBackend_stream*)stream;
}

tAudioBackend_error_code AudioBackend_StreamWrite(tAudioBackend_stream* stream_handle, const unsigned char* data, unsigned long size) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)stream_handle;

    if (stream == NULL || data == NULL || size == 0) {
        return eAB_error;
    }
    if (size > 0xffffffffu) {
        return eAB_error;
    }
    if (stream->device == 0) {
        if (!AudioBackend_OpenAndQueue(stream, &stream->spec, data, (Uint32)size)) {
            return eAB_error;
        }
        return eAB_success;
    }
#ifdef __DREAMCAST__
    if (stream == &g_dreamcast_smacker_stream && SDL_GetQueuedAudioSize(stream->device) > DREAMCAST_SMACKER_MAX_QUEUED_AUDIO) {
        return eAB_success;
    }
#else
    SDL_PauseAudioDevice(stream->device, 0);
#endif
    if (SDL_QueueAudio(stream->device, data, (Uint32)size) < 0) {
        printf("SDL_QueueAudio failed: %s\n", SDL_GetError());
        return eAB_error;
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    tAudioBackend_stream_impl* stream = (tAudioBackend_stream_impl*)stream_handle;
    if (stream != NULL) {
#ifdef __DREAMCAST__
    if (stream == &g_dreamcast_smacker_stream) {
            if (stream->device != 0) {
                Uint32 drain_start = SDL_GetTicks();
                Uint32 queued = SDL_GetQueuedAudioSize(stream->device);
                printf("Dreamcast: Smacker stream close begin queued=%lu\n", (unsigned long)queued);
                while (queued > stream->spec.size && SDL_GetTicks() - drain_start < 250u) {
                    SDL_Delay(1);
                    queued = SDL_GetQueuedAudioSize(stream->device);
                }
                printf("Dreamcast: Smacker stream close drain queued=%lu\n", (unsigned long)queued);
                SDL_ClearQueuedAudio(stream->device);
                SDL_Delay(20);
                printf("Dreamcast: Smacker stream close device\n");
                SDL_CloseAudioDevice(stream->device);
                printf("Dreamcast: Smacker stream device closed\n");
                stream->device = 0;
            }
            stream->initialized = 0;
            g_dreamcast_smacker_stream_allocated = 0;
            if (g_cda_suspended) {
                if (AudioBackend_ReinitDreamcastAudio("1")) {
                    printf("Dreamcast: SDL audio restored to ADPCM mode after Smacker\n");
                }
            }
            g_cda_suspended = 0;
            return eAB_success;
        }
#endif
        AudioBackend_CloseQueuedDevice(stream);
        free(stream);
    }
    return eAB_success;
}
