#include "global.h"
#include "Threads_Null.h"
#include "RageLog.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Stub threading implementation for single-threaded environments
// All threading operations are no-ops since we run single-threaded in browser

// Force vtable emission for Emscripten by providing non-inline virtual destructors
ThreadImpl_Null::~ThreadImpl_Null() {}
MutexImpl_Null::~MutexImpl_Null() {}
EventImpl_Null::~EventImpl_Null() {}
SemaImpl_Null::~SemaImpl_Null() {}

// Force these virtual methods into the indirect function table
#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE bool _force_mutex_lock(MutexImpl_Null *m) { return m->Lock(); }
EMSCRIPTEN_KEEPALIVE bool _force_mutex_trylock(MutexImpl_Null *m) { return m->TryLock(); }
EMSCRIPTEN_KEEPALIVE void _force_mutex_unlock(MutexImpl_Null *m) { m->Unlock(); }
}
#endif

static const uint64_t INVALID_THREAD_ID = 0;
static const uint64_t MAIN_THREAD_ID = 1;

uint64_t GetInvalidThreadId()
{
	return INVALID_THREAD_ID;
}

uint64_t GetThisThreadId()
{
	// Always return main thread ID in single-threaded environment
	return MAIN_THREAD_ID;
}

ThreadImpl *MakeThisThread()
{
	return new ThreadImpl_Null;
}

ThreadImpl *MakeThread( int (*fn)(void *), void *data, uint64_t *pID )
{
	// Cannot create threads in single-threaded environment
	LOG->Warn("MakeThread: Thread creation not supported in single-threaded mode");
	if (pID)
		*pID = INVALID_THREAD_ID;
	return nullptr;
}

MutexImpl *MakeMutex( RageMutex *pParent )
{
	return new MutexImpl_Null(pParent);
}

EventImpl *MakeEvent( MutexImpl * /* pMutex */ )
{
	return new EventImpl_Null;
}

SemaImpl *MakeSemaphore( int iInitialValue )
{
	return new SemaImpl_Null(iInitialValue);
}

/*
 * (c) 2025 Browser port contributors
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
