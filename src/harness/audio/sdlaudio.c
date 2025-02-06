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
} tAudioBackend_stream;

// Global variables
static float g_volume_multiplier = 1.0f;
static Mix_Music* cda_music = NULL;

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
        printf("SDL could not initialize! SDL Error: %s\n", SDL_GetError());
        return eAB_error;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("SDL_mixer could not initialize! SDL_mixer Error: %s\n", Mix_GetError());
        return eAB_error;
    }

    printf("Audio backend initialized successfully.\n");
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

    stream->chunk = Mix_QuickLoad_RAW((Uint8*)data, size);
    if (!stream->chunk) {
        printf("Failed to load sample: %s\n", Mix_GetError());
        return eAB_error;
    }

    stream->initialized = 1;
    Mix_PlayChannel(-1, stream->chunk, loop ? -1 : 0);

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

    Mix_VolumeChunk(stream->chunk, volume * MIX_MAX_VOLUME / 255);
    return eAB_success;
}

// Set sample pan
tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)type_struct_sample;
    assert(stream != NULL);

    if (!stream->initialized) {
        stream->pan = pan;
        return eAB_success;
    }

    // SDL2_mixer does not support panning directly, so you may need to implement this manually
    // or use a different library that supports panning.
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
        Mix_HaltChannel(-1);
        Mix_FreeChunk(stream->chunk);
        stream->initialized = 0;
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

    // SDL2_mixer does not support custom streaming directly. You may need to use Mix_Music for streaming.
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