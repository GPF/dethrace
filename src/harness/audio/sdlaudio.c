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
        Mix_Chunk* chunk;      // Holds the Mix_Chunk for short sound effects (samples)
        Mix_Music* music;      // Holds the Mix_Music for long audio tracks (background music)
        int volume;            // Controls the volume of the audio (0 - MIX_MAX_VOLUME)
        int pan;               // Controls the stereo panning (left-right balance)
        int frequency;         // The frequency/sample rate of the audio
        int initialized;       // Flag to indicate if the stream is properly initialized
        int channel;           // The channel number on which the audio is being played
        unsigned int sample_rate;  // The sample rate (frequency) of the audio
        unsigned int channels;     // Number of audio channels (1 for mono, 2 for stereo, etc.)
        int device_id;         // The ID of the audio device for SDL audio streaming
    } tAudioBackend_stream;


    // Global variables
    static int g_audio_device_open = 0; // Global flag to track if the audio device is open
    static float g_volume_multiplier = 1.0f;
    static Mix_Music* cda_music = NULL;

    // Global or static buffer to hold streaming audio data
    // #define STREAM_BUFFER_SIZE (4096 * 4) // Example size, adjust as needed
    // static Uint8 stream_buffer[STREAM_BUFFER_SIZE];
    // static int stream_buffer_pos = 0;
    // static int stream_buffer_size = 0;

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

        // Initialize ADX support
        if (Mix_Init(MIX_INIT_ADX) != MIX_INIT_ADX) {
            printf("Could not initialize mixer with ADX support: %s\n", Mix_GetError());
            SDL_Quit();
            return eAB_error;
        }

        if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 4096) < 0) {
            printf("SDL_mixer init error: %s\n", Mix_GetError());
            Mix_Quit();  // Clean up Mix_Init
            SDL_Quit();
            return eAB_error;
        }

        // If the audio device is already open, skip reopening it
        if (g_audio_device_open == 0) {
            // SDL_AudioSpec desired_spec;
            // desired_spec.freq = 44100;  // Default to 44.1kHz
            // desired_spec.channels = 2;  // Stereo
            // desired_spec.format = AUDIO_S16SYS;  // 16-bit audio
            // desired_spec.samples = 1024;  // Buffer size
            // desired_spec.callback = NULL;  // No callback

            // // Open the audio device
            // if (SDL_OpenAudioDevice(NULL, 0, &desired_spec, NULL, SDL_AUDIO_ALLOW_ANY_CHANGE) == 0) {
            //     printf("Error opening audio device: %s\n", SDL_GetError());
            //     return eAB_error;
            // }

            g_audio_device_open = 1;  // Mark the device as open
        }

        printf("Audio initialized at 44100 Hz, 16-bit stereo\n");
        return eAB_success;
    }


    // Initialize CDA (CD Audio) playback
    tAudioBackend_error_code AudioBackend_InitCDA(void) {
        printf("AudioBackend_InitCDA\n");

        // Check if music files are present or not
        if (access("MUSIC/Track02.wav", F_OK) == -1) {
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
        // printf("AudioBackend_StopCDA\n");
        


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
        sprintf(path, "MUSIC/Track0%d.wav", track);

        if (access(path, F_OK) == -1) {
            return eAB_error;
        }

        // printf("Starting music track: %s\n", path);
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

        // Load sample from memory
        Mix_Chunk* chunk = Mix_LoadWAV_RW(SDL_RWFromMem(data, size), 1);
        if (!chunk) {
            printf("Failed to load sample: %s\n", Mix_GetError());
            return eAB_error;
        }

        // Play sample
        int channel = Mix_PlayChannel(-1, chunk, loop ? -1 : 0);
        if (channel == -1) {
            printf("Failed to play sample: %s\n", Mix_GetError());
            Mix_FreeChunk(chunk);
            return eAB_error;
        }

        // Store chunk and channel in stream
        stream->chunk = chunk;
        stream->channel = channel;
        stream->initialized = 1;

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

    tAudioBackend_error_code AudioBackend_SetVolumeSeparate(void* type_struct_sample, int left_volume, int right_volume) {
        return eAB_error;
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

// Open a stream for Smacker audio
tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate) {
    tAudioBackend_stream* stream = malloc(sizeof(tAudioBackend_stream));
    if (!stream) {
        printf("Failed to allocate memory for audio stream\n");
        return NULL;
    }
    SDL_Log("AudioBackend_StreamOpen\n");

    // Initialize the stream struct
    memset(stream, 0, sizeof(tAudioBackend_stream));
    stream->sample_rate = sample_rate;
    stream->channels = channels;
    stream->channel = -1;  // No channel assigned yet

    // Validate the channel count (should be 1 for mono, 2 for stereo)
    if (stream->channels != 1 && stream->channels != 2) {
        printf("Invalid number of channels: %d\n", stream->channels);
        free(stream);
        return NULL;
    }

    // Set the sample rate (ensure it's a valid rate)
    if (stream->sample_rate < 8000 || stream->sample_rate > 48000) {
        printf("Invalid sample rate: %u\n", sample_rate);
        free(stream);
        return NULL;
    }

    return stream;
}

// Write audio data to the stream
tAudioBackend_error_code AudioBackend_StreamWrite(void* stream_handle, const unsigned char* data, unsigned long size) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)stream_handle;
    if (!stream || !data || size == 0) {
        printf("Invalid stream or data\n");
        return eAB_error;
    }

    // Allocate memory for PCM data (size / 1 byte per DPCM sample * 2 bytes per PCM sample)
    int16_t *pcm_data = malloc(size * sizeof(int16_t));
    if (!pcm_data) {
        printf("Failed to allocate memory for PCM data\n");
        return eAB_error;
    }

    // Convert DPCM to PCM
    // int16_t previous_sample = 0;  // Start with an initial value
    // uint8_t *dpcm = (uint8_t*)data;
    // int pcm_index = 0;

    // for (unsigned long i = 0; i < size; ++i) {
    //     int8_t delta = (int8_t)dpcm[i];  // Get the DPCM delta value
    //     previous_sample += delta;  // Apply the delta

    //     // Clamp the sample to the valid range for int16_t
    //     if (previous_sample > 32767) previous_sample = 32767;
    //     if (previous_sample < -32768) previous_sample = -32768;

    //     pcm_data[pcm_index++] = previous_sample;  // Store the decoded PCM sample
    // }

    // If sample rate conversion is needed, resample the PCM data here
    // (You can use a library like libsamplerate or soxr for this)

    // Create a Mix_Chunk from the audio data
    // Mix_Chunk* chunk = Mix_QuickLoad_RAW((Uint8*)data, size);
    // if (!chunk) {
    //     printf("Failed to create Mix_Chunk: %s\n", Mix_GetError());
    //     free(pcm_data);
    //     return eAB_error;
    // }

    // // Play the chunk on a channel
    // int channel = Mix_PlayChannel(-1, chunk, 0);
    // if (channel == -1) {
    //     printf("Failed to play audio: %s\n", Mix_GetError());
    //     Mix_FreeChunk(chunk);
    //     free(pcm_data);
    //     return eAB_error;
    // }

    // // Store the chunk and channel in the stream
    // if (stream->chunk) {
    //     Mix_FreeChunk(stream->chunk);  // Free the previous chunk if it exists
    // }
    // stream->chunk = chunk;
    // stream->channel = channel;

    // free(pcm_data);  // Free the temporary PCM buffer
    return eAB_success;
}
// Close the stream
tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    tAudioBackend_stream* stream = (tAudioBackend_stream*)stream_handle;
    if (stream) {
        // Stop and clean up the audio chunk when done
        if (stream->chunk) {
            Mix_HaltChannel(stream->channel);  // Stop the channel
            Mix_FreeChunk(stream->chunk);      // Free the chunk
            stream->chunk = NULL;
            stream->channel = -1;
        }

        free(stream);
    }
    return eAB_success;
}