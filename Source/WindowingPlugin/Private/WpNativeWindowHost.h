// Developed by Neko Creative Technologies

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWindow.h"
#include "WpWindowTypes.h"

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

private:
	TSharedPtr<SWindow> Window;
};
