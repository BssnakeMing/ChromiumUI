// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if USE_ANDROID_JNI

#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ChromiumAndroidWebBrowserWindow.h"
#include "ChromiumAndroidWebBrowserDialog.h"
#include "Android/AndroidJava.h"
#include "RHI.h"
#include "RHIResources.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "ChromiumAndroidJavaWebBrowser.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WebBrowserTexture.h"

#include <jni.h>

class UMaterialExpressionTextureSample;
class FWebBrowserTextureSamplePool;

class SChromiumAndroidWebBrowserWidget : public SLeafWidget
{
	SLATE_BEGIN_ARGS(SChromiumAndroidWebBrowserWidget)
		: _InitialURL("about:blank")
		, _UseTransparency(false)
	{ }

		SLATE_ARGUMENT(FString, InitialURL);
		SLATE_ARGUMENT(bool, UseTransparency);
		SLATE_ARGUMENT(TSharedPtr<FChromiumAndroidWebBrowserWindow>, WebBrowserWindow);

	SLATE_END_ARGS()

public:
	virtual ~SChromiumAndroidWebBrowserWidget();

	void Construct(const FArguments& Args);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FVector2D ComputeDesiredSize(float) const override;

	void ExecuteJavascript(const FString& Script);
	void LoadURL(const FString& NewURL);
	void LoadString(const FString& Content, const FString& BaseUrl);

	void StopLoad();
	void Reload();
	void Close();
	void GoBack();
	void GoForward();
	bool CanGoBack();
	bool CanGoForward();

	// WebViewClient callbacks

	jbyteArray HandleShouldInterceptRequest(jstring JUrl);

	bool HandleShouldOverrideUrlLoading(jstring JUrl);
	bool HandleJsDialog(EChromiumWebBrowserDialogType Type, jstring JUrl, jstring MessageText, jobject ResultCallback)
	{
		TSharedPtr<IChromiumWebBrowserDialog> Dialog(new FChromiumWebBrowserDialog(Type, MessageText, ResultCallback));
		return HandleJsDialog(Dialog);
	}

	bool HandleJsPrompt(jstring JUrl, jstring MessageText, jstring DefaultPrompt, jobject ResultCallback)
	{
		TSharedPtr<IChromiumWebBrowserDialog> Dialog(new FChromiumWebBrowserDialog(MessageText, DefaultPrompt, ResultCallback));
		return HandleJsDialog(Dialog);
	}

	void HandleReceivedTitle(jstring JTitle);
	void HandlePageLoad(jstring JUrl, bool bIsLoading, int InHistorySize, int InHistoryPosition);
	void HandleReceivedError(jint ErrorCode, jstring JMessage, jstring JUrl);

	// Helper to get the native widget pointer from a Java callback.
	// Jobj can either be a WebViewControl, a WebViewControl.ViewClient or WebViewControl.ChromeClient instance
	static TSharedPtr<SChromiumAndroidWebBrowserWidget> GetWidgetPtr(JNIEnv* JEnv, jobject Jobj);

	//set the native control's visibility
	void SetWebBrowserVisibility(bool InIsVisible);
protected:
	static FCriticalSection WebControlsCS;
	static TMap<int64, TWeakPtr<SChromiumAndroidWebBrowserWidget>> AllWebControls;

	bool HandleJsDialog(TSharedPtr<IChromiumWebBrowserDialog>& Dialog);
	int HistorySize;
	int HistoryPosition;

	TWeakPtr<FChromiumAndroidWebBrowserWindow> WebBrowserWindowPtr;
private:

	/** Enable 3D appearance for Android. */
	bool IsAndroid3DBrowser;

	/** The Java side webbrowser interface. */
	TSharedPtr<FChromiumJavaAndroidWebBrowser, ESPMode::ThreadSafe> JavaWebBrowser;

	/** The external texture to render the webbrowser output. */
	UWebBrowserTexture* WebBrowserTexture;

	/** The material for the external texture. */
	UMaterialInstanceDynamic* WebBrowserMaterial;

	/** The Slate brush that renders the material. */
	TSharedPtr<FSlateBrush> WebBrowserBrush;

	/** The sample queue. */
	TSharedPtr<FWebBrowserTextureSampleQueue, ESPMode::ThreadSafe> WebBrowserTextureSamplesQueue;

	/** Texture sample object pool. */
	FWebBrowserTextureSamplePool* TextureSamplePool;
};

#endif // USE_ANDROID_JNI