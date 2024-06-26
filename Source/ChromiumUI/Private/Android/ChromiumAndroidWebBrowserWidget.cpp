// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChromiumAndroidWebBrowserWidget.h"

#if USE_ANDROID_JNI

#include "ChromiumAndroidWebBrowserWindow.h"
#include "ChromiumAndroidWebBrowserDialog.h"
#include "MobileJS/ChromiumMobileJSScripting.h"
#include "Android/AndroidApplication.h"
#include "Android/AndroidWindow.h"
#include "Android/AndroidJava.h"
#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "ExternalTexture.h"
#include "Slate/SlateTextures.h"
#include "SlateMaterialBrush.h"
#include "Templates/SharedPointer.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "WebBrowserTextureSample.h"
#include "ChromiumWebBrowserModule.h"
#include "IChromiumWebBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"

// For UrlDecode
#include "Http.h"

#include <jni.h>

FCriticalSection SChromiumAndroidWebBrowserWidget::WebControlsCS;
TMap<int64, TWeakPtr<SChromiumAndroidWebBrowserWidget>> SChromiumAndroidWebBrowserWidget::AllWebControls;

TSharedPtr<SChromiumAndroidWebBrowserWidget> SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JNIEnv* JEnv, jobject Jobj)
{
	FScopeLock L(&WebControlsCS);

	auto Class = NewScopedJavaObject(JEnv, JEnv->GetObjectClass(Jobj));
	jmethodID JMethod = JEnv->GetMethodID(*Class, "GetNativePtr", "()J");
	check(JMethod != nullptr);

	int64 ObjAddr = JEnv->CallLongMethod(Jobj, JMethod);

	TWeakPtr<SChromiumAndroidWebBrowserWidget> WebControl = AllWebControls.FindRef(ObjAddr);
	return (WebControl.IsValid()) ? WebControl.Pin() : TSharedPtr<SChromiumAndroidWebBrowserWidget>();
}

SChromiumAndroidWebBrowserWidget::~SChromiumAndroidWebBrowserWidget()
{
	if (JavaWebBrowser.IsValid())
	{
		if (GSupportsImageExternal && !FAndroidMisc::ShouldUseVulkan())
		{
			// Unregister the external texture on render thread
			FTextureRHIRef VideoTexture = JavaWebBrowser->GetVideoTexture();

			JavaWebBrowser->SetVideoTexture(nullptr);
			JavaWebBrowser->Release();

			struct FReleaseVideoResourcesParams
			{
				FTextureRHIRef VideoTexture;
				FGuid PlayerGuid;
			};

			FReleaseVideoResourcesParams ReleaseVideoResourcesParams = { VideoTexture, WebBrowserTexture->GetExternalTextureGuid() };

			ENQUEUE_RENDER_COMMAND(AndroidWebBrowserWriteVideoSample)(
				[Params = ReleaseVideoResourcesParams](FRHICommandListImmediate& RHICmdList)
				{
					FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PlayerGuid);
					// @todo: this causes a crash
					//					Params.VideoTexture->Release();
				});
		}
		else
		{
			JavaWebBrowser->SetVideoTexture(nullptr);
			JavaWebBrowser->Release();
		}

	}
	delete TextureSamplePool;
	TextureSamplePool = nullptr;
	
	WebBrowserTextureSamplesQueue->RequestFlush();

	if (WebBrowserMaterial != nullptr)
	{
		WebBrowserMaterial->RemoveFromRoot();
		WebBrowserMaterial = nullptr;
	}

	if (WebBrowserTexture != nullptr)
	{
		WebBrowserTexture->RemoveFromRoot();
		WebBrowserTexture = nullptr;
	}

	FScopeLock L(&WebControlsCS);
	AllWebControls.Remove(reinterpret_cast<int64>(this));
}

