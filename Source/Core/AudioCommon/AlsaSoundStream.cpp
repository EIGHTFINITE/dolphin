// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <mutex>

#include "AudioCommon/AlsaSoundStream.h"
#include "Common/CommonTypes.h"
#include "Common/Thread.h"
#include "Common/Logging/Log.h"

AlsaSound::AlsaSound()
	: m_thread_status(ALSAThreadStatus::STOPPED)
	, handle(nullptr)
	, frames_to_deliver(FRAME_COUNT_MIN)
{
}

bool AlsaSound::Start()
{
	m_thread_status.store(ALSAThreadStatus::RUNNING);
	if (!AlsaInit())
	{
		m_thread_status.store(ALSAThreadStatus::STOPPED);
		return false;
	}

	thread = std::thread(&AlsaSound::SoundLoop, this);
	return true;
}

void AlsaSound::Stop()
{
	m_thread_status.store(ALSAThreadStatus::STOPPING);

	//Give the opportunity to the audio thread
	//to realize we are stopping the emulation
	cv.notify_one();
	thread.join();
}

void AlsaSound::Update()
{
	// don't need to do anything here.
}

// Called on audio thread.
void AlsaSound::SoundLoop()
{
	Common::SetCurrentThreadName("Audio thread - alsa");
	while (m_thread_status.load() != ALSAThreadStatus::STOPPING)
	{
		while (m_thread_status.load() == ALSAThreadStatus::RUNNING)
		{
			m_mixer->Mix(mix_buffer, frames_to_deliver);
			int rc = snd_pcm_writei(handle, mix_buffer, frames_to_deliver);
			if (rc == -EPIPE)
			{
				// Underrun
				snd_pcm_prepare(handle);
			}
			else if (rc < 0)
			{
				ERROR_LOG(AUDIO, "writei fail: %s", snd_strerror(rc));
			}
		}
		if (m_thread_status.load() == ALSAThreadStatus::PAUSED)
		{
			snd_pcm_drop(handle); // Stop sound output

			// Block until thread status changes.
			std::unique_lock<std::mutex> lock(cv_m);
			cv.wait(lock, [this]{ return m_thread_status.load() != ALSAThreadStatus::PAUSED; });

			snd_pcm_prepare(handle); // resume sound output
		}
	}
	AlsaShutdown();
	m_thread_status.store(ALSAThreadStatus::STOPPED);
}


void AlsaSound::Clear(bool muted)
{
	m_muted = muted;
	m_thread_status.store(muted ? ALSAThreadStatus::PAUSED : ALSAThreadStatus::RUNNING);
	cv.notify_one(); // Notify thread that status has changed
}

bool AlsaSound::AlsaInit()
{
	unsigned int sample_rate = m_mixer->GetSampleRate();
	int err;
	int dir;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_uframes_t buffer_size,buffer_size_max;
	unsigned int periods;

	err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Audio open error: %s\n", snd_strerror(err));
		return false;
	}

	snd_pcm_hw_params_alloca(&hwparams);

	err = snd_pcm_hw_params_any(handle, hwparams);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Broken configuration for this PCM: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_hw_params_set_access(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Access type not available: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_hw_params_set_format(handle, hwparams, SND_PCM_FORMAT_S16_LE);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Sample format not available: %s\n", snd_strerror(err));
		return false;
	}

	dir = 0;
	err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &sample_rate, &dir);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Rate not available: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_hw_params_set_channels(handle, hwparams, CHANNEL_COUNT);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Channels count not available: %s\n", snd_strerror(err));
		return false;
	}

	periods = BUFFER_SIZE_MAX / FRAME_COUNT_MIN;
	err = snd_pcm_hw_params_set_periods_max(handle, hwparams, &periods, &dir);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Cannot set maximum periods per buffer: %s\n", snd_strerror(err));
		return false;
	}

	buffer_size_max = BUFFER_SIZE_MAX;
	err = snd_pcm_hw_params_set_buffer_size_max(handle, hwparams, &buffer_size_max);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Cannot set maximum buffer size: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Unable to install hw params: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_hw_params_get_buffer_size(hwparams, &buffer_size);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Cannot get buffer size: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_hw_params_get_periods_max(hwparams, &periods, &dir);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Cannot get periods: %s\n", snd_strerror(err));
		return false;
	}

	//periods is the number of fragments alsa can wait for during one
	//buffer_size
	frames_to_deliver = buffer_size / periods;
	//limit the minimum size. pulseaudio advertises a minimum of 32 samples.
	if (frames_to_deliver < FRAME_COUNT_MIN)
		frames_to_deliver = FRAME_COUNT_MIN;
	//it is probably a bad idea to try to send more than one buffer of data
	if ((unsigned int)frames_to_deliver > buffer_size)
		frames_to_deliver = buffer_size;
	NOTICE_LOG(AUDIO, "ALSA gave us a %ld sample \"hardware\" buffer with %d periods. Will send %d samples per fragments.\n", buffer_size, periods, frames_to_deliver);

	snd_pcm_sw_params_alloca(&swparams);

	err = snd_pcm_sw_params_current(handle, swparams);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "cannot init sw params: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, 0U);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "cannot set start thresh: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "cannot set sw params: %s\n", snd_strerror(err));
		return false;
	}

	err = snd_pcm_prepare(handle);
	if (err < 0)
	{
		ERROR_LOG(AUDIO, "Unable to prepare: %s\n", snd_strerror(err));
		return false;
	}
	NOTICE_LOG(AUDIO, "ALSA successfully initialized.\n");
	return true;
}

void AlsaSound::AlsaShutdown()
{
	if (handle != nullptr)
	{
		snd_pcm_drop(handle);
		snd_pcm_close(handle);
		handle = nullptr;
	}
}

