/*********************************************************
 * Copyright (C) 2008 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is released with an additional exemption that
 * compiling, linking, and/or using the OpenSSL libraries with this
 * program is allowed.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*
 * window.hh --
 *
 *    Application's window, which logs the user in and displays a
 *    desktop.
 *
 */

#ifndef WINDOW_HH
#define WINDOW_HH


#include <gtk/gtk.h>


#include "baseApp.hh"
#include "broker.hh"
#include "cryptoki.hh"
#include "desktop.hh"
#include "desktopSelectDlg.hh"
#include "helpSupportDlg.hh"
#include "restartMonitor.hh"
#include "util.hh"


namespace cdk {


class Window
   : virtual public Broker::Delegate
{
public:
   struct MonitorBounds {
      long top;
      long bottom;
      long left;
      long right;

      MonitorBounds() : top(-1), bottom(-1), left(-1), right(-1) { }
   };

   Window();
   virtual ~Window();

   virtual void ShowMessageDialog(GtkMessageType type,
                                  const Util::string &message,
                                  const Util::string &details,
                                  va_list args);

   void Show();
   void Close() { gtk_widget_destroy(GTK_WIDGET(mWindow)); }
   void Hide() { gtk_widget_hide(GTK_WIDGET(mWindow)); }
   void Disconnect() { Close(); }

   GtkWindow *GetWindow() { return mWindow; }

   // Implements Broker::Delegate
   virtual void SetLogoutOnCertRemoval(bool enabled);
   virtual void UpdateDesktops();

   // State change actions, implements Broker::Delegate
   virtual void RequestBroker();
   virtual void RequestDisclaimer(const Util::string &disclaimer);
   virtual void RequestCertificate(std::list<Util::string> &trustedIssuers);
   virtual void RequestPasscode(const Util::string &username,
                                bool userSelectable);
   virtual void RequestNextTokencode(const Util::string &username);
   virtual void RequestPinChange(const Util::string &pin1,
                                 const Util::string &message,
                                 bool userSelectable);
   virtual void RequestPassword(const Util::string &username,
                                bool readOnly,
                                const std::vector<Util::string> &domains,
                                const Util::string &suggestedDomain);
   virtual void RequestPasswordChange(const Util::string &username,
                                      const Util::string &domain);
   virtual void RequestTransition(const Util::string &message,
                                  bool useMarkup = false);
   virtual void TunnelDisconnected(Util::string disconnectReason);

   void UpdateForwardButton(bool sensitive, bool visible = true)
      { UpdateButton(mForwardButton, sensitive, visible, true); }
   void UpdateCancelButton(bool sensitive, bool visible = true)
      { UpdateButton(mCancelButton, sensitive, visible, false); }
   void UpdateHelpButton(bool sensitive, bool visible = true)
      { UpdateButton(mHelpButton, sensitive, visible, false); }
   void UpdateViewCertButton(bool sensitive, bool visible = false)
      { UpdateButton(mViewCertButton, sensitive, visible, false); }

   virtual void SetReady();

protected:
   virtual void SetBusy(const Util::string &message);

   static void OnCancel(GtkButton *button, gpointer data)
      { reinterpret_cast<Window *>(data)->CancelHandler(); }
   virtual void CancelHandler();

   static void OnForward(GtkButton *button, gpointer data)
      {  reinterpret_cast<Window *>(data)->ForwardHandler(); }
   virtual void ForwardHandler();

   static gboolean OnIdleDeleteProcHelper(gpointer data)
      { delete reinterpret_cast<ProcHelper *>(data); return false; }

   virtual void DoDesktopConnect(Desktop *desktop)
      { mBroker->ConnectDesktop(desktop); }
   virtual void DoInitialize();
   virtual void InitializeProtocols();
   virtual bool GetFullscreen() const;
   virtual void RequestLaunchDesktop(Desktop *desktop);
   virtual void RequestDesktop();

   virtual void Reset();
   virtual void SetContent(Dlg *dlg);

   virtual DesktopSelectDlg *CreateDesktopSelectDlg(std::vector<Desktop *> &desktops,
                                                    Util::string initialDesktop,
                                                    bool offerMultiMon,
                                                    bool offerWindowSizes)
      { return new DesktopSelectDlg(desktops, initialDesktop, offerMultiMon,
                                    offerWindowSizes); }

   void SetBroker(Broker *broker);
   Broker *GetBroker() { return mBroker; }
   Dlg *GetDlg() { return mDlg; }
   void ShowContentBox() { gtk_widget_show(GTK_WIDGET(mContentBox)); }
   void HideContentBox() { gtk_widget_hide(GTK_WIDGET(mContentBox)); }

   virtual void CreateAlignment(GtkFixed *fixed);
   virtual void DoSizeAllocate(GtkAllocation *allocation);

   GtkHButtonBox *GetActionArea() { return mActionArea; }
   GtkButton *GetForwardButton() { return mForwardButton; }
   GtkButton *GetCancelButton() { return mCancelButton; }

   void UpdateButton(GtkButton *button, bool sensitive, bool visible,
                     bool isDefault);
   virtual void InitWindow();
   virtual HelpSupportDlg *GetHelpSupportDlg();
   virtual void InitializeHelpSupportDlg(HelpSupportDlg *dlg);

   virtual void RequestInsertSmartCard();
   virtual void RequestCertificate();
   virtual void RequestPrivateKey();

   static gboolean DelayedDoInitialize(void *that);

   void SetCookieFile(const Util::string &brokerUrl);

private:
   enum TokenEventAction {
      ACTION_NONE,
      ACTION_REQUEST_CERTIFICATE,
      ACTION_LOGOUT
   };

   struct GrabResults {
      bool mouseGrabbed;
      bool ignoreNext;
      GdkWindow *window;
   };

   static void OnRealize(GtkWindow *window, gpointer data);
   static void OnUnrealize(GtkWindow *window, gpointer data);
   static void OnWindowManagerChanged(GdkScreen *screen, gpointer data);

   static void FullscreenWindow(GtkWindow *win, MonitorBounds *bounds = NULL);
   static void OnSizeAllocate(GtkWidget *widget, GtkAllocation *allocation,
                              gpointer userData);
   static GtkWidget *CreateBanner();
   static void OnBannerSizeAllocate(GtkWidget *widget, GtkAllocation *allocation,
                                    gpointer userData);
   void ResizeBackground(GtkAllocation *allocation);
   static gboolean OnKeyPress(GtkWidget *widget, GdkEventKey *evt,
                              gpointer userData);

   bool OnCtrlAltDel();
#ifdef GDK_WINDOWING_X11
   static void OnCADMapped(GtkWidget *widget, GdkEventAny *event,
                           gpointer userData);
   static void OnCADUnrealized(GtkWidget *widget, gpointer userData);
   static GdkFilterReturn CADEventFilter(GdkXEvent *xevent, GdkEvent *event,
                                         gpointer userData);
#endif
   static void GrabPointer(GrabResults *results);
   static void UngrabPointer(GrabResults *results);

   void GetFullscreenGeometry(bool allMonitors, Util::Rect *geometry,
                              MonitorBounds *bounds = NULL);

   // Button click handlers
   void DoAttemptSubmitCertificate(const char *pin = NULL);
   void DoSubmitPasscode();
   void DoSubmitNextTokencode();
   void DoSubmitPins();
   void DoSubmitPassword();
   void DoChangePassword();
   void DoDesktopAction(DesktopSelectDlg::Action action);

   static void OnHelp(GtkButton *button, gpointer data);
   static void OnViewCert(GtkButton *button, gpointer data);
   static void OnSupport(GtkButton *button, gpointer data);
   void PushDesktopEnvironment();
   void PopDesktopEnvironment();
   virtual void OnDesktopUIExit(Dlg *dlg, int status);

   void StartWatchingForTokenEvents(TokenEventAction action);
   void StopWatchingForTokenEvents();

   static gboolean TokenEventMonitor(gpointer data);
   std::vector<Util::string> GetSmartCardRedirects();

   Delegate *mDelegate;
   Broker *mBroker;
   Dlg *mDlg;
   GtkWindow *mWindow;
   GtkVBox *mToplevelBox;
   GtkVBox *mContentBox;
   GtkAlignment *mFullscreenAlign;
   GtkImage *mBackgroundImage;
   GtkHButtonBox *mActionArea;
   GtkButton *mCancelButton;
   GtkButton *mForwardButton;
   GtkButton *mHelpButton;
   GtkButton *mViewCertButton;
   boost::signals::connection mDesktopUIExitCnx;
   RestartMonitor mRDesktopMonitor;
   MonitorBounds mMonitorBounds;
   Cryptoki *mCryptoki;
   guint mTokenEventTimeout;
   bool mCanceledScDlg;
   Util::string mOrigLDPath;
   Util::string mOrigGSTPath;
   X509 *mAuthCert;
   ProcHelper *mDesktopHelper;
   TokenEventAction mTokenEventAction;
   GtkWidget *mCadDlg;
   std::list<Util::string> mTrustedIssuers;
   std::list<X509 *> mCertificates;
};


} // namespace cdk


#endif // WINDOW_HH
