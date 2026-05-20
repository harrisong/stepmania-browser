// RageSoundDriver_SDL - SDL2 audio driver. On Emscripten this is wired
// to the Web Audio API by Emscripten's SDL2 port.

#include "global.h"
#include "RageSoundDriver_SDL.h"
#include "RageLog.h"
#include "RageSound.h"
#include "RageUtil.h"
#include "Preference.h"

#ifdef HAVE_SDL

#include <SDL2/SDL.h>

REGISTER_SOUND_DRIVER_CLASS(SDL);

static Preference<int> g_iSampleRate("SoundSampleRate", 44100);

static void SDLAudioCallback(void *userdata, Uint8 *stream, int len)
{
	RageSoundDriver_SDL *driver = static_cast<RageSoundDriver_SDL *>(userdata);
	driver->AudioCallback(reinterpret_cast<int16_t *>(stream), len);
}

RageSoundDriver_SDL::RageSoundDriver_SDL():
	m_deviceID(0),
	m_iSampleRate(44100),
	m_iChannels(2),
	m_iSamplesPerFrame(2048),
	m_iLastPosition(0)
{
}

RageSoundDriver_SDL::~RageSoundDriver_SDL()
{
	if (m_deviceID > 0)
	{
		SDL_PauseAudioDevice(m_deviceID, 1);
		SDL_CloseAudioDevice(m_deviceID);
		m_deviceID = 0;
	}
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

RString RageSoundDriver_SDL::Init()
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
		return ssprintf("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s", SDL_GetError());

	SDL_AudioSpec want, have;
	SDL_zero(want);

	want.freq = g_iSampleRate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 2048;
	want.callback = SDLAudioCallback;
	want.userdata = this;

	// Don't allow format changes — RageSoundDriver::Mix only emits S16 stereo.
	m_deviceID = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (m_deviceID == 0)
		return ssprintf("SDL_OpenAudioDevice failed: %s", SDL_GetError());

	m_iSampleRate = have.freq;
	m_iChannels = have.channels;
	m_iSamplesPerFrame = have.samples;

	LOG->Info("SDL Audio: %d Hz, %d ch, %d-sample buffer", m_iSampleRate, m_iChannels, m_iSamplesPerFrame);

	if (have.format != AUDIO_S16SYS || have.channels != 2)
	{
		SDL_CloseAudioDevice(m_deviceID);
		m_deviceID = 0;
		return ssprintf("SDL audio format mismatch (got format=%d channels=%d)", have.format, have.channels);
	}

	StartDecodeThread();

	SDL_PauseAudioDevice(m_deviceID, 0);
	return RString();
}

void RageSoundDriver_SDL::AudioCallback(int16_t *stream, int bytes)
{
	const int iSamples = bytes / sizeof(int16_t);
	const int iFrames = iSamples / 2; // stereo

	int64_t iPos1 = m_iLastPosition.load(std::memory_order_relaxed);
	int64_t iPos2 = iPos1 + iFrames;
	this->Mix(stream, iFrames, iPos1, iPos2);
	m_iLastPosition.store(iPos2, std::memory_order_release);
}

int64_t RageSoundDriver_SDL::GetPosition() const
{
	return m_iLastPosition.load(std::memory_order_acquire);
}

int RageSoundDriver_SDL::GetSampleRate() const
{
	return m_iSampleRate;
}

float RageSoundDriver_SDL::GetPlayLatency() const
{
	return float(m_iSamplesPerFrame) / float(m_iSampleRate);
}

void RageSoundDriver_SDL::Update()
{
#ifdef __EMSCRIPTEN__
	// Background decode thread can't run on single-threaded Emscripten.
	// Pump decoding inline so the per-sound buffers stay full.
	DecodePlayingSounds();
#endif
	RageSoundDriver::Update();
}

#endif // HAVE_SDL

/*
 * Copyright (c) 2026 StepMania Browser Port Contributors
 * All rights reserved.
 */