void SChromiumAndroidWebBrowserWidget::Construct(const FArguments& Args)
{
	{
		FScopeLock L(&WebControlsCS);
		AllWebControls.Add(reinterpret_cast<int64>(this), StaticCastSharedRef<SChromiumAndroidWebBrowserWidget>(AsShared()));
	}

	WebBrowserWindowPtr = Args._WebBrowserWindow;
	IsAndroid3DBrowser = true;

	HistorySize = 0;
	HistoryPosition = 0;
	
	FIntPoint viewportSize = WebBrowserWindowPtr.Pin()->GetViewportSize();
	JavaWebBrowser = MakeShared<FChromiumJavaAndroidWebBrowser, ESPMode::ThreadSafe>(false, FAndroidMisc::ShouldUseVulkan(), viewportSize.X, viewportSize.Y,
		reinterpret_cast<jlong>(this), !(UE_BUILD_SHIPPING || UE_BUILD_TEST), Args._UseTransparency);

	TextureSamplePool = new FWebBrowserTextureSamplePool();
	WebBrowserTextureSamplesQueue = MakeShared<FWebBrowserTextureSampleQueue, ESPMode::ThreadSafe>();
	WebBrowserTexture = nullptr;
	WebBrowserMaterial = nullptr;
	WebBrowserBrush = nullptr;

	// create external texture
	WebBrowserTexture = NewObject<UWebBrowserTexture>((UObject*)GetTransientPackage(), NAME_None, RF_Transient | RF_Public);

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SChromiumAndroidWebBrowserWidget::Construct0"));
	if (WebBrowserTexture != nullptr)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SChromiumAndroidWebBrowserWidget::Construct01"));

		WebBrowserTexture->UpdateResource();
		WebBrowserTexture->AddToRoot();
	}

	// create wrapper material
	IChromiumWebBrowserSingleton* WebBrowserSingleton = IChromiumWebBrowserModule::Get().GetSingleton();
	
	UMaterialInterface* DefaultWBMaterial = Args._UseTransparency? WebBrowserSingleton->GetDefaultTranslucentMaterial(): WebBrowserSingleton->GetDefaultMaterial();
	if (WebBrowserSingleton && DefaultWBMaterial)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SChromiumAndroidWebBrowserWidget::Construct1"));
		// create wrapper material
		WebBrowserMaterial = UMaterialInstanceDynamic::Create(DefaultWBMaterial, nullptr);

		if (WebBrowserMaterial)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SChromiumAndroidWebBrowserWidget::Construct2"));
			WebBrowserMaterial->SetTextureParameterValue("SlateUI", WebBrowserTexture);
			WebBrowserMaterial->AddToRoot();

			// create Slate brush
			WebBrowserBrush = MakeShareable(new FSlateBrush());
			{
				WebBrowserBrush->SetResourceObject(WebBrowserMaterial);
			}
		}
	}
	
	check(JavaWebBrowser.IsValid());

	JavaWebBrowser->LoadURL(Args._InitialURL);
}

void SChromiumAndroidWebBrowserWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if(WebBrowserWindowPtr.IsValid())
	{
		WebBrowserWindowPtr.Pin()->SetTickLastFrame();
		if (WebBrowserWindowPtr.Pin()->GetParentWindow().IsValid())
		{
			bool ShouldSetAndroid3DBrowser = WebBrowserWindowPtr.Pin()->GetParentWindow().Get()->IsVirtualWindow();
			if (IsAndroid3DBrowser != ShouldSetAndroid3DBrowser)
			{
				IsAndroid3DBrowser = ShouldSetAndroid3DBrowser;
				JavaWebBrowser->SetAndroid3DBrowser(IsAndroid3DBrowser);
			}
		}
	}

	if (!JavaWebBrowser.IsValid())
	{	
		return;
	}
	// deal with resolution changes (usually from streams)
	if (JavaWebBrowser->DidResolutionChange())
	{
		JavaWebBrowser->SetVideoTextureValid(false);
	}

	// Calculate UIScale, which can vary frame-to-frame thanks to device rotation
	// UI Scale is calculated relative to vertical axis of 1280x720 / 720x1280
	float UIScale;
	FPlatformRect ScreenRect = FAndroidWindow::GetScreenRect();
	int32_t ScreenWidth, ScreenHeight;
	FAndroidWindow::CalculateSurfaceSize(ScreenWidth, ScreenHeight);
	if (ScreenWidth > ScreenHeight)
	{
		UIScale = (float)ScreenHeight / (ScreenRect.Bottom - ScreenRect.Top);
	}
	else
	{
		UIScale = (float)ScreenHeight / (ScreenRect.Bottom - ScreenRect.Top);
	}

	FVector2D Position = AllottedGeometry.GetAccumulatedRenderTransform().GetTranslation() * UIScale;
	FVector2D Size = TransformVector(AllottedGeometry.GetAccumulatedRenderTransform(), AllottedGeometry.GetLocalSize()) * UIScale;

	// Convert position to integer coordinates
	FIntPoint IntPos(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
	// Convert size to integer taking the rounding of position into account to avoid double round-down or double round-up causing a noticeable error.
	FIntPoint IntSize = FIntPoint(FMath::RoundToInt(Position.X + Size.X), FMath::RoundToInt(Size.Y + Position.Y)) - IntPos;

	JavaWebBrowser->Update(IntPos.X, IntPos.Y, IntSize.X, IntSize.Y);


	if (IsAndroid3DBrowser)
	{
		if (WebBrowserTexture)
		{
			TSharedPtr<FWebBrowserTextureSample, ESPMode::ThreadSafe> WebBrowserTextureSample;
			WebBrowserTextureSamplesQueue->Peek(WebBrowserTextureSample);

			WebBrowserTexture->TickResource(WebBrowserTextureSample);
		}

		if (FAndroidMisc::ShouldUseVulkan())
		{
			// create new video sample
			auto NewTextureSample = TextureSamplePool->AcquireShared();

			FIntPoint viewportSize = WebBrowserWindowPtr.Pin()->GetViewportSize();

			if (!NewTextureSample->Initialize(viewportSize))
			{
				return;
			}

			struct FWriteWebBrowserParams
			{
				TWeakPtr<FChromiumJavaAndroidWebBrowser, ESPMode::ThreadSafe> JavaWebBrowserPtr;
				TWeakPtr<FWebBrowserTextureSampleQueue, ESPMode::ThreadSafe> WebBrowserTextureSampleQueuePtr;
				TSharedRef<FWebBrowserTextureSample, ESPMode::ThreadSafe> NewTextureSamplePtr;
				int32 SampleCount;
			}
			WriteWebBrowserParams = { JavaWebBrowser, WebBrowserTextureSamplesQueue, NewTextureSample, (int32)(viewportSize.X * viewportSize.Y * sizeof(int32)) };

			ENQUEUE_RENDER_COMMAND(WriteAndroidWebBrowser)(
				[Params = WriteWebBrowserParams](FRHICommandListImmediate& RHICmdList)
				{
					auto PinnedJavaWebBrowser = Params.JavaWebBrowserPtr.Pin();
					auto PinnedSamples = Params.WebBrowserTextureSampleQueuePtr.Pin();

					if (!PinnedJavaWebBrowser.IsValid() || !PinnedSamples.IsValid())
					{
						return;
					}

					bool bRegionChanged = false;

					// write frame into buffer
					void* Buffer = nullptr;
					int64 SampleCount = 0;

					if (!PinnedJavaWebBrowser->GetVideoLastFrameData(Buffer, SampleCount, &bRegionChanged))
					{
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: ShouldUseVulkan couldn't get texture buffer"));
						return;
					}

					if (SampleCount != Params.SampleCount)
					{
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("SChromiumAndroidWebBrowserWidget::Fetch: Sample count mismatch (Buffer=%llu, Available=%llu"), Params.SampleCount, SampleCount);
					}
					check(Params.SampleCount <= SampleCount);

					// must make a copy (buffer is owned by Java, not us!)
					Params.NewTextureSamplePtr->InitializeBuffer(Buffer, true);

					PinnedSamples->RequestFlush();
					PinnedSamples->Enqueue(Params.NewTextureSamplePtr);
				});
		}
		else if (GSupportsImageExternal && WebBrowserTexture != nullptr)
		{
			struct FWriteWebBrowserParams
			{
				TWeakPtr<FChromiumJavaAndroidWebBrowser, ESPMode::ThreadSafe> JavaWebBrowserPtr;
				FGuid PlayerGuid;
				FIntPoint Size;
			};

			FIntPoint viewportSize = WebBrowserWindowPtr.Pin()->GetViewportSize();

			FWriteWebBrowserParams WriteWebBrowserParams = { JavaWebBrowser, WebBrowserTexture->GetExternalTextureGuid(), viewportSize };
			ENQUEUE_RENDER_COMMAND(WriteAndroidWebBrowser)(
				[Params = WriteWebBrowserParams](FRHICommandListImmediate& RHICmdList)
				{
					auto PinnedJavaWebBrowser = Params.JavaWebBrowserPtr.Pin();

					if (!PinnedJavaWebBrowser.IsValid())
					{
						return;
					}

					FTextureRHIRef VideoTexture = PinnedJavaWebBrowser->GetVideoTexture();
					if (VideoTexture == nullptr)
					{
						FRHIResourceCreateInfo CreateInfo;
						FIntPoint LocalSize = Params.Size;

						VideoTexture = RHICreateTextureExternal2D(LocalSize.X, LocalSize.Y, PF_R8G8B8A8, 1, 1, TexCreate_None, CreateInfo);
						PinnedJavaWebBrowser->SetVideoTexture(VideoTexture);

						if (VideoTexture == nullptr)
						{
							UE_LOG(LogAndroid, Warning, TEXT("CreateTextureExternal2D failed!"));
							return;
						}

						PinnedJavaWebBrowser->SetVideoTextureValid(false);
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: Created VideoTexture: %d - %s (%d, %d)"), *reinterpret_cast<int32*>(VideoTexture->GetNativeResource()), *Params.PlayerGuid.ToString(), LocalSize.X, LocalSize.Y);
					}

					int32 TextureId = *reinterpret_cast<int32*>(VideoTexture->GetNativeResource());
					bool bRegionChanged = false;
					if (PinnedJavaWebBrowser->UpdateVideoFrame(TextureId, &bRegionChanged))
					{
						// if region changed, need to reregister UV scale/offset
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("UpdateVideoFrame RT: %s"), *Params.PlayerGuid.ToString());
						if (bRegionChanged)
						{
							PinnedJavaWebBrowser->SetVideoTextureValid(false);
							FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: %s"), *Params.PlayerGuid.ToString());
						}
					}

					if (!PinnedJavaWebBrowser->IsVideoTextureValid())
					{
						FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp);
						FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
						FExternalTextureRegistry::Get().RegisterExternalTexture(Params.PlayerGuid, VideoTexture, SamplerStateRHI);
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Fetch RT: Register Guid: %s"), *Params.PlayerGuid.ToString());

						PinnedJavaWebBrowser->SetVideoTextureValid(true);
					}
				});
		}
		else
		{
			// create new video sample
			auto NewTextureSample = TextureSamplePool->AcquireShared();

			FIntPoint viewportSize = WebBrowserWindowPtr.Pin()->GetViewportSize();

			if (!NewTextureSample->Initialize(viewportSize))
			{
				return;
			}

			// populate & add sample (on render thread)
			struct FWriteWebBrowserParams
			{
				TWeakPtr<FChromiumJavaAndroidWebBrowser, ESPMode::ThreadSafe> JavaWebBrowserPtr;
				TWeakPtr<FWebBrowserTextureSampleQueue, ESPMode::ThreadSafe> WebBrowserTextureSampleQueuePtr;
				TSharedRef<FWebBrowserTextureSample, ESPMode::ThreadSafe> NewTextureSamplePtr;
				int32 SampleCount;
			}
			WriteWebBrowserParams = { JavaWebBrowser, WebBrowserTextureSamplesQueue, NewTextureSample, (int32)(viewportSize.X * viewportSize.Y * sizeof(int32)) };

			ENQUEUE_RENDER_COMMAND(WriteAndroidWebBrowser)(
				[Params = WriteWebBrowserParams](FRHICommandListImmediate& RHICmdList)
				{
					auto PinnedJavaWebBrowser = Params.JavaWebBrowserPtr.Pin();
					auto PinnedSamples = Params.WebBrowserTextureSampleQueuePtr.Pin();

					if (!PinnedJavaWebBrowser.IsValid() || !PinnedSamples.IsValid())
					{
						return;
					}

					// write frame into texture
					FRHITexture2D* Texture = Params.NewTextureSamplePtr->InitializeTexture();

					if (Texture != nullptr)
					{
						int32 Resource = *reinterpret_cast<int32*>(Texture->GetNativeResource());
						if (!PinnedJavaWebBrowser->GetVideoLastFrame(Resource))
						{
							return;
						}
					}

					PinnedSamples->RequestFlush();
					PinnedSamples->Enqueue(Params.NewTextureSamplePtr);
				});
		}
	}
}


