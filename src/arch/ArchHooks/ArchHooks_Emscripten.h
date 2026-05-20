#ifndef ARCH_HOOKS_EMSCRIPTEN_H
#define ARCH_HOOKS_EMSCRIPTEN_H

#include "ArchHooks.h"

class ArchHooks_Emscripten: public ArchHooks
{
public:
	void Init();
	RString GetArchName() const override;
	void DumpDebugInfo();

	void SetTime( tm newtime );

	float GetDisplayAspectRatio();

	bool GoToURL( RString sUrl );

	RString GetClipboard();
};

#ifdef ARCH_HOOKS
#error "More than one ArchHooks selected!"
#endif
#define ARCH_HOOKS ArchHooks_Emscripten

#endif
