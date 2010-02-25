/*********************************************************
 * Copyright (C) 2009 VMware, Inc. All rights reserved.
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
 * cdkWindowController.m --
 *
 *     Implementation of CdkWindowController
 */

extern "C" {
#include "vm_basic_types.h"
#include "base64.h"
#define _UINT64
}


#import <SecurityInterface/SFChooseIdentityPanel.h>
#include <sys/stat.h>
#include <fcntl.h>


#import "app.hh"
#import "cdkBroker.h"
#import "cdkBrokerAddress.h"
#import "cdkBrokerViewController.h"
#import "cdkChangePinCreds.h"
#import "cdkChangePinCredsViewController.h"
#import "cdkChangeWinCreds.h"
#import "cdkChangeWinCredsViewController.h"
#import "cdkConfirmPinCreds.h"
#import "cdkConfirmPinCredsViewController.h"
#import "cdkDesktop.h"
#import "cdkDesktopSize.h"
#import "cdkDesktopsViewController.h"
#import "cdkDisclaimer.h"
#import "cdkDisclaimerViewController.h"
#import "cdkKeychain.h"
#import "cdkPasscodeCreds.h"
#import "cdkPasscodeCredsViewController.h"
#import "cdkPrefs.h"
#import "cdkProcHelper.h"
#import "cdkRdc.h"
#import "cdkString.h"
#import "cdkTokencodeCreds.h"
#import "cdkTokencodeCredsViewController.h"
#import "cdkWaitingViewController.h"
#import "cdkWinCreds.h"
#import "cdkWinCredsViewController.h"
#import "cdkWindowController.h"
#import "desktop.hh"
#import "protocols.hh"
#import "restartMonitor.hh"


#define COOKIE_DIR_MODE (S_IRWXU)
#define COOKIE_FILE_MODE (S_IRUSR | S_IWUSR)


// For now, show the cert dialog for debug builds.
#ifndef VMX86_DEBUG
#define AUTO_SELECT_SINGLE_CERT 1
#endif // VMX86_DEBUG


@interface CdkDesktop (Friend)
-(cdk::Desktop *)adaptedDesktop;
@end // @interface CdkDesktop (Friend)


@interface CdkWindowController () // Private setters
@property(readwrite) BOOL busy;
@property(readwrite, assign) CdkViewController *viewController;
@property(readwrite, retain) CdkDesktop *desktop;
@property(readwrite, retain) CdkBroker *broker;
@property(readwrite, retain) CdkRdc *rdc;
@end // @interface CdkWindowController ()


@interface CdkWindowController (Private)
-(void)setCookieFile:(NSString *)brokerUrl;
@end // @interface CdkWindowController (Private)


static int const MINIMUM_VIEW_WIDTH = 480;


enum {
   SERVER_VIEW,
   DISCLAIMER_VIEW,
   PASSCODE_CREDS_VIEW,
   TOKENCODE_CREDS_VIEW,
   CHANGE_PIN_VIEW,
   CONFIRM_PIN_VIEW,
   WIN_CREDS_VIEW,
   CHANGE_WIN_CREDS_VIEW,
   DESKTOPS_VIEW,
   WAITING_VIEW,
   LAST_VIEW
};


@implementation CdkWindowController


@synthesize broker;
@synthesize busy;
@synthesize busyText;
@synthesize desktop;
@synthesize rdc;
@synthesize viewController;


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController windowController] --
 *
 *      Create and return an autoreleased window controller.
 *
 * Results:
 *      a new window controller, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkWindowController *)windowController
{
   return [[[CdkWindowController alloc] init] autorelease];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController init] --
 *
 *      Initialize the controller by creating our views.
 *
 * Results:
 *      Returns self.
 *
 * Side effects:
 *      Various views are loaded.
 *
 *-----------------------------------------------------------------------------
 */