int32 SChromiumAndroidWebBrowserWidget::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	bool bIsVisible = !WebBrowserWindowPtr.IsValid() || WebBrowserWindowPtr.Pin()->IsVisible();

	if (bIsVisible && IsAndroid3DBrowser && WebBrowserBrush.IsValid())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WebBrowserBrush.Get(), ESlateDrawEffect::None);
	}
	return LayerId;
}

FVector2D SChromiumAndroidWebBrowserWidget::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(640, 480);
}

void SChromiumAndroidWebBrowserWidget::ExecuteJavascript(const FString& Script)
{
	JavaWebBrowser->ExecuteJavascript(Script);
}

void SChromiumAndroidWebBrowserWidget::LoadURL(const FString& NewURL)
{
	JavaWebBrowser->LoadURL(NewURL);
}

void SChromiumAndroidWebBrowserWidget::LoadString(const FString& Contents, const FString& BaseUrl)
{
	JavaWebBrowser->LoadString(Contents, BaseUrl);
}

void SChromiumAndroidWebBrowserWidget::StopLoad()
{
	JavaWebBrowser->StopLoad();
}

void SChromiumAndroidWebBrowserWidget::Reload()
{
	JavaWebBrowser->Reload();
}

void SChromiumAndroidWebBrowserWidget::Close()
{
	JavaWebBrowser->Release();
	WebBrowserWindowPtr.Reset();
}

