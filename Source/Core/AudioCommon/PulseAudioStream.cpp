// Copyright 2009 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstring>

#include "AudioCommon/DPL2Decoder.h"
#include "AudioCommon/PulseAudioStream.h"
#include "Common/CommonTypes.h"
#include "Common/Thread.h"
#include "Common/Logging/Log.h"
#include "Core/ConfigManager.h"

namespace
{
const size_t BUFFER_SAMPLES = 512; // ~10 ms - needs to be at least 240 for surround
}

PulseAudio::PulseAudio()
	: m_thread()
	, m_run_thread()
{
}

bool PulseAudio::Start()
{
	m_stereo = !SConfig::GetInstance().bDPL2Decoder;
	m_channels = m_stereo ? 2 : 5; // will tell PA we use a Stereo or 5.0 channel setup

	NOTICE_LOG(AUDIO, "PulseAudio backend using %d channels", m_channels);

	m_run_thread = true;
	m_thread = std::thread(&PulseAudio::SoundLoop, this);

	// Initialize DPL2 parameters
	DPL2Reset();

	return true;
}

void PulseAudio::Stop()
{
	m_run_thread = false;
	m_thread.join();
}

void PulseAudio::Update()
{
	// don't need to do anything here.
}

// Called on audio thread.
void PulseAudio::SoundLoop()
{
	Common::SetCurrentThreadName("Audio thread - pulse");

	if (PulseInit())
	{
		while (m_run_thread.load() && m_pa_connected == 1 && m_pa_error >= 0)
			m_pa_error = pa_mainloop_iterate(m_pa_ml, 1, nullptr);

		if (m_pa_error < 0)
			ERROR_LOG(AUDIO, "PulseAudio error: %s", pa_strerror(m_pa_error));

		PulseShutdown();
	}
}

