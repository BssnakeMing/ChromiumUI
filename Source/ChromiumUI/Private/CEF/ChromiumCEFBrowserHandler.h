// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_CEF3


#include "ChromiumCEFLibCefIncludes.h"


#include "IChromiumWebBrowserWindow.h"

#endif

class IChromiumWebBrowserWindow;
struct Rect;
class FChromiumCEFWebBrowserWindow;
class FChromiumCEFBrowserPopupFeatures;

#if WITH_CEF3

/**
 * Implements CEF Client and other Browser level interfaces.
 */
class FChromiumCEFBrowserHandler
	: public CefClient
	, public CefDisplayHandler
	, public CefLifeSpanHandler
	, public CefLoadHandler
	, public CefRenderHandler
	, public CefRequestHandler
	, public CefKeyboardHandler
	, public CefJSDialogHandler
	, public CefContextMenuHandler
	, public CefDragHandler
	, public CefResourceRequestHandler
	, public CefRequestContextHandler
	, public CefCookieAccessFilter
{
public:

	/** Default constructor. */
	FChromiumCEFBrowserHandler(bool InUseTransparency, const TArray<FString>& AltRetryDomains = TArray<FString>());

public:

	/**
	 * Pass in a pointer to our Browser Window so that events can be passed on.
	 *
	 * @param InBrowserWindow The browser window this will be handling.
	 */
	void SetBrowserWindow(TSharedPtr<FChromiumCEFWebBrowserWindow> InBrowserWindow);
	
	/**
	 * Sets the browser window features and settings for popups which will be passed along when creating the new window.
	 *
	 * @param InPopupFeatures The popup features and settings for the window.
	 */
	void SetPopupFeatures(const TSharedPtr<FChromiumCEFBrowserPopupFeatures>& InPopupFeatures)
	{
		BrowserPopupFeatures = InPopupFeatures;
	}

public:

	// CefClient Interface

	virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefRequestHandler> GetRequestHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefDragHandler> GetDragHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefCookieAccessFilter> GetCookieAccessFilter(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request) override 
	{
		return this;
	}

	virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> frame,
		CefProcessId SourceProcess,
		CefRefPtr<CefProcessMessage> Message) override;

public:

	// CefDisplayHandler Interface

	virtual void OnTitleChange(CefRefPtr<CefBrowser> Browser, const CefString& Title) override;
	virtual void OnAddressChange(CefRefPtr<CefBrowser> Browser, CefRefPtr<CefFrame> Frame, const CefString& Url) override;
	virtual bool OnTooltip(CefRefPtr<CefBrowser> Browser, CefString& Text) override;
	virtual bool OnConsoleMessage(
		CefRefPtr<CefBrowser> Browser, 
		cef_log_severity_t level,
		const CefString& Message,
		const CefString& Source, 
		int Line) override;

public:

	// CefLifeSpanHandler Interface

	virtual void OnAfterCreated(CefRefPtr<CefBrowser> Browser) override;
	virtual bool DoClose(CefRefPtr<CefBrowser> Browser) override;
	virtual void OnBeforeClose(CefRefPtr<CefBrowser> Browser) override;

	virtual bool OnBeforePopup(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		const CefString& Target_Url,
		const CefString& Target_Frame_Name,
		CefLifeSpanHandler::WindowOpenDisposition /* Target_Disposition */,
		bool /* User_Gesture */,
		const CefPopupFeatures& PopupFeatures,
		CefWindowInfo& WindowInfo,
		CefRefPtr<CefClient>& Client,
		CefBrowserSettings& Settings,
		CefRefPtr<CefDictionaryValue>& extra_info,
		bool* no_javascript_access) override
	{
		return OnBeforePopup(Browser, Frame, Target_Url, Target_Frame_Name, PopupFeatures, WindowInfo, Client, Settings, no_javascript_access);
	}

	virtual bool OnBeforePopup(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame, 
		const CefString& Target_Url, 
		const CefString& Target_Frame_Name,
		const CefPopupFeatures& PopupFeatures, 
		CefWindowInfo& WindowInfo,
		CefRefPtr<CefClient>& Client, 
		CefBrowserSettings& Settings,
		bool* no_javascript_access) ;

