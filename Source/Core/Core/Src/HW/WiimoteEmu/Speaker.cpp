// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "WiimoteEmu.h"

//#define WIIMOTE_SPEAKER_DUMP
#ifdef WIIMOTE_SPEAKER_DUMP
#include <fstream>
#include "WaveFile.h"
#include <stdlib.h>
#include "FileUtil.h"
#endif

namespace WiimoteEmu
{

// Yamaha ADPCM decoder code based on The ffmpeg Project (Copyright (s) 2001-2003)

static const s32 yamaha_difflookup[] = {
	1, 3, 5, 7, 9, 11, 13, 15,
	-1, -3, -5, -7, -9, -11, -13, -15
};

static const s32 yamaha_indexscale[] = {
	230, 230, 230, 230, 307, 409, 512, 614,
	230, 230, 230, 230, 307, 409, 512, 614
};

static u16 av_clip16(s32 a)
{
	if ((a+32768) & ~65535) return (a>>31) ^ 32767;
	else                    return a;
}

static s32 av_clip(s32 a, s32 amin, s32 amax)
{
	if      (a < amin) return amin;
	else if (a > amax) return amax;
	else               return a;
}

static s16 adpcm_yamaha_expand_nibble(ADPCMState& s, u8 nibble)
{
	if(!s.step) {
		s.predictor = 0;
		s.step = 0;
	}

	s.predictor += (s.step * yamaha_difflookup[nibble]) / 8;
	s.predictor = av_clip16(s.predictor);
	s.step = (s.step * yamaha_indexscale[nibble]) >> 8;
	s.step = av_clip(s.step, 127, 24576);
	return s.predictor;
}

#ifdef WIIMOTE_SPEAKER_DUMP
std::ofstream ofile;
WaveFileWriter wav;

void stopdamnwav(){wav.Stop();ofile.close();}
#endif

void Wiimote::SpeakerData(wm_speaker_data* sd)
{
	// TODO consider using static max size instead of new
	s16 *samples = new s16[sd->length * 2];

	if (m_reg_speaker.format == 0x40)
	{
		// 8 bit PCM
		for (int i = 0; i < sd->length; ++i)
		{
			samples[i] = (s16)(s8)sd->data[i];
		}
	}
	else if (m_reg_speaker.format == 0x00)
	{
		// 4 bit Yamaha ADPCM (same as dreamcast)
		for (int i = 0; i < sd->length; ++i)
		{
			samples[i * 2] = adpcm_yamaha_expand_nibble(m_adpcm_state, (sd->data[i] >> 4) & 0xf);
			samples[i * 2 + 1] = adpcm_yamaha_expand_nibble(m_adpcm_state, sd->data[i] & 0xf);
		}
	}

#ifdef WIIMOTE_SPEAKER_DUMP
	std::stringstream name;
	static int num = 0;

	if (num == 0)
	{
		File::Delete("rmtdump.wav");
		File::Delete("rmtdump.bin");
		atexit(stopdamnwav);
		OpenFStream(ofile, "rmtdump.bin", ofile.binary | ofile.out);
		wav.Start("rmtdump.wav", 6000/*Common::swap16(m_reg_speaker.sample_rate)*/);
	}
	wav.AddMonoSamples(samples, sd->length*2);
	if (ofile.good())
	{
		for (int i = 0; i < sd->length; i++)
		{
			ofile << sd->data[i];
		}
	}
	num++;
#endif

	delete[] samples;
}

}
