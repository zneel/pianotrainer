// sfizz's C API (sfizz.h), resolved at runtime from sfizz.dll / libsfizz.dylib, the same way as Verovio.
#pragma once

#include "CoreMinimal.h"

struct sfizz_synth_t;

struct FSfizzApi
{
	sfizz_synth_t* (*create_synth)() = nullptr;
	void (*free)(sfizz_synth_t* Synth) = nullptr;
	bool (*load_file)(sfizz_synth_t* Synth, const char* Path) = nullptr;
	bool (*load_string)(sfizz_synth_t* Synth, const char* Path, const char* Text) = nullptr;
	void (*set_sample_rate)(sfizz_synth_t* Synth, float SampleRate) = nullptr;
	void (*set_samples_per_block)(sfizz_synth_t* Synth, int SamplesPerBlock) = nullptr;
	void (*set_num_voices)(sfizz_synth_t* Synth, int NumVoices) = nullptr;
	int (*get_num_voices)(sfizz_synth_t* Synth) = nullptr;
	int (*get_num_regions)(sfizz_synth_t* Synth) = nullptr;
	size_t (*get_num_preloaded_samples)(sfizz_synth_t* Synth) = nullptr;
	int (*get_num_active_voices)(sfizz_synth_t* Synth) = nullptr;
	void (*set_volume)(sfizz_synth_t* Synth, float VolumeDb) = nullptr;
	void (*send_note_on)(sfizz_synth_t* Synth, int Delay, int Note, int Velocity) = nullptr;
	void (*send_note_off)(sfizz_synth_t* Synth, int Delay, int Note, int Velocity) = nullptr;
	void (*send_cc)(sfizz_synth_t* Synth, int Delay, int Cc, int Value) = nullptr;
	void (*send_hdcc)(sfizz_synth_t* Synth, int Delay, int Cc, float NormValue) = nullptr;
	void (*set_sample_quality)(sfizz_synth_t* Synth, int Mode, int Quality) = nullptr; // Mode 0: live
	void (*all_sound_off)(sfizz_synth_t* Synth) = nullptr;
	void (*render_block)(sfizz_synth_t* Synth, float** Channels, int NumChannels, int NumFrames) = nullptr;

	// Resolves every entry point; returns false (and logs the missing name) if any is absent.
	bool Load(void* DllHandle);
	bool IsLoaded() const { return bLoaded; }

	static FSfizzApi& Get();

private:
	bool bLoaded = false;
};