public:

	// CefLoadHandler Interface

	virtual void OnLoadError(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefLoadHandler::ErrorCode InErrorCode,
		const CefString& ErrorText,
		const CefString& FailedUrl) override;

	virtual void OnLoadingStateChange(
		CefRefPtr<CefBrowser> browser,
		bool isLoading,
		bool canGoBack,
		bool canGoForward) override;

	virtual void OnLoadStart(
		CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		TransitionType CefTransitionType) override;

public:

	// CefRenderHandler Interface
	virtual bool GetRootScreenRect(CefRefPtr<CefBrowser> Browser, CefRect& Rect) override;
	virtual void GetViewRect(CefRefPtr<CefBrowser> Browser, CefRect& Rect) override;
	virtual void OnPaint(CefRefPtr<CefBrowser> Browser,
		PaintElementType Type,
		const RectList& DirtyRects,
		const void* Buffer,
		int Width, int Height) override;
	virtual void OnAcceleratedPaint(CefRefPtr<CefBrowser> Browser,
		PaintElementType Type,
		const RectList& DirtyRects,
		void* SharedHandle) override;
	virtual void OnCursorChange(CefRefPtr<CefBrowser> Browser,
		CefCursorHandle Cursor,
		CefRenderHandler::CursorType Type,
		const CefCursorInfo& CustomCursorInfo) override;
	virtual void OnPopupShow(CefRefPtr<CefBrowser> Browser, bool bShow) override;
	virtual void OnPopupSize(CefRefPtr<CefBrowser> Browser, const CefRect& Rect) override;
	virtual bool GetScreenInfo(CefRefPtr<CefBrowser> Browser, CefScreenInfo& ScreenInfo) override;
#if !PLATFORM_LINUX
	virtual void OnImeCompositionRangeChanged(
		CefRefPtr<CefBrowser> Browser,
		const CefRange& SelectionRange,
		const CefRenderHandler::RectList& CharacterBounds) override;
#endif

public:

	// CefRequestHandler Interface

	virtual CefResourceRequestHandler::ReturnValue OnBeforeResourceLoad(
		CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefRefPtr<CefRequest> Request,
		CefRefPtr<CefRequestCallback> Callback) override;
	virtual void OnResourceLoadComplete(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefRefPtr<CefRequest> Request,
		CefRefPtr<CefResponse> Response,
		URLRequestStatus Status,
		int64 Received_content_length) override;
	virtual void OnRenderProcessTerminated(CefRefPtr<CefBrowser> Browser, TerminationStatus Status) override;
	virtual bool OnBeforeBrowse(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefRefPtr<CefRequest> Request,
		bool user_gesture, 
		bool IsRedirect) override;
	virtual CefRefPtr<CefResourceHandler> GetResourceHandler(
		CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefRefPtr<CefRequest> Request ) override;
	virtual bool OnCertificateError(
		CefRefPtr<CefBrowser> Browser,
		cef_errorcode_t CertError,
		const CefString& RequestUrl,
		CefRefPtr<CefSSLInfo> SslInfo,
		CefRefPtr<CefRequestCallback> Callback ) override;

	virtual CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
		CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request,
		bool is_navigation,
		bool is_download,
		const CefString& request_initiator,
		bool& disable_default_handling) override;

public:
	// CefKeyboardHandler interface
	virtual bool OnKeyEvent(CefRefPtr<CefBrowser> Browser,
		const CefKeyEvent& Event,
		CefEventHandle OsEvent) override;

public:
	// CefCookieAccessFilter interface
	virtual bool CanSaveCookie(CefRefPtr<CefBrowser> browser,
		CefRefPtr<CefFrame> frame,
		CefRefPtr<CefRequest> request,
		CefRefPtr<CefResponse> response,
		const CefCookie& cookie) override;