void SChromiumAndroidWebBrowserWidget::GoBack()
{
	JavaWebBrowser->GoBack();
}

void SChromiumAndroidWebBrowserWidget::GoForward()
{
	JavaWebBrowser->GoForward();
}

bool SChromiumAndroidWebBrowserWidget::CanGoBack()
{
	return HistoryPosition > 1;
}

bool SChromiumAndroidWebBrowserWidget::CanGoForward()
{
	return HistoryPosition < HistorySize-1;
}

void SChromiumAndroidWebBrowserWidget::SetWebBrowserVisibility(bool InIsVisible)
{
	JavaWebBrowser->SetVisibility(InIsVisible);
}

jbyteArray SChromiumAndroidWebBrowserWidget::HandleShouldInterceptRequest(jstring JUrl)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);

	FString Response;
	bool bOverrideResponse = false;
	int32 Position = Url.Find(*FChromiumMobileJSScripting::JSMessageTag, ESearchCase::CaseSensitive);
	if (Position >= 0)
	{
		AsyncTask(ENamedThreads::GameThread, [Url, Position, this]()
		{
			if (WebBrowserWindowPtr.IsValid())
			{
				TSharedPtr<FChromiumAndroidWebBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
				if (BrowserWindow.IsValid())
				{
					FString Origin = Url.Left(Position);
					FString Message = Url.RightChop(Position + FChromiumMobileJSScripting::JSMessageTag.Len());

					TArray<FString> Params;
					Message.ParseIntoArray(Params, TEXT("/"), false);
					if (Params.Num() > 0)
					{
						for (int I = 0; I < Params.Num(); I++)
						{
							Params[I] = FPlatformHttp::UrlDecode(Params[I]);
						}

						FString Command = Params[0];
						Params.RemoveAt(0, 1);
						BrowserWindow->OnJsMessageReceived(Command, Params, Origin);
					}
					else
					{
						GLog->Logf(ELogVerbosity::Error, TEXT("Invalid message from browser view: %s"), *Message);
					}
				}
			}
		});
		bOverrideResponse = true;
	}
	else
	{
	    FGraphEventRef OnLoadUrl = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
	    {
			if (WebBrowserWindowPtr.IsValid())
			{
				TSharedPtr<FChromiumAndroidWebBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
				if (BrowserWindow.IsValid() && BrowserWindow->OnLoadUrl().IsBound())
				{
					FString Method = TEXT(""); // We don't support passing anything but the requested URL
					bOverrideResponse = BrowserWindow->OnLoadUrl().Execute(Method, Url, Response);
				}
			}
		}, TStatId(), NULL, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnLoadUrl);
	}

	if ( bOverrideResponse )
	{
		FTCHARToUTF8 Converter(*Response);
		jbyteArray Buffer = JEnv->NewByteArray(Converter.Length());
		JEnv->SetByteArrayRegion(Buffer, 0, Converter.Length(), reinterpret_cast<const jbyte *>(Converter.Get()));
		return Buffer;
	}
	return nullptr;
}

