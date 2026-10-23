#include "PianoFileDialog.h"

#if PLATFORM_MAC

#include "Mac/CocoaThread.h"
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

bool PianoFileDialog::OpenScore(FString& OutPath)
{
	__block FString Picked;
	MainThreadCall(^{
		NSOpenPanel* Panel = [NSOpenPanel openPanel];
		Panel.canChooseFiles = YES;
		Panel.canChooseDirectories = NO;
		Panel.allowsMultipleSelection = NO;
		Panel.title = @"Open a score";
		NSMutableArray<UTType*>* Types = [NSMutableArray array];
		for (NSString* Ext in @[ @"musicxml", @"xml", @"mxl", @"mei", @"krn", @"abc", @"mscz", @"mscx" ])
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

#endif