bool PulseAudio::PulseInit()
{
	m_pa_error = 0;
	m_pa_connected = 0;

	// create pulseaudio main loop and context
	// also register the async state callback which is called when the connection to the pa server has changed
	m_pa_ml = pa_mainloop_new();
	m_pa_mlapi = pa_mainloop_get_api(m_pa_ml);
	m_pa_ctx = pa_context_new(m_pa_mlapi, "dolphin-emu");
	m_pa_error = pa_context_connect(m_pa_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
	pa_context_set_state_callback(m_pa_ctx, StateCallback, this);

	// wait until we're connected to the pulseaudio server
	while (m_pa_connected == 0 && m_pa_error >= 0)
		m_pa_error = pa_mainloop_iterate(m_pa_ml, 1, nullptr);

	if (m_pa_connected == 2 || m_pa_error < 0)
	{
		ERROR_LOG(AUDIO, "PulseAudio failed to initialize: %s", pa_strerror(m_pa_error));
		return false;
	}

	// create a new audio stream with our sample format
	// also connect the callbacks for this stream
	pa_sample_spec ss;
	pa_channel_map channel_map;
	pa_channel_map* channel_map_p = nullptr; // auto channel map
	if (m_stereo)
	{
		ss.format = PA_SAMPLE_S16LE;
		m_bytespersample = sizeof(s16);
	}
	else
	{
		// surround is remixed in floats, use a float PA buffer to save another conversion
		ss.format = PA_SAMPLE_FLOAT32NE;
		m_bytespersample = sizeof(float);

		channel_map_p = &channel_map; // explicit channel map:
		channel_map.channels = 5;
		channel_map.map[0] = PA_CHANNEL_POSITION_FRONT_LEFT;
		channel_map.map[1] = PA_CHANNEL_POSITION_FRONT_RIGHT;
		channel_map.map[2] = PA_CHANNEL_POSITION_FRONT_CENTER;
		channel_map.map[3] = PA_CHANNEL_POSITION_REAR_LEFT;
		channel_map.map[4] = PA_CHANNEL_POSITION_REAR_RIGHT;
	}
	ss.channels = m_channels;
	ss.rate = m_mixer->GetSampleRate();
	assert(pa_sample_spec_valid(&ss));
	m_pa_s = pa_stream_new(m_pa_ctx, "Playback", &ss, channel_map_p);
	pa_stream_set_write_callback(m_pa_s, WriteCallback, this);
	pa_stream_set_underflow_callback(m_pa_s, UnderflowCallback, this);

	// connect this audio stream to the default audio playback
	// limit buffersize to reduce latency
	m_pa_ba.fragsize = -1;
	m_pa_ba.maxlength = -1;          // max buffer, so also max latency
	m_pa_ba.minreq = -1;             // don't read every byte, try to group them _a bit_
	m_pa_ba.prebuf = -1;             // start as early as possible
	m_pa_ba.tlength = BUFFER_SAMPLES * m_channels * m_bytespersample; // designed latency, only change this flag for low latency output
	pa_stream_flags flags = pa_stream_flags(PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE);
	m_pa_error = pa_stream_connect_playback(m_pa_s, nullptr, &m_pa_ba, flags, nullptr, nullptr);
	if (m_pa_error < 0)
	{
		ERROR_LOG(AUDIO, "PulseAudio failed to initialize: %s", pa_strerror(m_pa_error));
		return false;
	}

	INFO_LOG(AUDIO, "Pulse successfully initialized");
	return true;
}

void PulseAudio::PulseShutdown()
{
	pa_context_disconnect(m_pa_ctx);
	pa_context_unref(m_pa_ctx);
	pa_mainloop_free(m_pa_ml);
}

void PulseAudio::StateCallback(pa_context* c)
{
	pa_context_state_t state = pa_context_get_state(c);
	switch (state)
	{
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		m_pa_connected = 2;
		break;
	case PA_CONTEXT_READY:
		m_pa_connected = 1;
		break;
	default:
		break;
	}
}
// on underflow, increase pulseaudio latency in ~10ms steps
void PulseAudio::UnderflowCallback(pa_stream* s)
{
	m_pa_ba.tlength += BUFFER_SAMPLES * m_channels * m_bytespersample;
	pa_operation* op = pa_stream_set_buffer_attr(s, &m_pa_ba, nullptr, nullptr);
	pa_operation_unref(op);

	WARN_LOG(AUDIO, "pulseaudio underflow, new latency: %d bytes", m_pa_ba.tlength);
}

void PulseAudio::WriteCallback(pa_stream* s, size_t length)
{
	int bytes_per_frame = m_channels * m_bytespersample;
	int frames = (length / bytes_per_frame);
	size_t trunc_length = frames * bytes_per_frame;

	// fetch dst buffer directly from pulseaudio, so no memcpy is needed
	void* buffer;
	m_pa_error = pa_stream_begin_write(s, &buffer, &trunc_length);

	if (!buffer || m_pa_error < 0)
		return; // error will be printed from main loop

	if (m_stereo)
	{
		// use the raw s16 stereo mix
		m_mixer->Mix((s16*) buffer, frames);
	}
	else
	{
		// get a floating point mix
		s16 s16buffer_stereo[frames * 2];
		m_mixer->Mix(s16buffer_stereo, frames); // implicitly mixes to 16-bit stereo

		float floatbuffer_stereo[frames * 2];
		// s16 to float
		for (int i=0; i < frames * 2; ++i)
		{
			floatbuffer_stereo[i] = s16buffer_stereo[i] / float(1 << 15);
		}

		if (m_channels == 5) // Extract dpl2/5.0 Surround
		{
			float floatbuffer_6chan[frames * 6];
			// DPL2Decode output: LEFTFRONT, RIGHTFRONT, CENTREFRONT, (sub), LEFTREAR, RIGHTREAR
			DPL2Decode(floatbuffer_stereo, frames, floatbuffer_6chan);

			// Discard the subwoofer channel - DPL2Decode generates a pretty
			// good 5.0 but not a good 5.1 output.
			const int dpl2_to_5chan[] = {0,1,2,4,5};
			for (int i=0; i < frames; ++i)
			{
				for (int j=0; j < m_channels; ++j)
				{
					((float*)buffer)[m_channels * i + j] = floatbuffer_6chan[6 * i + dpl2_to_5chan[j]];
				}
			}
		}
		else
		{
			ERROR_LOG(AUDIO, "Unsupported number of PA channels requested: %d", (int)m_channels);
			return;
		}
	}

	m_pa_error = pa_stream_write(s, buffer, trunc_length, nullptr, 0, PA_SEEK_RELATIVE);
}

// Callbacks that forward to internal methods (required because PulseAudio is a C API).

void PulseAudio::StateCallback(pa_context* c, void* userdata)
{
	PulseAudio* p = (PulseAudio*) userdata;
	p->StateCallback(c);
}

void PulseAudio::UnderflowCallback(pa_stream* s, void* userdata)
{
	PulseAudio* p = (PulseAudio*) userdata;
	p->UnderflowCallback(s);
}

void PulseAudio::WriteCallback(pa_stream* s, size_t length, void* userdata)
{
	PulseAudio* p = (PulseAudio*) userdata;
	p->WriteCallback(s, length);
}
