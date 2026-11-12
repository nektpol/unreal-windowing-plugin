// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWindow.h"
#include "WpWindowTypes.h"

class SBorder;

class FWpNativeWindowHost final
{
public:
	void Construct(const FWpWindowCreateOptions& Options,
		const TSharedRef<SWidget>& Content, const FOnWindowClosed& PreClose);
	bool RegisterWindow();
	bool ShowWindow();
	void SetTitle(const FText& Title);
	void RequestDestroy();
	bool IsTracked() const;
	bool HasNativeHandle() const;
	void ReleaseResources();
	void* GetNativeHandle() const;
	void MaximizeWindow();
	bool IsMaximized() const;
	void SetContent(const TSharedRef<SWidget>& Content);

	bool IsSlateWindow(const SWindow* Candidate) const
	{
		return Candidate && Window.Get() == Candidate;
	}

	static void* GetForegroundHandle();

private:
	TSharedPtr<SWindow> Window;
	TSharedPtr<SBorder> ContentBackground;
};
