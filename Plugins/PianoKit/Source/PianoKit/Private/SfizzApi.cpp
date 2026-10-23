#include "SfizzApi.h"

#include "PianoKitLog.h"
#include "PianoSession.h"

#include "HAL/PlatformProcess.h"

FSfizzApi& FSfizzApi::Get()
{
	static FSfizzApi Api;
	return Api;
}

bool FSfizzApi::Load(void* DllHandle)
{
	bLoaded = false;
	if (!DllHandle)
	{
		return false;
	}

	bool bAll = true;
	auto Resolve = [&](auto& Fn, const TCHAR* Name)
	{
		Fn = reinterpret_cast<std::remove_reference_t<decltype(Fn)>>(FPlatformProcess::GetDllExport(DllHandle, Name));
		if (!Fn)
		{
			UE_LOG(LogPianoKit, Error, TEXT("sfizz is missing export %s"), Name);
			bAll = false;
		}
	};
	Resolve(create_synth, TEXT("sfizz_create_synth"));
	Resolve(free, TEXT("sfizz_free"));
	Resolve(load_file, TEXT("sfizz_load_file"));
	Resolve(load_string, TEXT("sfizz_load_string"));
	Resolve(set_sample_rate, TEXT("sfizz_set_sample_rate"));
	Resolve(set_samples_per_block, TEXT("sfizz_set_samples_per_block"));
	Resolve(set_num_voices, TEXT("sfizz_set_num_voices"));
	Resolve(get_num_voices, TEXT("sfizz_get_num_voices"));
	Resolve(get_num_regions, TEXT("sfizz_get_num_regions"));
	Resolve(get_num_preloaded_samples, TEXT("sfizz_get_num_preloaded_samples"));
	Resolve(get_num_active_voices, TEXT("sfizz_get_num_active_voices"));
	Resolve(set_volume, TEXT("sfizz_set_volume"));
	Resolve(send_note_on, TEXT("sfizz_send_note_on"));
	Resolve(send_note_off, TEXT("sfizz_send_note_off"));
	Resolve(send_cc, TEXT("sfizz_send_cc"));
	Resolve(all_sound_off, TEXT("sfizz_all_sound_off"));
	Resolve(render_block, TEXT("sfizz_render_block"));
	bLoaded = bAll;
	return bAll;
}

FPianoSynth::~FPianoSynth()
{
	if (Synth && FSfizzApi::Get().IsLoaded())
	{
		FSfizzApi::Get().free(Synth);
	}
}