bool SChromiumAndroidWebBrowserWidget::HandleShouldOverrideUrlLoading(jstring JUrl)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);
	bool Retval = false;
	FGraphEventRef OnBeforeBrowse = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			TSharedPtr<FChromiumAndroidWebBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
			if (BrowserWindow.IsValid())
			{
				if (BrowserWindow->OnBeforeBrowse().IsBound())
				{
					FChromiumWebNavigationRequest RequestDetails;
					RequestDetails.bIsRedirect = false;
					RequestDetails.bIsMainFrame = true; // shouldOverrideUrlLoading is only called on the main frame

					Retval = BrowserWindow->OnBeforeBrowse().Execute(Url, RequestDetails);
				}
			}
		}
	}, TStatId(), NULL, ENamedThreads::GameThread);
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnBeforeBrowse);

	return Retval;
}

bool SChromiumAndroidWebBrowserWidget::HandleJsDialog(TSharedPtr<IChromiumWebBrowserDialog>& Dialog)
{
	bool Retval = false;
	FGraphEventRef OnShowDialog = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
	{
		if (WebBrowserWindowPtr.IsValid())
		{
			TSharedPtr<FChromiumAndroidWebBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
			if (BrowserWindow.IsValid() && BrowserWindow->OnShowDialog().IsBound())
			{
				EChromiumWebBrowserDialogEventResponse EventResponse = BrowserWindow->OnShowDialog().Execute(TWeakPtr<IChromiumWebBrowserDialog>(Dialog));
				switch (EventResponse)
				{
				case EChromiumWebBrowserDialogEventResponse::Handled:
					Retval = true;
					break;
				case EChromiumWebBrowserDialogEventResponse::Continue:
					Dialog->Continue(true, (Dialog->GetType() == EChromiumWebBrowserDialogType::Prompt) ? Dialog->GetDefaultPrompt() : FText::GetEmpty());
					Retval = true;
					break;
				case EChromiumWebBrowserDialogEventResponse::Ignore:
					Dialog->Continue(false);
					Retval = true;
					break;
				case EChromiumWebBrowserDialogEventResponse::Unhandled:
				default:
					Retval = false;
					break;
				}
			}
		}
	}, TStatId(), NULL, ENamedThreads::GameThread);
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(OnShowDialog);

	return Retval;
}

void SChromiumAndroidWebBrowserWidget::HandleReceivedTitle(jstring JTitle)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Title = FJavaHelper::FStringFromParam(JEnv, JTitle);

	if (WebBrowserWindowPtr.IsValid())
	{
		TSharedPtr<FChromiumAndroidWebBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
		if (BrowserWindow.IsValid())
		{
			BrowserWindow->SetTitle(Title);
		}
	}
}

