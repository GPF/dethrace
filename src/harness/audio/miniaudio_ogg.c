// Disable miniaudio's 'null' device fallback. A proper device must be found to enable playback
#define MA_NO_NULL

#include "harness/audio.h"
#include "harness/config.h"
#include "harness/os.h"
#include "harness/trace.h"

// Must come before miniaudio.h
#define STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

// Must come after miniaudio.h
#undef STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// duplicates DETHRACE/constants.h but is a necessary evil(?)
static int kMem_S3_DOS_SOS_channel = 234;

typedef struct tMiniaudio_sample {
    ma_audio_buffer_ref buffer_ref;
    ma_sound sound;
    int init_volume;
    int init_pan;
    int init_new_rate;
    int initialized;
} tMiniaudio_sample;

typedef struct tMiniaudio_stream {
    int frame_size_in_bytes;
    int sample_rate;
    int needs_converting;
    ma_paged_audio_buffer_data paged_audio_buffer_data;
    ma_paged_audio_buffer paged_audio_buffer;
    ma_data_converter data_converter;
    ma_sound sound;
} tMiniaudio_stream;

ma_engine engine;
ma_sound cda_sound;
//int cda_sound_initialized;

#include <kos.h>
#include <math.h>
#include <dc/sound/stream.h>
#include <oggvorbis/sndoggvorbis.h>
#include <SDL2/SDL.h>

#define NUM_SAMPLES (2048) // 2048
#define NUM_CHANNELS (2)
#define SAMPLERATE (44100) // 22050 44100

static float g_volume_multiplier = 1.0f;

#include <stdint.h>
#include <string.h>
#include <malloc.h> 

// Custom audio reading function
ma_result ma_engine_read_pcm_frames_dc(ma_engine* pEngine, float* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    ma_result result = MA_SUCCESS;
    ma_uint64 totalFramesRead = 0;
    ma_uint32 channels;

    // Ensure output buffer is 4-byte aligned for SH-4
    if (((uintptr_t)pFramesOut % 4) != 0) {
        printf("Misaligned memory, prevent crash\n");
        return MA_INVALID_ARGS;  // Misaligned memory, prevent crash
    }

    if (pFramesRead != NULL) {
        *pFramesRead = 0;  // Safety initialization
    }

    if (pEngine == NULL) {
        return MA_INVALID_ARGS;  // Handle null engine safely
    }

    channels = ma_engine_get_channels(pEngine);

    // Read audio data in chunks if necessary
    while (totalFramesRead < frameCount) {
        ma_uint32 framesToRead = (frameCount - totalFramesRead > 0xFFFFFFFF) ? 0xFFFFFFFF : (ma_uint32)(frameCount - totalFramesRead);
        ma_uint64 framesJustRead = 0; // fix - ma_uint32

        // Read frames from the engine, ensuring aligned output
        result = ma_engine_read_pcm_frames(pEngine, pFramesOut + totalFramesRead * channels, framesToRead, &framesJustRead);
        
        if (result != MA_SUCCESS) {
            break;
        }

        totalFramesRead += framesJustRead;

        // Avoid infinite loop in case no frames were read
        if (framesJustRead == 0) {
            break;
        }
    }

    // Silence any remaining frames to prevent noise
    if (totalFramesRead < frameCount) {
        memset(pFramesOut + totalFramesRead * channels, 0, (frameCount - totalFramesRead) * channels * sizeof(float));
    }

    if (pFramesRead != NULL) {
        *pFramesRead = totalFramesRead;
    }

    return result;
}

