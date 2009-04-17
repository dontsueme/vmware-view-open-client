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
 * app.hh --
 *
 *    Application singleton object. It handles initialization of global
 *    libraries and resources.
 *
 */

#ifndef APP_HH
#define APP_HH


#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkvbox.h>


#include "broker.hh"
#include "brokerDlg.hh"
#include "desktop.hh"
#include "desktopSelectDlg.hh"
#include "rdesktop.hh"
#include "restartMonitor.hh"
#include "util.hh"


namespace cdk {


#define VMWARE_VIEW "vmware-view"


class App
   : public Broker
{
public:
   struct MonitorBounds {
      long top;
      long bottom;
      long left;
      long right;

      MonitorBounds() : top(-1), bottom(-1), left(-1), right(-1) { }
   };

   App(int argc, char **argv);
   ~App();

   static App *GetApp() { return sApp; }
   static void ShowDialog(GtkMessageType type,
                          const Util::string format, ...);

private:
   static void IntegrateGLibLogging();
   static void OnGLibLog(const gchar *domain,
                         GLogLevelFlags level,
                         const gchar *message);

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

   static App *sApp;
   static gchar *sOptBroker;
   static gchar *sOptUser;
   static gchar *sOptPassword;
   static gchar *sOptDomain;
   static gchar *sOptDesktop;
   static gboolean sOptNonInteractive;
   static gboolean sOptFullscreen;
   static gchar *sOptBackground;
   static gchar *sOptFile;
   static gchar **sOptRedirect;
   static gboolean sOptVersion;
   static gchar **sOptUsb;

   static GOptionEntry sOptEntries[];
   static GOptionEntry sOptFileEntries[];

   void ParseFileArgs();
   void InitWindow();
   void SetContent(Dlg *dlg);

   void GetFullscreenGeometry(bool allMonitors, GdkRectangle *geometry,
                              MonitorBounds *bounds = NULL);

   // Status notifications, overrides Broker
   void SetBusy(const Util::string &message);
   void SetReady();
   void UpdateDesktops();

   // Broker state change actions. overrides Broker
   void RequestBroker();
   void RequestScInsertPrompt(Cryptoki *cryptoki);
   void RequestScPin(const Util::string &tokenName, const X509 *x509);
   void RequestCertificate(std::list<X509 *> &certs);
   void RequestDisclaimer(const Util::string &disclaimer);
   void RequestPasscode(const Util::string &username);
   void RequestNextTokencode(const Util::string &username);
   void RequestPinChange(const Util::string &pin1,
                         const Util::string &message,
                         bool userSelectable);
   void RequestPassword(const Util::string &username,
                        bool readOnly,
                        const std::vector<Util::string> &domains,
                        const Util::string &suggestedDomain);
   void RequestPasswordChange(const Util::string &username,
                              const Util::string &domain);
   void RequestDesktop();
   void OnTunnelConnected(Tunnel *tunnel);
   void RequestTransition(const Util::string &message);
   void RequestLaunchDesktop(Desktop *desktop);
   void Quit();

   // Inherited from Broker
   void TunnelDisconnected(Util::string disconnectReason);

   // Button click handlers
   void DoInitialize();
   void DoSubmitScInsertPrompt();
   void DoSubmitScPin();
   void DoSubmitCertificate();
   void DoSubmitPasscode();
   void DoSubmitNextTokencode();
   void DoSubmitPins();
   void DoSubmitPassword();
   void DoChangePassword();
   void DoDesktopAction(DesktopSelectDlg::Action action);

   void OnCancel();
   void UpdateDisplayEnvironment();
   void OnDesktopUIExit(Dlg *dlg, int status);
   void OnDesktopUICancel(Dlg *dlg);

   GtkWindow *mWindow;
   GtkVBox *mToplevelBox;
   GtkVBox *mContentBox;
   GtkAlignment *mFullscreenAlign;
   GtkImage *mBackgroundImage;
   Dlg *mDlg;
   boost::signals::connection mDesktopUIExitCnx;
   RestartMonitor mRDesktopMonitor;
   bool mUseAllMonitors;
   MonitorBounds mMonitorBounds;
#ifdef VIEW_ENABLE_WINDOW_MODE
   GdkRectangle mDesktopSize;
   bool mFullScreen;
#endif
};


} // namespace cdk


#endif // APP_HH