void SChromiumAndroidWebBrowserWidget::HandlePageLoad(jstring JUrl, bool bIsLoading, int InHistorySize, int InHistoryPosition)
{
	HistorySize = InHistorySize;
	HistoryPosition = InHistoryPosition;

	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);
	if (WebBrowserWindowPtr.IsValid())
	{
		TSharedPtr<FChromiumAndroidWebBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
		if (BrowserWindow.IsValid())
		{
			BrowserWindow->NotifyDocumentLoadingStateChange(Url, bIsLoading);
		}
	}
}

void SChromiumAndroidWebBrowserWidget::HandleReceivedError(jint ErrorCode, jstring /* ignore */, jstring JUrl)
{
	JNIEnv*	JEnv = FAndroidApplication::GetJavaEnv();

	FString Url = FJavaHelper::FStringFromParam(JEnv, JUrl);
	if (WebBrowserWindowPtr.IsValid())
	{
		TSharedPtr<FChromiumAndroidWebBrowserWindow> BrowserWindow = WebBrowserWindowPtr.Pin();
		if (BrowserWindow.IsValid())
		{
			BrowserWindow->NotifyDocumentError(Url, ErrorCode);
		}
	}
}

// Native method implementations:

JNI_METHOD jbyteArray Java_com_epicgames_ue4_WebViewControl_00024ViewClient_shouldInterceptRequestImpl(JNIEnv* JEnv, jobject Client, jstring JUrl)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleShouldInterceptRequest(JUrl);
	}
	else
	{
		return nullptr;
	}
}

JNI_METHOD jboolean Java_com_epicgames_ue4_WebViewControl_00024ViewClient_shouldOverrideUrlLoading(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleShouldOverrideUrlLoading(JUrl);
	}
	else
	{
		return false;
	}
}

JNI_METHOD void Java_com_epicgames_ue4_WebViewControl_00024ViewClient_onPageLoad(JNIEnv* JEnv, jobject Client, jstring JUrl, jboolean bIsLoading, jint HistorySize, jint HistoryPosition)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		Widget->HandlePageLoad(JUrl, bIsLoading, HistorySize, HistoryPosition);
	}
}

JNI_METHOD void Java_com_epicgames_ue4_WebViewControl_00024ViewClient_onReceivedError(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jint ErrorCode, jstring Description, jstring JUrl)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		Widget->HandleReceivedError(ErrorCode, Description, JUrl);
	}
}

JNI_METHOD jboolean Java_com_epicgames_ue4_WebViewControl_00024ChromeClient_onJsAlert(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jobject Result)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsDialog(EChromiumWebBrowserDialogType::Alert, JUrl, Message, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD jboolean Java_com_epicgames_ue4_WebViewControl_00024ChromeClient_onJsBeforeUnload(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jobject Result)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsDialog(EChromiumWebBrowserDialogType::Unload, JUrl, Message, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD jboolean Java_com_epicgames_ue4_WebViewControl_00024ChromeClient_onJsConfirm(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jobject Result)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsDialog(EChromiumWebBrowserDialogType::Confirm, JUrl, Message, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD jboolean Java_com_epicgames_ue4_WebViewControl_00024ChromeClient_onJsPrompt(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring JUrl, jstring Message, jstring DefaultValue, jobject Result)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		return Widget->HandleJsPrompt(JUrl, Message, DefaultValue, Result);
	}
	else
	{
		return false;
	}
}

JNI_METHOD void Java_com_epicgames_ue4_WebViewControl_00024ChromeClient_onReceivedTitle(JNIEnv* JEnv, jobject Client, jobject /* ignore */, jstring Title)
{
	TSharedPtr<SChromiumAndroidWebBrowserWidget> Widget = SChromiumAndroidWebBrowserWidget::GetWidgetPtr(JEnv, Client);
	if (Widget.IsValid())
	{
		Widget->HandleReceivedTitle(Title);
	}
}

#endif // USE_ANDROID_JNI