-(id)init
{
   self = [super init];
   if (!(self = [super initWithWindowNibName:@"MainWindow"])) {
      return nil;
   }

   [self setBroker:[CdkBroker broker]];
   [broker setDelegate:self];

   mRdcMonitor = new cdk::RestartMonitor();
   clientName = @PRODUCT_VIEW_CLIENT_NAME;

   viewControllers =
      [[NSArray alloc] initWithObjects:[CdkBrokerViewController brokerViewController],
                       [CdkDisclaimerViewController disclaimerViewController],
                       [CdkPasscodeCredsViewController passcodeCredsViewController],
                       [CdkTokencodeCredsViewController tokencodeCredsViewController],
                       [CdkChangePinCredsViewController changePinCredsViewController],
                       [CdkConfirmPinCredsViewController confirmPinCredsViewController],
                       [CdkWinCredsViewController winCredsViewController],
                       [CdkChangeWinCredsViewController changeWinCredsViewController],
                       [CdkDesktopsViewController desktopsViewController],
                       [CdkWaitingViewController waitingViewController],
                       nil];
   return self;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController dealloc] --
 *
 *      Release our resources.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Releases views.
 *
 *-----------------------------------------------------------------------------
 */

-(void)dealloc
{
   [viewControllers release];
   [self setBroker:nil];
   [self setDesktop:nil];
   [rdc setDelegate:nil];
   [self setRdc:nil];
   delete mRdcMonitor;
   [super dealloc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController awakeFromNib] --
 *
 *      Awake handler.  Prompt the user for a server.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Broker screen is displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)awakeFromNib
{
   [aboutMenu setTitle:[NSString stringWithUTF8Format:_("About %s"),
                                 _(PRODUCT_VIEW_CLIENT_NAME)]];
   [hideMenu setTitle:[NSString stringWithUTF8Format:_("Hide %s"),
                                _(PRODUCT_VIEW_CLIENT_NAME)]];
   [quitMenu setTitle:[NSString stringWithUTF8Format:_("Quit %s"),
                                _(PRODUCT_VIEW_CLIENT_NAME)]];
   [helpMenu setTitle:[NSString stringWithUTF8Format:_("%s Help"),
                                _(PRODUCT_VIEW_CLIENT_NAME)]];
   [banner setImageAlignment:NSImageAlignLeft];
   [self brokerDidRequestBroker:broker];
   [[self window] center];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController onContinue:] --
 *
 *      Continue button click handler.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      RPC is likely in flight.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onContinue:(id)sender // IN
{
   Log("Continue!\n");
   switch ([viewControllers indexOfObject:viewController]) {
   case SERVER_VIEW: {
      CdkBrokerAddress *server =
	 [(CdkBrokerViewController *)viewController brokerAddress];
      Log("Connecting to %s...\n", [[server description] UTF8String]);
      [self setCookieFile:[server label]];
      [broker connectToAddress:server
                   defaultUser:[[CdkPrefs sharedPrefs] user]
                 defaultDomain:[[CdkPrefs sharedPrefs] domain]];
      break;
   }
   case DISCLAIMER_VIEW:
      [broker acceptDisclaimer];
      break;
   case PASSCODE_CREDS_VIEW: {
      id<CdkPasscodeCreds> passcodeCreds = [viewController representedObject];
      [broker submitUsername:[passcodeCreds username]
                    passcode:[passcodeCreds secret]];
      break;
   }
   case TOKENCODE_CREDS_VIEW: {
      id<CdkTokencodeCreds> tokencodeCreds = [viewController representedObject];
      [broker submitNextTokencode:[tokencodeCreds secret]];
      break;
   }
   case CHANGE_PIN_VIEW: {
      id<CdkChangePinCreds> changePinCreds = [viewController representedObject];
      [broker submitPin:[changePinCreds secret]
                    pin:[changePinCreds confirm]];
      break;
   }
   case CONFIRM_PIN_VIEW: {
      id<CdkConfirmPinCreds> confirmPinCreds = [viewController representedObject];
      [broker submitPin:[confirmPinCreds secret]
                    pin:[confirmPinCreds confirm]];
      break;
   }
   case WIN_CREDS_VIEW: {
      id<CdkWinCreds> winCreds = [viewController representedObject];
      [broker submitUsername:[winCreds username]
                    password:[winCreds secret]
                      domain:[winCreds domain]];
      break;
   }
   case CHANGE_WIN_CREDS_VIEW: {
      id<CdkChangeWinCreds> changeCreds = [viewController representedObject];
      [broker submitOldPassword:[changeCreds oldSecret]
                    newPassword:[changeCreds secret]
                        confirm:[changeCreds confirm]];
      break;
   }
   case DESKTOPS_VIEW: {
      CdkDesktopsViewController *desktops
	 = (CdkDesktopsViewController *)viewController;
      [broker connectDesktop:[desktops desktop]];
      break;
   }
   default:
      Log("Bad view index: %d\n",
	    [viewControllers indexOfObject:viewController]);
      NOT_REACHED();
      break;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController onGoBack:] --
 *
 *      Click handler for back button; reset and display the broker
 *      screen again.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      mApp is reset.
 *
 *-----------------------------------------------------------------------------
 */

-(IBAction)onGoBack:(id)sender // IN
{
   if ([self busy]) {
      [broker cancelRequests];
   } else {
      [self brokerDidRequestBroker:broker];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController goBackEnabled] --
 *
 *      Determines whether the "Go Back" button should be enabled.  It
 *      should if we're busy, or not on the broker screen.
 *
 *      Since this uses an or, it can't be expressed using IB
 *      properties.
 *
 * Results:
 *      YES if the go back button should be enabled.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

-(BOOL)goBackEnabled
{
   return [self busy] ||
      SERVER_VIEW != [viewControllers indexOfObject:viewController];
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkWindowController keyPathsForValuesAffectingGoBackEnabled] --
 *
 *      Determine which keys affect the value of goBackEnabled.
 *
 * Results:
 *      A set of property names.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(NSSet *)keyPathsForValuesAffectingGoBackEnabled
{
   return [NSSet setWithObjects:@"busy", @"viewController", nil];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController setViewController:] --
 *
 *      Switch the view to the specified ViewController.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      A different dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setViewController:(CdkViewController *)vc // IN
{
   if ([vc isEqual:viewController]) {
      return;
   }
   BOOL ended = [[self window] makeFirstResponder:[self window]];
   if (!ended) {
      NSBeep();
      return;
   }

   /*
    * If we're going back to the broker screen, it means there was an
    * error, so don't save the current dialog's settings.
    */
   if ([viewControllers indexOfObject:vc] != SERVER_VIEW) {
      [viewController savePrefs];
   }
   [viewController setWindowController:nil];

   viewController = vc;
   [viewController setWindowController:self];
   NSView *v = [viewController view];

   // Resize, whee.
   NSSize curSize = [[box contentView] frame].size;
   NSSize newSize = [v frame].size;
   float dw = MAX(newSize.width, MINIMUM_VIEW_WIDTH) - curSize.width;
   float dh = newSize.height - curSize.height;
   NSRect windowFrame = [[self window] frame];
   windowFrame.size.height += dh;
   windowFrame.origin.y -= dh;
   windowFrame.size.width += dw;

   // Empty before resizing.
   [box setContentView:nil];
   [[self window] setFrame:windowFrame display:YES animate:YES];

   [box setContentView:v];
   [[self window] selectNextKeyView:self];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController setBusyText:] --
 *
 *      Setter for busyText property.
 *
 *      When we become busy, the controls become disabled, but they
 *      don't retake focus when re-enabled.  So we do that here.
 *
 *      Since we need to reset the focus anyway, we may as well make
 *      the Go Back button take focus while we're busy.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Focus is updated based on whether we're busy or not; updates
 *      busy property.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setBusyText:(NSString *)newText // IN
{
   if ([busyText isEqualToString:newText]) {
      return;
   }
   [busyText release];
   busyText = [newText copy];

   [self setBusy:[busyText length] > 0];

   if ([self busy]) {
      [[self window] makeFirstResponder:goBackButton];
   } else {
      /*
       * XXX: Need a call to get the view controller's preferred focus
       * widget - password dialogs should focus on the password if
       * there was an error.
       */
      [[self window] makeFirstResponder:[self window]];
      [[self window] selectNextKeyView:self];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController brokerDidRequestBroker:] --
 *
 *      Present the user with the broker dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Broker dialog is loaded and displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)brokerDidRequestBroker:(CdkBroker *)aBroker // IN/UNUSED
{
   static BOOL firstTimeThrough = YES;
   /*
    * Reset() will free the C++ desktops, so make sure we abandon our
    * reference to them.
    */
   [self setDesktop:nil];
   [broker reset];

   CdkBrokerViewController *vc = [viewControllers objectAtIndex:SERVER_VIEW];
   if (firstTimeThrough) {
      [[vc brokerAddress] setLabel:[[[CdkPrefs sharedPrefs]
				    recentBrokers] objectAtIndex:0]];
   }
   [self setViewController:vc];
   if (firstTimeThrough && [[CdkPrefs sharedPrefs] autoConnect] &&
       [vc continueEnabled]) {
      [self onContinue:self];
   }
   firstTimeThrough = NO;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestDisclaimer:] --
 *
 *      Present the user with the disclaimer dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Disclaimer dialog is loaded and displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker           // IN/UNUSED
didRequestDisclaimer:(NSString *)disclaimer // IN
{
   CdkDisclaimerViewController *vc = [viewControllers objectAtIndex:DISCLAIMER_VIEW];
   [[vc disclaimer] setDisclaimer:disclaimer];
   [self setViewController:vc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestPasscode:userSelectable:] --
 *
 *      Present the user with the SecurID dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      SecurID dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker      // IN/UNUSED
didRequestPasscode:(NSString *)username // IN
userSelectable:(BOOL)userSelectable     // IN
{
   CdkPasscodeCredsViewController *vc =
      [viewControllers objectAtIndex:PASSCODE_CREDS_VIEW];
   id<CdkPasscodeCreds> passcodeCreds = [vc representedObject];
   [passcodeCreds setUsername:username];
   [passcodeCreds setSecret:@""];
   [passcodeCreds setUserSelectable:userSelectable];
   [self setViewController:vc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestNextTokencode:] --
 *
 *      Prompt the user for their next tokencode.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Tokencode dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker           // IN/UNUSED
didRequestNextTokencode:(NSString *)username // IN
{
   CdkTokencodeCredsViewController *vc =
      [viewControllers objectAtIndex:TOKENCODE_CREDS_VIEW];
   id<CdkTokencodeCreds> tokencodeCreds = [vc representedObject];
   [tokencodeCreds setUsername:username];
   [tokencodeCreds setSecret:@""];
   [self setViewController:vc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestPinChange:message:userSelectable:] --
 *
 *      Prompt the user to change their SecurID PIN.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Change PIN dialog is displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker  // IN/UNSED
didRequestPinChange:(NSString *)pin // IN
      message:(NSString *)message   // IN
userSelectable:(BOOL)userSelectable // IN
{
   NSString *label;
   // XXX: i18n
   if (![pin length]) {
      label = @"Enter a new RSA Securid PIN.";
   } else {
      label = userSelectable
         ? @"Enter a new RSA SecurID PIN or accept the default."
         : @"Accept the default RSA SecurID PIN.";
   }
   if ([message length]) {
      label = [label stringByAppendingFormat:@"\n\n%@", message];
   }
   CdkViewController *vc = nil;
   if ([pin length]) {
      vc = [viewControllers objectAtIndex:CONFIRM_PIN_VIEW];
      id<CdkConfirmPinCreds> confirmCreds = [vc representedObject];
      [confirmCreds setSecret:pin];
      [confirmCreds setConfirm:@""];
      [confirmCreds setLabel:label];
      [confirmCreds setUserSelectable:userSelectable];
   } else {
      vc = [viewControllers objectAtIndex:CHANGE_PIN_VIEW];
      id<CdkChangePinCreds> changeCreds = [vc representedObject];
      [changeCreds setSecret:pin];
      [changeCreds setConfirm:@""];
      [changeCreds setLabel:label];
   }
   [self setViewController:vc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestPassword:readOnly:domains:suggestedDomain:] --
 *
 *      Present the user with the password dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Password dialog is loaded and displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker      // IN/UNUSED
didRequestPassword:(NSString *)username // IN
     readOnly:(BOOL)readOnly            // IN
      domains:(NSArray *)domains        // IN
suggestedDomain:(NSString *)domain      // IN
{
   CdkWinCredsViewController *vc = [viewControllers objectAtIndex:WIN_CREDS_VIEW];
   id<CdkWinCreds> winCreds = [vc representedObject];
   [winCreds setUsername:username];
   [winCreds setSecret:@""];
   [winCreds setDomains:domains];
   [winCreds setUserSelectable:!readOnly];
   if ([domain length] && [domains indexOfObject:domain] != NSNotFound) {
      [winCreds setDomain:domain];
   }
   [self setViewController:vc];

   if (![username length]) {
      return;
   }

   UInt32 pwordLen = 0;
   void *pwordData = NULL;

   CdkBrokerAddress *address = [broker address];
   const char *hostname = [[address hostname] UTF8String];
   const char *user = [username UTF8String];

   OSStatus rv = SecKeychainFindInternetPassword(
      NULL,
      strlen(hostname), hostname,
      0, NULL, // security domain
      strlen(user), user,
      0, NULL, // path
      [address port],
      [address secure] ? kSecProtocolTypeHTTPS : kSecProtocolTypeHTTP,
      kSecAuthenticationTypeDefault, &pwordLen, &pwordData, NULL);

   if (rv != noErr) {
      if (rv != errSecItemNotFound) {
         Warning("Error looking up password: %ld: %s (%s)\n", rv,
                 GetMacOSStatusErrorString(rv),
                 GetMacOSStatusCommentString(rv));
      }
      return;
   }

   // The format length is an int, so...
   if (pwordLen <= G_MAXINT) {
      [winCreds setSecret:[NSString stringWithFormat:@"%.*s", pwordLen,
                                    pwordData]];
      if ([vc continueEnabled]) {
         [self onContinue:self];
      }
   }
   SecKeychainItemFreeContent(NULL, pwordData);
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestPasswordChange:domain:] --
 *
 *      Present the user with the password change dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Password change dialog is shown.
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker            // IN/UNSED
didRequestPasswordChange:(NSString *)username // IN
       domain:(NSString *)domain              // IN
{
   CdkChangeWinCredsViewController *vc =
      [viewControllers objectAtIndex:CHANGE_WIN_CREDS_VIEW];
   id<CdkChangeWinCreds> changeCreds = [vc representedObject];
   [changeCreds setUsername:username];
   [changeCreds setDomain:domain];
   [changeCreds setOldSecret:@""];
   [changeCreds setSecret:@""];
   [changeCreds setConfirm:@""];
   [self setViewController:vc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController brokerDidRequestDesktop:] --
 *
 *      Present the user with the desktop selection dialog.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop selection dialog is loaded and displayed.
 *
 *-----------------------------------------------------------------------------
 */

-(void)brokerDidRequestDesktop:(CdkBroker *)aBroker // IN/UNUSED
{
   CdkDesktopsViewController *vc = [viewControllers objectAtIndex:DESKTOPS_VIEW];
   [vc setDesktops:[broker desktops]];
   [self setViewController:vc];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController procHelper:didExitWithStatus:] --
 *
 *      Notification callback for RDC exiting.  If it exited "cleanly"
 *      we exit, otherwise print an error and go back to the desktops
 *      list.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May exit, or display desktop list.
 *
 *-----------------------------------------------------------------------------
 */

-(void)procHelper:(CdkProcHelper *)procHelper // IN
didExitWithStatus:(int)status                 // IN
{
   [rdc setDelegate:nil];
   [self setRdc:nil];
   if (!status) {
      // Just exit when the desktop cleanly exits.
      [self brokerDidDisconnect:broker];
   }
   if (!mRdcMonitor->ShouldThrottle()) {
      [NSApp unhide:self];
      [broker reconnectDesktop];
   } else {
      cdk::App::ShowDialog(GTK_MESSAGE_ERROR,
                           _("The desktop has unexpectedly disconnected."));
      [self setDesktop:nil];
      [self setViewController:[viewControllers objectAtIndex:DESKTOPS_VIEW]];
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController procHelper:didWriteError:] --
 *
 *      Implements CdkProcHelperDelegate.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)procHelper:(CdkProcHelper *)procHelper // IN
    didWriteError:(NSString *)error           // IN
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestLaunchDesktop:] --
 *
 *      Connect the user to a desktop.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Desktop's UI is started.
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker             // IN/UNUSED
didRequestLaunchDesktop:(CdkDesktop *)aDesktop // IN
{
   [viewController savePrefs];
   [NSApp hide:self];
   [self setBusyText:nil];
   [self setViewController:[viewControllers objectAtIndex:WAITING_VIEW]];
   [[self window] makeFirstResponder:goBackButton];
   [self setDesktop:aDesktop];

   CdkDesktopSize *size = nil;
   NSSize screenSize = [[[self window] screen] frame].size;
   /*
    * "Large" and "Small" aren't actually defined by the spec, so
    * we're free to choose whatever size we feel is appropriate.  A
    * quarter of the screen (by area) is decently small, and 3/4ths of
    * the dimensions is half-way between small and full screen, so
    * these seem seems decent enough.
    */
   switch ([[CdkPrefs sharedPrefs] desktopSize]) {
   case CDK_PREFS_FULL_SCREEN:
      size = [CdkDesktopSize desktopSizeForFullScreen];
      break;
   case CDK_PREFS_LARGE_WINDOW:
      size = [CdkDesktopSize
                desktopSizeWithWidth:MAX(640, screenSize.width * .75)
                              height:MAX(400, screenSize.height * .75)];
      break;
   case CDK_PREFS_SMALL_WINDOW:
      size = [CdkDesktopSize
                desktopSizeWithWidth:MAX(640, screenSize.width / 2)
                              height:MAX(400, screenSize.height / 2)];
      break;
   case CDK_PREFS_CUSTOM_SIZE:
      size = [[CdkPrefs sharedPrefs] customDesktopSize];
      ASSERT(size);
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }

   switch (cdk::Protocols::GetProtocolFromName([[desktop protocol] utilString])) {
   case cdk::Protocols::RDP:
      [self setRdc:[CdkRdc rdc]];
      [rdc setDelegate:self];
      [[self rdc] startWithConnection:[desktop adaptedDesktop]->GetConnection()
                                 size:size];
      break;
   default:
      NOT_IMPLEMENTED();
      break;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController brokerDidDisconnect:] --
 *
 *      Quit the application.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Application will exit.
 *
 *-----------------------------------------------------------------------------
 */

-(void)brokerDidDisconnect:(CdkBroker *)aBroker // IN/UNSED
{
   [NSApp terminate:self];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didDisconnectTunnelWithReason:] --
 *
 *      Handle tunnel disconnections.  Display the error, and return
 *      to the broker screen.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(void)broker:(CdkBroker *)aBroker               // IN/UNUSED
didDisconnectTunnelWithReason:(NSString *)reason // IN
{
   NSString *message = @"The secure connection to the View Server has "
      "unexpectedly disconnected.";
   if ([reason length]) {
      message = [NSString stringWithFormat:@"%@\n\nReason: %@.", message,
                          reason];
   }

   cdk::App::ShowDialog(GTK_MESSAGE_ERROR, "%s", [message UTF8String]);

   /*
    * If the tunnel really exited, it's probably not going to let us
    * get a new one until we log in again.
    */
   [self brokerDidRequestBroker:broker];
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController broker:didRequestIdentityWithTrustedAuthorities:] --
 *
 *      Have the user select an identity to use for authentication.
 *
 * Results:
 *      An identity, or NULL if none could be found or none were
 *      selected.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(SecIdentityRef)broker:(CdkBroker *)aBroker                        // IN/UNUSED
didRequestIdentityWithTrustedAuthorities:(STACK_OF(X509_NAME) *)CAs // IN
{
   SecIdentitySearchRef search;
   OSStatus rv = SecIdentitySearchCreate(NULL, 0, &search);
   if (rv != noErr) {
      Warning("Could not create search: %d\n", (int)rv);
      return NULL;
   }

   CFMutableArrayRef identities = CFArrayCreateMutable(NULL, 0, NULL);
   SecIdentityRef identity;
   fprintf(stderr, "Searching identities...\n");
   while (errSecItemNotFound != (rv = SecIdentitySearchCopyNext(search,
                                                                &identity))) {
      if (rv == noErr) {
         X509 *x509 = [[CdkKeychain sharedKeychain]
                         certificateWithIdentity:identity];
         if (x509) {
            X509_NAME *issuer = X509_get_issuer_name(x509);
            if (sk_X509_NAME_find(CAs, issuer) < 0) {
               char *dispName = X509_NAME_oneline(issuer, NULL, 0);
               Warning("Cert issuer %s not accepted by server, ignoring "
                       "cert.\n", dispName);
               OPENSSL_free(dispName);
            } else {
               CFRetain(identity);
               CFArrayAppendValue(identities, identity);
            }
            X509_free(x509);
         }
         CFRelease(identity);
      } else {
         NSString *err = (NSString *)SecCopyErrorMessageString(rv, NULL);
         Warning("Skipping an identity due to error: %s\n", [err UTF8String]);
         [err release];
      }
   }
   CFRelease(search);

   switch (CFArrayGetCount(identities)) {
   case 0:
      identity = NULL;
      break;
   case 1:
#ifdef AUTO_SELECT_SINGLE_CERT
      identity = (SecIdentityRef)CFArrayGetValueAtIndex(identities, 0);
      break;
#endif
   default:
      SFChooseIdentityPanel *panel = [SFChooseIdentityPanel
                                        sharedChooseIdentityPanel];
      [panel setAlternateButtonTitle:@"Cancel"];
      int button = [panel runModalForIdentities:(NSArray *)identities
                                        message:@"Choose an identity with which"
                          " to log in."];

      identity = button == NSOKButton ? [panel identity] : NULL;
      break;
   }
   if (identity) {
      CFRetain(identity);
   }

   CFArrayApplyFunction(identities, CFRangeMake(0, CFArrayGetCount(identities)),
                        (CFArrayApplierFunction)CFRelease, NULL);
   CFRelease(identities);

   return identity;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkWindowController setCookieFile:] --
 *
 *      This sets the cookie file for our broker object based on the
 *      passed-in URL.  We base64-encode that url, and append that to
 *      our base cookie file.  This lets us use a separate cookie file
 *      per-broker, to allow multiple instances to work.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Cookie file is created and set on broker.
 *
 *-----------------------------------------------------------------------------
 */

-(void)setCookieFile:(NSString *)brokerUrl // IN
{
   NSArray *paths = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);

   if ([paths count] <= 0) {
      Warning("Could not find application support directory to store "
              "cookies.\n");
      return;
   }

   NSString *cookiesPath =
      [[paths objectAtIndex:0]
         stringByAppendingPathComponents:@"VMware View", @"Cookies", nil];

   NSDictionary *attrs;
   {
      NSArray *keys = [NSArray arrayWithObjects:NSFilePosixPermissions, nil];
      NSArray *objs = [NSArray arrayWithObjects:
                        [NSNumber numberWithInt:COOKIE_DIR_MODE], nil];
      attrs = [NSDictionary dictionaryWithObjects:objs forKeys:keys];
   }

   NSError *error = NULL;
   if (![[NSFileManager defaultManager] createDirectoryAtPath:cookiesPath
                                  withIntermediateDirectories:YES
                                                   attributes:attrs
                                                        error:&error]) {
      Warning("Failed to create cookies directory '%s': %s\n",
              [cookiesPath UTF8String],
              [[error localizedDescription] UTF8String]);
      return;
   }

   char *encUrl = NULL;
   if (Base64_EasyEncode(
          (const uint8 *)[brokerUrl UTF8String],
          [brokerUrl lengthOfBytesUsingEncoding:NSUTF8StringEncoding],
          &encUrl)) {
      cookiesPath =
         [cookiesPath stringByAppendingPathComponent:
                      [NSString stringWithUTF8String:encUrl]];
      cookiesPath = [cookiesPath stringByAppendingPathExtension:@"txt"];
      free(encUrl);
   } else {
      Log("Failed to b64-encode url: %s; using default cookie file.\n",
          [brokerUrl UTF8String]);
      cookiesPath = [cookiesPath stringByAppendingPathComponent:@"cookies.txt"];
   }

   if (cdk::Util::EnsureFilePermissions([cookiesPath UTF8String],
                                        COOKIE_FILE_MODE)) {
      [broker setCookieFile:cookiesPath];
   }
}


@end // @implementation CdkWindowController
