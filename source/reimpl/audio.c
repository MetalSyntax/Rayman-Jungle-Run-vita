/*
 * Copyright (C) 2026 Rayman Jungle Run Vita port contributors
 * Based on the Carnivores / Riptide GP Vita ports (audio confirmed on hardware)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  audio.c
 * @brief Native replacement for org.fmod.FMODAudioDevice.
 *
 * libfmodex.so uses FMOD_OUTPUTTYPE_AUDIOTRACK. With that output FMOD
 * never talks to the hardware itself: the Java class FMODAudioDevice runs a thread
 * that polls fmodGetInfo() and pulls mixed PCM with fmodProcess(ByteBuffer), then
 * writes it into an AudioTrack.
 * This file implements that same loop using SceAudioOut instead of AudioTrack.
 */

#include "reimpl/audio.h"
#include "utils/logger.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <stdlib.h>
#include <string.h>

extern so_module so_mod_fmodex;

// FMODAudioDevice constants
#define FMOD_INFO_SAMPLERATE      0
#define FMOD_INFO_DSPBUFFERLENGTH 1
#define FMOD_INFO_DSPNUMBUFFERS   2
#define FMOD_INFO_MIXERRUNNING    3
#define FMOD_INFO_CHANNELCOUNT    4
#define NUMCHANNELS               2

// sceAudioOutOutput() granule: multiple of 64 samples, at most 65472.
#define DEFAULT_GRAIN 1024

static int (*fmodGetInfo)(JNIEnv *env, jobject thiz, jint info);
static int (*fmodProcess)(JNIEnv *env, jobject thiz, jobject byteBuffer);

static SceUID audio_thid = -1;
static volatile int audio_running = 0;
static volatile int audio_muted = 0;

static int valid_rate(int rate) {
    switch (rate) {
        case 8000: case 11025: case 12000: case 16000: case 22050:
        case 24000: case 32000: case 44100: case 48000:
            return 1;
        default:
            return 0;
    }
}

static int audio_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    int port = -1;
    int initialised = 0;
    int dsp_len = 0;       // samples produced by one fmodProcess() call
    int grain = 0;         // samples consumed by one sceAudioOutOutput() call
    int16_t *mix = NULL;   // fmodProcess() destination, dsp_len frames
    int16_t *fifo = NULL;  // accumulation buffer, grain + dsp_len frames
    int fifo_len = 0;      // frames currently queued in fifo

    while (audio_running) {
        if (!initialised) {
            int rate = fmodGetInfo(&jni, NULL, FMOD_INFO_SAMPLERATE);
            if (rate <= 0) {
                // FMOD system init not done yet. Wait 100 ms.
                sceKernelDelayThread(100 * 1000);
                continue;
            }

            dsp_len = fmodGetInfo(&jni, NULL, FMOD_INFO_DSPBUFFERLENGTH);
            int num = fmodGetInfo(&jni, NULL, FMOD_INFO_DSPNUMBUFFERS);
            int channels = fmodGetInfo(&jni, NULL, FMOD_INFO_CHANNELCOUNT);
            if (dsp_len <= 0) dsp_len = DEFAULT_GRAIN;
            if (channels != NUMCHANNELS) {
                l_error("audio: FMOD channel count %d != 2, audio disabled", channels);
                break;
            }

            grain = dsp_len;
            if (grain % 64 != 0 || grain > 65472)
                grain = DEFAULT_GRAIN;

            if (!valid_rate(rate)) {
                l_error("audio: FMOD sample rate %d unsupported by SceAudioOut, audio disabled", rate);
                break;
            }

            port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, grain, rate,
                                       SCE_AUDIO_OUT_MODE_STEREO);
            if (port < 0) {
                l_error("audio: sceAudioOutOpenPort(%d, %d) failed: 0x%08X",
                        grain, rate, port);
                break;
            }

            int vol[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
            sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);

            mix = calloc(dsp_len * NUMCHANNELS, sizeof(int16_t));
            fifo = calloc((grain + dsp_len) * NUMCHANNELS, sizeof(int16_t));
            fifo_len = 0;
            if (!mix || !fifo) {
                l_error("audio: out of memory for mix buffers");
                break;
            }

            l_info("audio: FMOD AudioTrack output up: rate=%d dsp_len=%d num_buffers=%d grain=%d port=%d",
                   rate, dsp_len, num, grain, port);
            initialised = 1;
            continue;
        }

        if (fmodGetInfo(&jni, NULL, FMOD_INFO_MIXERRUNNING) != 1) {
            // Mixer stopped (e.g. paused/closed)
            sceKernelDelayThread(10 * 1000);
            continue;
        }

        while (fifo_len < grain) {
            fmodProcess(&jni, NULL, (jobject) mix);
            memcpy(fifo + fifo_len * NUMCHANNELS, mix,
                   dsp_len * NUMCHANNELS * sizeof(int16_t));
            fifo_len += dsp_len;
        }

        if (audio_muted)
            memset(fifo, 0, grain * NUMCHANNELS * sizeof(int16_t));

        // Blocks until hardware consumed previous granule
        sceAudioOutOutput(port, fifo);

        fifo_len -= grain;
        if (fifo_len > 0)
            memmove(fifo, fifo + grain * NUMCHANNELS,
                    fifo_len * NUMCHANNELS * sizeof(int16_t));
    }

    if (port >= 0) {
        sceAudioOutOutput(port, NULL);
        sceAudioOutReleasePort(port);
    }
    free(mix);
    free(fifo);

    l_info("audio: thread finished");
    return sceKernelExitDeleteThread(0);
}

void audio_start(void) {
    if (audio_running)
        return;

    fmodGetInfo = (void *) so_symbol(&so_mod_fmodex, "Java_org_fmod_FMODAudioDevice_fmodGetInfo");
    fmodProcess = (void *) so_symbol(&so_mod_fmodex, "Java_org_fmod_FMODAudioDevice_fmodProcess");
    if (!fmodGetInfo || !fmodProcess) {
        l_error("audio: FMODAudioDevice natives not found in libfmodex.so");
        return;
    }

    audio_running = 1;
    audio_thid = sceKernelCreateThread("fmod_audiodevice", audio_thread,
                                       0x10000100 - 10, 64 * 1024, 0,
                                       SCE_KERNEL_CPU_MASK_USER_1, NULL);
    if (audio_thid < 0) {
        l_error("audio: sceKernelCreateThread failed: 0x%08X", audio_thid);
        audio_running = 0;
        return;
    }
    sceKernelStartThread(audio_thid, 0, NULL);
}

void audio_stop(void) {
    if (!audio_running)
        return;
    audio_running = 0;
    sceKernelWaitThreadEnd(audio_thid, NULL, NULL);
    audio_thid = -1;
}

void audio_set_muted(int muted) {
    audio_muted = muted;
}
