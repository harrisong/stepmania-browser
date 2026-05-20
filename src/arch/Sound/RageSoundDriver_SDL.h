// RageSoundDriver_SDL - SDL2 audio driver header
#ifndef RAGE_SOUND_DRIVER_SDL_H
#define RAGE_SOUND_DRIVER_SDL_H

#include "RageSoundDriver.h"

#ifdef HAVE_SDL

#include <SDL2/SDL_audio.h>
#include <atomic>

class RageSoundDriver_SDL : public RageSoundDriver
{
public:
	RageSoundDriver_SDL();
	virtual ~RageSoundDriver_SDL();

	RString Init() override;
	int64_t GetPosition() const override;
	int GetSampleRate() const override;
	float GetPlayLatency() const override;

	// Update is called from the main loop on Emscripten so we can pump
	// the decode pipeline inline (the background decode thread can't run).
	void Update() override;

	// Audio mixing callback (called by SDL/Web Audio on the audio thread).
	void AudioCallback(int16_t *stream, int bytes);

private:
	SDL_AudioDeviceID m_deviceID;
	int m_iSampleRate;
	int m_iChannels;
	int m_iSamplesPerFrame;
	std::atomic<int64_t> m_iLastPosition;
};

#endif // HAVE_SDL

#endif // RAGE_SOUND_DRIVER_SDL_H

/*
 * Copyright (c) 2026 StepMania Browser Port Contributors
 * All rights reserved.
 */
