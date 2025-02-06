#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Define your audio backend error codes
typedef enum {
    eAB_success = 0,
    eAB_error = -1,
} tAudioBackend_error_code;

// Define your audio backend stream structure
typedef struct {
    Mix_Chunk* chunk;
    Mix_Music* music;
    int volume;
    int pan;
    int frequency;
    int initialized;
    int channel; // Track the channel for this sample
} tAudioBackend_stream;

// Global variables
static float g_volume_multiplier = 1.0f;
static Mix_Music* cda_music = NULL;

// Global or static buffer to hold streaming audio data
#define STREAM_BUFFER_SIZE (4096 * 4) // Example size, adjust as needed
static Uint8 stream_buffer[STREAM_BUFFER_SIZE];
static int stream_buffer_pos = 0;
static int stream_buffer_size = 0;

// Function prototypes
tAudioBackend_error_code AudioBackend_Init(void);
tAudioBackend_error_code AudioBackend_InitCDA(void);
void AudioBackend_UnInit(void);
void AudioBackend_UnInitCDA(void);
tAudioBackend_error_code AudioBackend_StopCDA(void);
tAudioBackend_error_code AudioBackend_PlayCDA(int track);
int AudioBackend_CDAIsPlaying(void);
tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume);
void* AudioBackend_AllocateSampleTypeStruct(void);
tAudioBackend_error_code AudioBackend_PlaySample(void* type_struct_sample, int channels, void* data, int size, int rate, int loop);
int AudioBackend_SoundIsPlaying(void* type_struct_sample);
tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume);
tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan);
tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate);
tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample);
tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate);
tAudioBackend_error_code AudioBackend_StreamWrite(void* stream_handle, const unsigned char* data, unsigned long size);
tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle);

// Initialize the audio backend
tAudioBackend_error_code AudioBackend_Init(void) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL init error: %s\n", SDL_GetError()); 
        return eAB_error;
    }

    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 2, 4096) < 0) {
        printf("SDL_mixer init error: %s\n", Mix_GetError());
        return eAB_error;
    }

    printf("Audio initialized at 22050 Hz, 16-bit stereo\n");
    return eAB_success;
}

// Initialize CDA (CD Audio) playback
tAudioBackend_error_code AudioBackend_InitCDA(void) {
    printf("AudioBackend_InitCDA\n");

    // Check if music files are present or not
    if (access("MUSIC/Track02.ogg", F_OK) == -1) {
        printf("Music not found\n");
        return eAB_error;
    }
    printf("Music found\n");

    return eAB_success;
}


// Uninitialize the audio backend
void AudioBackend_UnInit(void) {
    Mix_CloseAudio();
    SDL_Quit();
}

// Uninitialize CDA playback
void AudioBackend_UnInitCDA(void) {
    if (cda_music) {
        Mix_FreeMusic(cda_music);
        cda_music = NULL;
    }
}

// Stop CDA playback
tAudioBackend_error_code AudioBackend_StopCDA(void) {
    printf("AudioBackend_StopCDA\n");

    if (cda_music && Mix_PlayingMusic()) {
        Mix_HaltMusic();
        Mix_FreeMusic(cda_music);
        cda_music = NULL;
    }

    return eAB_success;
}

// Play a CDA track
tAudioBackend_error_code AudioBackend_PlayCDA(int track) {
    printf("AudioBackend_PlayCDA\n");

    char path[256];
    sprintf(path, "MUSIC/Track0%d.ogg", track);

    if (access(path, F_OK) == -1) {
        return eAB_error;
    }

    printf("Starting music track: %s\n", path);
    AudioBackend_StopCDA();

    cda_music = Mix_LoadMUS(path);
    if (!cda_music) {
        printf("Failed to load music: %s\n", Mix_GetError());
        return eAB_error;
    }

    if (Mix_PlayMusic(cda_music, 0) == -1) {
        printf("Failed to play music: %s\n", Mix_GetError());
        return eAB_error;
    }

    return eAB_success;
}

// Check if CDA is playing
int AudioBackend_CDAIsPlaying(void) {
    return Mix_PlayingMusic();
}

// Set CDA volume
tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume) {
    printf("AudioBackend_SetCDAVolume\n");
    Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 255);
    return eAB_success;
}

// Allocate a sample type structure
void* AudioBackend_AllocateSampleTypeStruct(void) {
    tAudioBackend_stream* sample_struct = malloc(sizeof(tAudioBackend_stream));
    if (sample_struct) {
        memset(sample_struct, 0, sizeof(tAudioBackend_stream));
    }
    return sample_struct;
}

