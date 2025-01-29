// Disable miniaudio's 'null' device fallback. A proper device must be found to enable playback
#define MA_NO_NULL

#include "harness/audio.h"
#include "harness/config.h"
#include "harness/os.h"
#include "harness/trace.h"

#include <kos.h>
#include <math.h>
#include <dc/sound/stream.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <wav/sndwav.h>
#include <SDL2/SDL.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <stdint.h>
#include <string.h>
#include <malloc.h> 

// duplicates DETHRACE/constants.h but is a necessary evil(?)
static int kMem_S3_DOS_SOS_channel = 234;

typedef struct tMiniaudio_sample {
    //ma_audio_buffer_ref buffer_ref;
    sfxhnd_t sound;
    int init_volume;
    int init_pan;
    int init_new_rate;
    int initialized;
} tMiniaudio_sample;

typedef struct tMiniaudio_stream {
    int frame_size_in_bytes;
    int sample_rate;
    int needs_converting;
    //ma_paged_audio_buffer_data paged_audio_buffer_data;
    //ma_paged_audio_buffer paged_audio_buffer;
    //ma_data_converter data_converter;
    wav_stream_hnd_t sound;
} tMiniaudio_stream;


#define NUM_SAMPLES (2048) // 2048
#define NUM_CHANNELS (2)
#define SAMPLERATE (44100) // 22050 44100

tAudioBackend_error_code AudioBackend_Init(void) {

    // Initialize sound system
    snd_init();

    printf("Audio backend initialized successfully.\n");
    return eAB_success;
}

// https://github.com/KallistiOS/KallistiOS/blob/80a54c601e71dfeb44ec764583815ac353580205/kernel/arch/dreamcast/include/dc/sound/sfxmgr.h
// https://github.com/Dreamcast-Projects/libwav/blob/master/sndwav.h

 wav_stream_hnd_t cda_hnd;

tAudioBackend_error_code AudioBackend_InitCDA(void) {
    printf("AudioBackend_InitCDA\n");

    // check if music files are present or not
    if (access("MUSIC/Track02.wav", F_OK) == -1) {
        printf("Music not found\n");
        return eAB_error;
    }
    printf("Music found\n");

    //snd_stream_init();
    wav_init(); 

    return eAB_success;
}

void AudioBackend_UnInit(void) {
    snd_sfx_unload_all();	
    snd_shutdown();
    wav_shutdown();
    snd_stream_shutdown();
}

void AudioBackend_UnInitCDA(void) {
    wav_stop(cda_hnd);
    wav_shutdown(); 
    snd_stream_shutdown();
}

