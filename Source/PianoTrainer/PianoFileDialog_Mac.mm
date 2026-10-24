#include "PianoFileDialog.h"

#if PLATFORM_MAC

#include "Mac/CocoaThread.h"
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

static bool OpenPanel(NSArray<NSString*>* Extensions, NSString* Title, FString& OutPath)
{
	__block FString Picked;
	MainThreadCall(^{
		NSOpenPanel* Panel = [NSOpenPanel openPanel];
		Panel.canChooseFiles = YES;
		Panel.canChooseDirectories = NO;
		Panel.allowsMultipleSelection = NO;
		Panel.title = Title;
		NSMutableArray<UTType*>* Types = [NSMutableArray array];
		for (NSString* Ext in Extensions)
		{
			if (UTType* Type = [UTType typeWithFilenameExtension:Ext])
			{
				[Types addObject:Type];
			}
		}
		Panel.allowedContentTypes = Types;
		if ([Panel runModal] == NSModalResponseOK && Panel.URLs.count > 0)
		{
			Picked = FString(Panel.URLs[0].path);
		}
	}, NSDefaultRunLoopMode, true);

	if (Picked.IsEmpty())
	{
		return false;
	}
	OutPath = Picked;
	return true;
}

bool PianoFileDialog::OpenScore(FString& OutPath)
{
	return OpenPanel(@[ @"musicxml", @"xml", @"mxl", @"mei", @"krn", @"abc", @"mscz", @"mscx" ], @"Open a score", OutPath);
}

bool PianoFileDialog::OpenInstrument(FString& OutPath)
{
	return OpenPanel(@[ @"sfz", @"vst3" ], @"Choose an instrument (.sfz or .vst3)", OutPath); // a bundle is picked as one file
}

#endif
