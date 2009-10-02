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


#include "broker.hh"
#include "cryptoki.hh"
#include "desktop.hh"
#include "desktopSelectDlg.hh"
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

   void ShowDialog(GtkMessageType type,
                   const Util::string format, ...);

   void Show();
   void Close() { gtk_widget_destroy(GTK_WIDGET(mWindow)); }
   void Disconnect() { Close(); }

   GtkWindow *GetWindow() { return mWindow; }

   // Status notifications, implements Broker::Delegate
   virtual void SetBusy(const Util::string &message);
   virtual void SetReady();
   virtual void SetLogoutOnCertRemoval(bool enabled);
   virtual void UpdateDesktops();

   // State change actions, implements Broker::Delegate
   virtual void RequestBroker();
   virtual void RequestDisclaimer(const Util::string &disclaimer);
   virtual void RequestPasscode(const Util::string &username);
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
   virtual void RequestTransition(const Util::string &message);
   virtual void TunnelDisconnected(Util::string disconnectReason);

protected:
   static void OnCancel(GtkButton *button, gpointer data)
      { reinterpret_cast<Window *>(data)->CancelHandler(); }
   virtual void CancelHandler();

   static void OnForward(GtkButton *button, gpointer data)
      {  reinterpret_cast<Window *>(data)->ForwardHandler(); }
   virtual void ForwardHandler();

   void AddForwardIdleHandler()
      {  g_idle_add(OnIdleNonInteractive, this); }

   static gboolean OnIdleNonInteractive(gpointer data)
      {  reinterpret_cast<Window *>(data)->ForwardHandler(); return false; }

   virtual void DoDesktopConnect(Desktop *desktop)
      { mBroker->ConnectDesktop(desktop); }
   virtual void DoInitialize();
   virtual bool GetFullscreen() const;
   virtual void RequestLaunchDesktop(Desktop *desktop);
   virtual void RequestDesktop();

   virtual void Reset();
   void SetContent(Dlg *dlg);

   void SetBroker(Broker *broker) { mBroker = broker; }
   Broker *GetBroker() { return mBroker; }
   Dlg *GetDlg() { return mDlg; }

   GtkButton *GetForwardButton() { return mForwardButton; }

   void UpdateForwardButton(bool sensitive, bool visible = true)
      { UpdateButton(mForwardButton, sensitive, visible, true); }
   void UpdateCancelButton(bool sensitive, bool visible = true)
      { UpdateButton(mCancelButton, sensitive, visible, false); }
   void UpdateViewCertButton(bool sensitive, bool visible = false)
      { UpdateButton(mViewCertButton, sensitive, visible, false); }

private:
   enum TokenEventAction {
      ACTION_NONE,
      ACTION_QUIT_MAIN_LOOP,
      ACTION_LOGOUT
   };

   static void FullscreenWindow(GtkWindow *win, MonitorBounds *bounds = NULL);
   static void OnSizeAllocate(GtkWidget *widget, GtkAllocation *allocation,
                              gpointer userData);
   static GtkWidget *CreateBanner();
   static void OnBannerSizeAllocate(GtkWidget *widget, GtkAllocation *allocation,
                                    gpointer userData);
   void ResizeBackground(GtkAllocation *allocation);
   static gboolean OnKeyPress(GtkWidget *widget, GdkEventKey *evt,
                              gpointer userData);
   static gboolean DelayedDoInitialize(void *that);

   bool OnCtrlAltDel();

   void UpdateButton(GtkButton *button, bool sensitive, bool visible,
                     bool isDefault);

   void InitWindow();

   void GetFullscreenGeometry(bool allMonitors, Util::Rect *geometry,
                              MonitorBounds *bounds = NULL);

   Broker::CertAuthInfo GetCertAuthInfo(SSL *ssl);

   // Button click handlers
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
   void OnDesktopUIExit(Dlg *dlg, int status);

   const char *OnScPinRequested(const Util::string &label, const X509 *x509);

   void StartWatchingForTokenEvents(TokenEventAction action);
   void StopWatchingForTokenEvents();
   bool HadTokenEvent() const { return mTokenEventTimeout == 0; }
   static gboolean TokenEventMonitor(gpointer data);
   std::vector<Util::string> GetSmartCardRedirects();

   void SetCookieFile(const Util::string &brokerUrl);

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
   Broker::CertAuthInfo *mCertAuthInfo;
   X509 *mAuthCert;
   ProcHelper *mDesktopHelper;
   TokenEventAction mTokenEventAction;
   GtkWidget *mCadDlg;
};


} // namespace cdk


#endif // WINDOW_HH