void data_callback(void* pUserData, Uint8* pBuffer, int bufferSizeInBytes) {
    //    Each sample is a float (4 bytes), each frame has 'NUM_CHANNELS' samples.
    //    However, we requested 16-bit output from SDL, so we need to ensure
    //    we read the correct number of float frames from MiniAudio.

    // We'll compute frames based on 16-bit stereo => 4 bytes per frame.
    // bufferSizeInBytes is total bytes. For stereo 16-bit => 4 bytes/frame.
    int framesRequested = bufferSizeInBytes / (sizeof(int16_t) * NUM_CHANNELS);

    if (framesRequested > NUM_SAMPLES) {
        framesRequested = NUM_SAMPLES;
    }

    #define AUDIO_ALIGNMENT 4
    //float floatBuffer[NUM_SAMPLES * NUM_CHANNELS] __attribute__((aligned(4)));
    float *floatBuffer = (float*)aligned_alloc(AUDIO_ALIGNMENT, NUM_SAMPLES * NUM_CHANNELS * sizeof(float));
    //float* floatBuffer = (float*)memalign(4, NUM_SAMPLES * NUM_CHANNELS * sizeof(float));

    if (((uintptr_t)floatBuffer % 4) != 0) {
        printf("Memory allocation failed or misaligned.\n");
        free(floatBuffer);
        return;
    }


   ma_engine_read_pcm_frames(&engine, floatBuffer, framesRequested, NULL);
   //ma_engine_read_pcm_frames_dc(&engine, __builtin_assume_aligned(floatBuffer, 4), framesRequested, NULL);
   /*
    if (result != MA_SUCCESS) {
        printf("Error reading audio frames\n");
        memset(pBuffer, 0, bufferSizeInBytes);
        free(floatBuffer);
        return;
    }
   */

    int16_t *pBufferS16 = (int16_t *)pBuffer;
    for (int i = 0; i < framesRequested * NUM_CHANNELS; i++) {
        float s = floatBuffer[i] * g_volume_multiplier; // apply volume
        if (s > 1.0f)  s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        pBufferS16[i] = (int16_t)(s * 32767.0f);
    }

    // If SDL wants more frames than we read, zero them out
    int totalFramesSDL = bufferSizeInBytes / (sizeof(int16_t) * NUM_CHANNELS);
    int leftoverFrames = totalFramesSDL - framesRequested;
    if (leftoverFrames > 0) {
        memset(&pBufferS16[framesRequested * NUM_CHANNELS], 0,
               leftoverFrames * NUM_CHANNELS * sizeof(int16_t));
    }
    free(floatBuffer);
}