// Play a sample
tAudioBackend_error_code AudioBackend_PlaySample(void* type_struct_sample, int channels, void* data, int size, int rate, int loop) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)type_struct_sample;
    assert(stream != NULL);

    SDL_AudioCVT cvt;
    int build_result = SDL_BuildAudioCVT(&cvt, AUDIO_U8, channels, rate, AUDIO_S16SYS, 2, 22050);
    if (build_result < 0) {
        printf("SDL_BuildAudioCVT failed: %s\n", SDL_GetError());
        return eAB_error;
    }

    cvt.len = size;
    cvt.buf = (Uint8*)malloc(cvt.len * cvt.len_mult);
    if (!cvt.buf) {
        printf("Memory allocation failed for audio conversion\n");
        return eAB_error;
    }
    memcpy(cvt.buf, data, size);

    if (SDL_ConvertAudio(&cvt) < 0) {
        printf("SDL_ConvertAudio failed: %s\n", SDL_GetError());
        free(cvt.buf);
        return eAB_error;
    }

    // Resampling (example using linear interpolation, requires implementation)
    // Implement or integrate a resampler here if SDL's conversion isn't sufficient

    stream->chunk = Mix_QuickLoad_RAW(cvt.buf, cvt.len_cvt);
    if (!stream->chunk) {
        printf("Failed to load sample: %s\n", Mix_GetError());
        free(cvt.buf);
        return eAB_error;
    }

    int channel = Mix_PlayChannel(-1, stream->chunk, loop ? -1 : 0);
    if (channel == -1) {
        printf("Failed to play sample: %s\n", Mix_GetError());
        free(cvt.buf);
        Mix_FreeChunk(stream->chunk);
        return eAB_error;
    }

    stream->channel = channel;
    stream->initialized = 1;
    free(cvt.buf);
    return eAB_success;
}

// Check if a sample is playing
int AudioBackend_SoundIsPlaying(void* type_struct_sample) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)type_struct_sample;
    assert(stream != NULL);

    return Mix_Playing(-1);
}

// Set sample volume
tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)type_struct_sample;
    assert(stream != NULL);

    if (!stream->initialized) {
        stream->volume = volume;
        return eAB_success;
    }

    Mix_Volume(stream->channel, (volume * MIX_MAX_VOLUME) / 255);
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)type_struct_sample;
    assert(stream != NULL);

    if (!stream->initialized) {
        stream->pan = pan;
        return eAB_success;
    }

    // Convert pan from -10000 (left) to 10000 (right) to SDL's 0-255 scale
    Uint8 left = (pan <= 0) ? 255 : (255 * (10000 - pan)) / 10000;
    Uint8 right = (pan >= 0) ? 255 : (255 * (10000 + pan)) / 10000;
    Mix_SetPanning(stream->channel, left, right);
    return eAB_success;
}

// Set sample frequency
tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)type_struct_sample;
    assert(stream != NULL);

    if (!stream->initialized) {
        stream->frequency = new_rate;
        return eAB_success;
    }

    // SDL2_mixer does not support changing the frequency of a playing sample directly.
    // You may need to stop and restart the sample with the new frequency.
    return eAB_success;
}

// Stop a sample
tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)type_struct_sample;
    assert(stream != NULL);

    if (stream->initialized) {
        Mix_HaltChannel(stream->channel);
        Mix_FreeChunk(stream->chunk);
        stream->initialized = 0;
        stream->chunk = NULL;
    }
    return eAB_success;
}

// Open a stream
tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate) {
    tAudioBackend_stream* stream = malloc(sizeof(tAudioBackend_stream));
    if (stream) {
        memset(stream, 0, sizeof(tAudioBackend_stream));
    }
    return stream;
}

// Write to a stream
tAudioBackend_error_code AudioBackend_StreamWrite(void* stream_handle, const unsigned char* data, unsigned long size) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)stream_handle;
    assert(stream != NULL);

    // if (size + stream_buffer_size > STREAM_BUFFER_SIZE) {
    //     // Buffer overflow, just log it or handle it in a way that fits your application
    //     printf("Audio buffer overflow\n");
    //     return eAB_error;
    // }

    // // Copy new data into our buffer
    // memcpy(&stream_buffer[stream_buffer_size], data, size);
    // stream_buffer_size += (int)size;

    // // If music isn't playing, start it
    // if (!Mix_PlayingMusic()) {
    //     Mix_PlayMusic(NULL, 0); // Play silent music to trigger the hook
    // }

    return eAB_success;
}

// Close a stream
tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)stream_handle;
    if (stream) {
        if (stream->music) {
            Mix_FreeMusic(stream->music);
        }
        free(stream);
    }
    return eAB_success;
}