public:
	// CefJSDialogHandler interface

	virtual bool OnJSDialog(
		CefRefPtr<CefBrowser> Browser,
		const CefString& OriginUrl,
		JSDialogType DialogType,
		const CefString& MessageText,
		const CefString& DefaultPromptText,
		CefRefPtr<CefJSDialogCallback> Callback,
		bool& OutSuppressMessage) override;

	virtual bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> Browser, const CefString& MessageText, bool IsReload, CefRefPtr<CefJSDialogCallback> Callback) override;

	virtual void OnResetDialogState(CefRefPtr<CefBrowser> Browser) override;

public:
	// CefContextMenuHandler

	virtual void OnBeforeContextMenu(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefRefPtr<CefContextMenuParams> Params,
		CefRefPtr<CefMenuModel> Model) override;

public:
	// CefDragHandler interface

	virtual void OnDraggableRegionsChanged(
		CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> frame, 
		const std::vector<CefDraggableRegion>& Regions) override;

public:

	IChromiumWebBrowserWindow::FOnBeforePopupDelegate& OnBeforePopup()
	{
		return BeforePopupDelegate;
	}

	IChromiumWebBrowserWindow::FOnCreateWindow& OnCreateWindow()
	{
		return CreateWindowDelegate;
	}

	typedef TMap<FString, FString> FRequestHeaders;
	DECLARE_DELEGATE_ThreeParams(FOnBeforeResourceLoadDelegate, const CefString& /*URL*/, CefRequest::ResourceType /*Type*/, FRequestHeaders& /*AdditionalHeaders*/);
	FOnBeforeResourceLoadDelegate& OnBeforeResourceLoad()
	{
		return BeforeResourceLoadDelegate;
	}

	DECLARE_DELEGATE_FourParams(FOnResourceLoadCompleteDelegate, const CefString& /*URL*/, CefRequest::ResourceType /*Type*/, CefResourceRequestHandler::URLRequestStatus /*Status*/, int64 /*ContentLength*/);
	FOnResourceLoadCompleteDelegate& OnResourceLoadComplete()
	{
		return ResourceLoadCompleteDelegate;
	}

	DECLARE_DELEGATE_FiveParams(FOnConsoleMessageDelegate, CefRefPtr<CefBrowser> /*Browser*/, cef_log_severity_t /*level*/, const CefString& /*Message*/, const CefString& /*Source*/, int /*Line*/);
	FOnConsoleMessageDelegate& OnConsoleMessage()
	{
		return ConsoleMessageDelegate;
	}

private:

	bool ShowDevTools(const CefRefPtr<CefBrowser>& Browser);

	bool bUseTransparency;
	bool bAllowAllCookies;

	TArray<FString> AltRetryDomains;
	uint32 AltRetryDomainIdx = 0;

	/** Delegate for notifying that a popup window is attempting to open. */
	IChromiumWebBrowserWindow::FOnBeforePopupDelegate BeforePopupDelegate;
	
	/** Delegate for handling requests to create new windows. */
	IChromiumWebBrowserWindow::FOnCreateWindow CreateWindowDelegate;

	/** Delegate for handling adding additional headers to requests */
	FOnBeforeResourceLoadDelegate BeforeResourceLoadDelegate;

	/** Delegate that allows response to the status of resource loads */
	FOnResourceLoadCompleteDelegate ResourceLoadCompleteDelegate;

	/** Delegate that allows for response to console logs.  Typically used to capture and mirror web logs in client application logs. */
	FOnConsoleMessageDelegate ConsoleMessageDelegate;

	/** Weak Pointer to our Web Browser window so that events can be passed on while it's valid.*/
	TWeakPtr<FChromiumCEFWebBrowserWindow> BrowserWindowPtr;
	
	/** Pointer to the parent web browser handler */
	CefRefPtr<FChromiumCEFBrowserHandler> ParentHandler;

	/** Stores popup window features and settings */
	TSharedPtr<FChromiumCEFBrowserPopupFeatures> BrowserPopupFeatures;

	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(FChromiumCEFBrowserHandler);
};

#endif