tAudioBackend_error_code AudioBackend_Init(void) {

    ma_engine_config config = ma_engine_config_init();
    config.noDevice   = MA_TRUE;
    config.channels   = NUM_CHANNELS;
    config.sampleRate = SAMPLERATE;

    ma_result result = ma_engine_init(&config, &engine);
    if (result != MA_SUCCESS) {
        printf("Failed to init MiniAudio engine. Error code: %x\n", result);
        return -1;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("Failed to init SDL audio subsystem. SDL Error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_AudioSpec desiredSpec, obtainedSpec;
    MA_ZERO_OBJECT(&desiredSpec);
    desiredSpec.freq     = SAMPLERATE;
    desiredSpec.format   = AUDIO_S16;
    desiredSpec.channels = NUM_CHANNELS;
    desiredSpec.samples  = NUM_SAMPLES;
    desiredSpec.callback = data_callback;
    desiredSpec.userdata = NULL;            

    SDL_AudioDeviceID deviceID = SDL_OpenAudioDevice(NULL, 0, &desiredSpec, &obtainedSpec, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    if (deviceID == 0) {
        printf("Failed to open SDL audio device: %s\n", SDL_GetError());
        return -1;
    }

    printf("Audio Init:\n");
    printf("  freq    = %d\n", obtainedSpec.freq);
    printf("  format  = 0x%x\n", obtainedSpec.format);
    printf("  channels= %d\n", obtainedSpec.channels);
    printf("  samples = %d\n", obtainedSpec.samples);

    SDL_PauseAudioDevice(deviceID, 0);

    ma_engine_set_volume(&engine, 1.0f);

    printf("Audio backend initialized successfully.\n");
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_InitCDA(void) {
    printf("AudioBackend_InitCDA\n");

    // check if music files are present or not
    if (access("MUSIC/Track02.ogg", F_OK) == -1) {
        printf("Music not found\n");
        return eAB_error;
    }
    printf("Music found\n");

    //snd_stream_init();
    sndoggvorbis_init();

    return eAB_success;
}

void AudioBackend_UnInit(void) {
    ma_engine_uninit(&engine);
}

// https://github.com/KallistiOS/liboggvorbisplay/blob/5cea4ada7069a372423734cbc9a94ae689c7601e/include/oggvorbis/sndoggvorbis.h#L19

void AudioBackend_UnInitCDA(void) {
    sndoggvorbis_stop();
    sndoggvorbis_shutdown();
    snd_stream_shutdown();
}

tAudioBackend_error_code AudioBackend_StopCDA(void) {
    printf("AudioBackend_StopCDA\n");

    /*if (!cda_sound_initialized) {
        return eAB_success;
    }*/
    if (sndoggvorbis_isplaying()) { 
    //if (ma_sound_is_playing(&cda_sound)) {
        ma_sound_stop(&cda_sound);
        sndoggvorbis_stop();
    }
    ma_sound_uninit(&cda_sound);
    //cda_sound_initialized = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_PlayCDA(int track) {
    printf("AudioBackend_PlayCDA\n");

    char path[256];
    ma_result result;

    sprintf(path, "MUSIC/Track0%d.ogg", track);

    if (access(path, F_OK) == -1) {
        return eAB_error;
    }

    printf("Starting music track: %s\n", path);
    AudioBackend_StopCDA(); 
    //sndoggvorbis_stop(); // double tap ... u know
    sndoggvorbis_start(path, 0); // dont loop me
    //cda_sound_initialized = 1;

    // ensure we are not still playing a track
    /*AudioBackend_StopCDA();

    result = ma_sound_init_from_file(&engine, path, 0, NULL, NULL, &cda_sound);
    if (result != MA_SUCCESS) {
        return eAB_error;
    }
    cda_sound_initialized = 1;
    result = ma_sound_start(&cda_sound);
    if (result != MA_SUCCESS) {
        return eAB_error;
    }*/
    return 0; // eAB_success;
}

int AudioBackend_CDAIsPlaying(void) {
    //printf("AudioBackend_CDAIsPlaying %d\n", cda_sound_initialized);
    /*if (!cda_sound_initialized) {
        return 0;
    }*/
    //printf("> AudioBackend_CDAIsPlaying: %d \n", sndoggvorbis_isplaying());
    //printf("AudioBackend_CDAIsPlaying %d\n", ma_sound_is_playing(&cda_sound));
    //return ma_sound_is_playing(&cda_sound);
    return sndoggvorbis_isplaying();
}

tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume) {
    printf("AudioBackend_SetCDAVolume\n");
    /*if (!cda_sound_initialized) {
        return eAB_error;
    }*/
    ma_sound_set_volume(&cda_sound, volume / 255.0f);
    //sndoggvorbis_volume(volume / 255.0f);
    return eAB_success;
}

void* AudioBackend_AllocateSampleTypeStruct(void) {
    tMiniaudio_sample* sample_struct;
    sample_struct = BrMemAllocate(sizeof(tMiniaudio_sample), kMem_S3_DOS_SOS_channel);
    if (sample_struct == NULL) {
        return 0;
    }
    memset(sample_struct, 0, sizeof(tMiniaudio_sample));
    return sample_struct;
}

tAudioBackend_error_code AudioBackend_PlaySample(void* type_struct_sample, int channels, void* data, int size, int rate, int loop) {
    tMiniaudio_sample* miniaudio;
    ma_result result;
    ma_int32 flags;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    result = ma_audio_buffer_ref_init(ma_format_u8, channels, data, size / channels, &miniaudio->buffer_ref);
    miniaudio->buffer_ref.sampleRate = rate;
    if (result != MA_SUCCESS) {
        return eAB_error;
    }

    flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
    result = ma_sound_init_from_data_source(&engine, &miniaudio->buffer_ref, flags, NULL, &miniaudio->sound);
    if (result != MA_SUCCESS) {
        return eAB_error;
    }
    miniaudio->initialized = 1;

    if (miniaudio->init_volume > 0) {
        AudioBackend_SetVolume(type_struct_sample, miniaudio->init_volume);
        AudioBackend_SetPan(type_struct_sample, miniaudio->init_pan);
        AudioBackend_SetFrequency(type_struct_sample, rate, miniaudio->init_new_rate);
    }

    ma_sound_set_looping(&miniaudio->sound, loop);
    ma_sound_start(&miniaudio->sound);
    return eAB_success;
}

int AudioBackend_SoundIsPlaying(void* type_struct_sample) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (ma_sound_is_playing(&miniaudio->sound)) {
        return 1;
    }
    return 0;
}

tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume) {
    tMiniaudio_sample* miniaudio;
    float linear_volume;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (!miniaudio->initialized) {
        miniaudio->init_volume = volume;
        return eAB_success;
    }

    linear_volume = volume / 510.0f;
    ma_sound_set_volume(&miniaudio->sound, linear_volume);
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (!miniaudio->initialized) {
        miniaudio->init_pan = pan;
        return eAB_success;
    }

    // convert from directsound -10000 - 10000 pan scale
    ma_sound_set_pan(&miniaudio->sound, pan / 10000.0f);
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (!miniaudio->initialized) {
        miniaudio->init_new_rate = new_rate;
        return eAB_success;
    }

    // convert from directsound frequency to linear pitch scale
    ma_sound_set_pitch(&miniaudio->sound, (new_rate / (float)original_rate));
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (miniaudio->initialized) {
        ma_sound_stop(&miniaudio->sound);
        ma_sound_uninit(&miniaudio->sound);
        ma_audio_buffer_ref_uninit(&miniaudio->buffer_ref);
        miniaudio->initialized = 0;
    }
    return eAB_success;
}

tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate) {
    tMiniaudio_stream* new;
    ma_data_converter_config data_converter_config;

    new = malloc(sizeof(tMiniaudio_stream));
    new->sample_rate = sample_rate;
    ma_format format;
    switch (bit_depth) {
    case 8:
        format = ma_format_u8;
        new->frame_size_in_bytes = 1 * channels;
        break;
    case 16:
        format = ma_format_s16;
        new->frame_size_in_bytes = 2 * channels;
        break;
    case 24:
        format = ma_format_s24;
        new->frame_size_in_bytes = 3 * channels;
        break;
    case 32:
        format = ma_format_s32;
        new->frame_size_in_bytes = 4 * channels;
        break;
    default:
        goto failed;
    }

    if ((new->frame_size_in_bytes == 0) || (ma_paged_audio_buffer_data_init(format, channels, &new->paged_audio_buffer_data) != MA_SUCCESS)) {
        LOG_WARN("Failed to create paged audio buffer data");
        goto failed;
    }

    ma_paged_audio_buffer_config paged_audio_buffer_config = ma_paged_audio_buffer_config_init(&new->paged_audio_buffer_data);
    if (ma_paged_audio_buffer_init(&paged_audio_buffer_config, &new->paged_audio_buffer) != MA_SUCCESS) {
        LOG_WARN("Failed to create paged audio buffer for smacker audio stream");
        goto failed;
    }

    if (ma_sound_init_from_data_source(&engine, &new->paged_audio_buffer, MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &new->sound) != MA_SUCCESS) {
        LOG_WARN("Failed to create sound from data source");
        goto failed;
    }

    // allocate and initialize data converter if miniaudio engine and Smack file soundtrack sample rates differ
    if (ma_engine_get_sample_rate(&engine) != sample_rate) {
        new->needs_converting = 1;
        data_converter_config = ma_data_converter_config_init(format, format, channels, channels, sample_rate, ma_engine_get_sample_rate(&engine));
        if (ma_data_converter_init(&data_converter_config, NULL, &new->data_converter) != MA_SUCCESS) {
            LOG_WARN("Failed to create sound data converter");
            goto failed;
        }
    }
    return new;

failed:
    free(new);
    return NULL;
}