tAudioBackend_error_code AudioBackend_StopCDA(void) {
    printf("AudioBackend_StopCDA\n");

    /*if (!cda_sound_initialized) {
        return eAB_success;
    }*/
    if (wav_is_playing(cda_hnd)) {
        printf("STOP!");
        wav_stop(cda_hnd);
        wav_destroy(cda_hnd); 
    }
    //cda_sound_initialized = 0;
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_PlayCDA(int track) {
    printf("AudioBackend_PlayCDA\n");

    char path[256];

    sprintf(path, "MUSIC/Track0%d.wav", track);

    if (access(path, F_OK) == -1) {
        return eAB_error;
    }

    printf("Starting music track: %s\n", path);
    AudioBackend_StopCDA(); 
    //sndoggvorbis_stop(); // double tap ... u know

    if(cda_hnd != NULL){
        wav_stop(cda_hnd); 
        wav_destroy(cda_hnd); 
    }

    cda_hnd = wav_create(path, 0);
    wav_play(cda_hnd); // dont loop me
    //cda_sound_initialized = 1;
    return 0; // eAB_success;
}

int AudioBackend_CDAIsPlaying(void) {
    //printf("AudioBackend_CDAIsPlaying %d\n", cda_sound_initialized);
    return wav_is_playing(cda_hnd); ;
}

tAudioBackend_error_code AudioBackend_SetCDAVolume(int volume) {
    printf("AudioBackend_SetCDAVolume\n");
    /*if (!cda_sound_initialized) {
        return eAB_error;
    }*/
    wav_volume(cda_hnd, volume / 255.0f);
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
    //ma_result result;
    //ma_int32 flags;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    /*assert(miniaudio != NULL);

    result = ma_audio_buffer_ref_init(ma_format_u8, channels, data, size / channels, &miniaudio->buffer_ref);
    miniaudio->buffer_ref.sampleRate = rate;
    if (result != MA_SUCCESS) {
        return eAB_error;
    }

    flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
    result = ma_sound_init_from_data_source(&engine, &miniaudio->buffer_ref, flags, NULL, &miniaudio->sound);
    if (result != MA_SUCCESS) {
        return eAB_error;
    }*/
    /*miniaudio->initialized = 1;

    if (miniaudio->init_volume > 0) {
        AudioBackend_SetVolume(type_struct_sample, miniaudio->init_volume);
        AudioBackend_SetPan(type_struct_sample, miniaudio->init_pan);
        AudioBackend_SetFrequency(type_struct_sample, rate, miniaudio->init_new_rate);
    }

    ma_sound_set_looping(&miniaudio->sound, loop);
    ma_sound_start(&miniaudio->sound);*/
    return eAB_success;
}

int AudioBackend_SoundIsPlaying(void* type_struct_sample) {
    /*tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (ma_sound_is_playing(&miniaudio->sound)) {
        return 1;
    }*/
    return 0;
}

tAudioBackend_error_code AudioBackend_SetVolume(void* type_struct_sample, int volume) {
    tMiniaudio_sample* miniaudio;
    float linear_volume;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    /*if (!miniaudio->initialized) {
        miniaudio->init_volume = volume;
        return eAB_success;
    }

    linear_volume = volume / 510.0f;
    ma_sound_set_volume(&miniaudio->sound, linear_volume);*/
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetPan(void* type_struct_sample, int pan) {
    tMiniaudio_sample* miniaudio;

    miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    /*if (!miniaudio->initialized) {
        miniaudio->init_pan = pan;
        return eAB_success;
    }

    // convert from directsound -10000 - 10000 pan scale
    ma_sound_set_pan(&miniaudio->sound, pan / 10000.0f);*/
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_SetFrequency(void* type_struct_sample, int original_rate, int new_rate) {
    tMiniaudio_sample* miniaudio;

    /*miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (!miniaudio->initialized) {
        miniaudio->init_new_rate = new_rate;
        return eAB_success;
    }

    // convert from directsound frequency to linear pitch scale
    ma_sound_set_pitch(&miniaudio->sound, (new_rate / (float)original_rate));*/
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StopSample(void* type_struct_sample) {
    tMiniaudio_sample* miniaudio;

   /*miniaudio = (tMiniaudio_sample*)type_struct_sample;
    assert(miniaudio != NULL);

    if (miniaudio->initialized) {
        ma_sound_stop(&miniaudio->sound);
        ma_sound_uninit(&miniaudio->sound);
        ma_audio_buffer_ref_uninit(&miniaudio->buffer_ref);
        miniaudio->initialized = 0;
    }*/
    return eAB_success;
}

tAudioBackend_stream* AudioBackend_StreamOpen(int bit_depth, int channels, unsigned int sample_rate) {
    printf("AudioBackend_StreamOpen - Not Implemented\n");
    return NULL;
}

tAudioBackend_error_code AudioBackend_StreamWrite(void* stream_handle, const unsigned char* data, unsigned long size) {
    
    printf("AudioBackend_StreamWrite - Not Implemented\n");
    return eAB_success;
}

tAudioBackend_error_code AudioBackend_StreamClose(tAudioBackend_stream* stream_handle) {
    printf("AudioBackend_StreamClose - Not Implemented\n");
    return eAB_success;
}