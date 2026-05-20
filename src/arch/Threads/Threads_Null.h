#ifndef THREADS_NULL_H
#define THREADS_NULL_H

#include "Threads.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Stub threading implementation for single-threaded environments (Emscripten without pthreads)

class ThreadImpl_Null: public ThreadImpl
{
public:
	ThreadImpl_Null() {}
	virtual ~ThreadImpl_Null();
	void Halt( bool /* Kill */ ) override {}
	void Resume() override {}
	uint64_t GetThreadId() const override { return 1; }
	int Wait() override { return 0; }
};

class MutexImpl_Null: public MutexImpl
{
public:
	MutexImpl_Null( RageMutex *pParent ): MutexImpl(pParent) {}
	virtual ~MutexImpl_Null();
	bool Lock() override { return true; }
	bool TryLock() override { return true; }
	void Unlock() override {}
};

class EventImpl_Null: public EventImpl
{
public:
	virtual ~EventImpl_Null();
	bool Wait( RageTimer * /* pTimeout */ ) override { return true; }
	void Signal() override {}
	void Broadcast() override {}
	bool WaitTimeoutSupported() const override { return false; }
};

class SemaImpl_Null: public SemaImpl
{
private:
	int m_iValue;
public:
	SemaImpl_Null( int iInitialValue ): m_iValue(iInitialValue) {}
	virtual ~SemaImpl_Null();
	int GetValue() const override { return m_iValue; }
	void Post() override { m_iValue++; }
	bool Wait() override { if (m_iValue > 0) { m_iValue--; return true; } return false; }
	bool TryWait() override { if (m_iValue > 0) { m_iValue--; return true; } return false; }
};

#endif