tAudioBackend_error_code AudioBackend_StreamWrite(void* stream_handle, const unsigned char* data, unsigned long size) {
    tMiniaudio_stream* stream = stream_handle;
    ma_uint64 nb_frames_in;
    ma_uint64 nb_frames_out;
    ma_uint64 current_pos;
    ma_paged_audio_buffer_page* new_page;

    if (ma_paged_audio_buffer_get_length_in_pcm_frames(&stream->paged_audio_buffer, &current_pos) != MA_SUCCESS) {
        LOG_WARN("ma_paged_audio_buffer_get_length_in_pcm_frames failed");
        return eAB_error;
    }

    // do we need to convert the sample frequency?
    if (stream->needs_converting) {
        nb_frames_in = size / stream->frame_size_in_bytes;
        nb_frames_out = nb_frames_in * ma_engine_get_sample_rate(&engine) / stream->sample_rate;

        if (ma_paged_audio_buffer_data_allocate_page(&stream->paged_audio_buffer_data, nb_frames_out, NULL, NULL, &new_page) != MA_SUCCESS) {
            LOG_WARN("ma_paged_audio_buffer_data_allocate_page failed");
            return eAB_error;
        }
        if (ma_data_converter_process_pcm_frames(&stream->data_converter, data, &nb_frames_in, new_page->pAudioData, &nb_frames_out) != MA_SUCCESS) {
            LOG_WARN("ma_data_converter_process_pcm_frames failed");
            return eAB_error;
        }
        if (ma_paged_audio_buffer_data_append_page(&stream->paged_audio_buffer_data, new_page) != MA_SUCCESS) {
            LOG_WARN("ma_paged_audio_buffer_data_append_page failed");
            return eAB_error;
        }
    } else { // no sampling frequency conversion needed
        if (ma_paged_audio_buffer_data_allocate_and_append_page(&stream->paged_audio_buffer_data, (ma_uint32)(size / (ma_uint64)stream->frame_size_in_bytes), data, NULL) != MA_SUCCESS) {
            LOG_WARN("ma_paged_audio_buffer_data_allocate_and_append_page failed");
            return eAB_error;
        }
    }

    if (!ma_sound_is_playing(&stream->sound)) {
        // seek either at start, or where the accumulated value hasn't played yet
        if (ma_sound_seek_to_pcm_frame(&stream->sound, current_pos) != MA_SUCCESS) {
            LOG_WARN("ma_sound_seek_to_pcm_frame failed");
        }
        if (ma_sound_start(&stream->sound) != MA_SUCCESS) {
            LOG_WARN("ma_sound_start failed");
        }
    }
    if (ma_sound_at_end(&stream->sound)) {
        LOG_WARN("video not playing fast enough: sound starved!");
    }
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    tMiniaudio_stream* stream = stream_handle;
    if (stream->needs_converting) {
        ma_data_converter_uninit(&stream->data_converter, NULL);
    }
    ma_sound_stop(&stream->sound);
    ma_sound_uninit(&stream->sound);
    ma_paged_audio_buffer_uninit(&stream->paged_audio_buffer);
    ma_paged_audio_buffer_data_uninit(&stream->paged_audio_buffer_data, NULL);

    free(stream);
    return eAB_success;
}