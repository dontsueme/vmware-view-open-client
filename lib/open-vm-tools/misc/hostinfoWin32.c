/* **********************************************************
 * Copyright 1998 VMware, Inc.  All rights reserved. -- VMware Confidential
 * **********************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winbase.h>
#include <winsock2.h>
#include <string.h>
#include <direct.h>
#include <winbase.h>
#include "safetime.h"
#include <process.h>
#include <math.h>
#include <lmcons.h>
#include <winsock.h>
#include <malloc.h>
#include <iphlpapi.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <malloc.h>
#include <winnt.h>

#include "vm_ctype.h"
#include "vmware.h"
#include "str.h"
#include "util.h"
#include "log.h"
#include "hostinfo.h"
#include "backdoor_def.h"
#ifndef __MINGW32__
#include "hostinfoInt.h"
#include "backdoor_types.h"
#include "rateconv.h"
#endif
#include "msg.h"
#include "crypto.h"
#include "config.h"
#include "win32u.h"
#include "unicode.h"
#include "guest_os.h"
#include "vm_atomic.h"
#include "x86cpuid_asm.h"

#include "x86cpuid.h"

typedef BOOL (WINAPI *IsWow64ProcessFn)(HANDLE hProcess, PBOOL Wow64Process);

#if defined(_WIN64)
// from touchBackdoorMasm64.asm
void Hostinfo_BackdoorInOut(Backdoor_proto *myBp);
#endif

#ifndef __MINGW32__
static Bool hostinfoOSVersionInitialized;

static int hostinfoOSVersion[4];
static DWORD hostinfoOSPlatform;

/*
 * In addition to OS name, the Win32 implementation caches a similar OS
 * "detail" value.  See the types' docs for more details.
 */
static OS_TYPE hostinfoCachedOSType;
static OS_DETAIL_TYPE hostinfoCachedOSDetailType;
#endif // __MINGW32__

#define MAX_VALUE_LEN 100

#define STR_OS_WIN32_FULL     "Windows 32s"
#define STR_OS_WIN_2003       "win2003"
#define STR_OS_WIN_2003_FULL  "Windows 2003"
#define STR_OS_WIN_XP         "winXP"
#define STR_OS_WIN_XP_FULL    "Windows XP"
#define STR_OS_WIN_2000       "win2000"
#define STR_OS_WIN_2000_FULL  "Windows 2000"
#define STR_OS_WRKSTAT_4_FULL "Workstation 4.0"

#define STR_OS_WORKST          L"WINNT"
#define STR_OS_WORKST_FULL     "Workstation"
#define STR_OS_SERVER          L"LANMANNT"
#define STR_OS_SERVER_FULL     "Server"
#define STR_OS_SERVERENT       L"SERVERNT"
#define STR_OS_SERVERENT_FULL  "Advanced Server"
#define STR_OS_SP_6A_FULL      "Service Pack 6a"
#define STR_OS_SP_6_FULL       _T("Service Pack 6")
#define C_OS_WIN_95_C         'C'
#define C_OS_WIN_95_B         'B'
#define STR_OS_OSR2           "OSR2"
#define C_OS_WIN_98_A         'A'
#define STR_OS_SE             "SE"

#define STR_OS_DELIMITER " "

/*
 * These are copied from MSDN, since they're not yet available in the current
 * SDK.  (As of writing, values starting at 0x0000001A / HOME_PREMIUM_N are
 * missing from the 6.1.6000 SDK.)
 */

#if !defined(PRODUCT_HOME_PREMIUM_N)
#define PRODUCT_BUSINESS                                0x00000006
#define PRODUCT_BUSINESS_N                              0x00000010
#define PRODUCT_CLUSTER_SERVER                          0x00000012
#define PRODUCT_DATACENTER_SERVER                       0x00000008
#define PRODUCT_DATACENTER_SERVER_CORE                  0x0000000C
#define PRODUCT_DATACENTER_SERVER_CORE_V                0x00000027
#define PRODUCT_DATACENTER_SERVER_V                     0x00000025
#define PRODUCT_ENTERPRISE                              0x00000004
#define PRODUCT_ENTERPRISE_N                            0x0000001B
#define PRODUCT_ENTERPRISE_SERVER                       0x0000000A
#define PRODUCT_ENTERPRISE_SERVER_CORE                  0x0000000E
#define PRODUCT_ENTERPRISE_SERVER_CORE_V                0x00000029
#define PRODUCT_ENTERPRISE_SERVER_IA64                  0x0000000F
#define PRODUCT_ENTERPRISE_SERVER_V                     0x00000026
#define PRODUCT_HOME_BASIC                              0x00000002
#define PRODUCT_HOME_BASIC_N                            0x00000005
#define PRODUCT_HOME_PREMIUM                            0x00000003
#define PRODUCT_HOME_PREMIUM_N                          0x0000001A
#define PRODUCT_HYPERV                                  0x0000002A
#define PRODUCT_MEDIUMBUSINESS_SERVER_MANAGEMENT        0x0000001E
#define PRODUCT_MEDIUMBUSINESS_SERVER_MESSAGING         0x00000020
#define PRODUCT_MEDIUMBUSINESS_SERVER_SECURITY          0x0000001F
#define PRODUCT_SERVER_FOR_SMALLBUSINESS                0x00000018
#define PRODUCT_SERVER_FOR_SMALLBUSINESS_V              0x00000023
#define PRODUCT_SMALLBUSINESS_SERVER                    0x00000009
#define PRODUCT_SMALLBUSINESS_SERVER_PREMIUM            0x00000019
#define PRODUCT_STANDARD_SERVER                         0x00000007
#define PRODUCT_STANDARD_SERVER_CORE                    0x0000000D
#define PRODUCT_STANDARD_SERVER_CORE_V                  0x00000028
#define PRODUCT_STANDARD_SERVER_V                       0x00000024
#define PRODUCT_STARTER                                 0x0000000B
#define PRODUCT_STORAGE_ENTERPRISE_SERVER               0x00000017
#define PRODUCT_STORAGE_EXPRESS_SERVER                  0x00000014
#define PRODUCT_STORAGE_STANDARD_SERVER                 0x00000015
#define PRODUCT_STORAGE_WORKGROUP_SERVER                0x00000016
#define PRODUCT_ULTIMATE                                0x00000001
#define PRODUCT_ULTIMATE_N                              0x0000001C
#define PRODUCT_WEB_SERVER                              0x00000011
#define PRODUCT_WEB_SERVER_CORE                         0x0000001D
#endif  // PRODUCT_HOME_PREMIUM_N

#if !defined(PRODUCT_SERVER_FOUNDATION)
#define PRODUCT_SERVER_FOUNDATION                       0x00000021
#endif

/*
 * Local data
 */

#ifndef __MINGW32__
static RateConv_Params hostinfoPCToUS;
static Bool hostinfoNoPC;
static Bool hostinfoHasPC;
static uint64 hostinfoPCHz;

static Atomic_Ptr hostinfoCSMemory;  // implicitely initialized to NULL

static Bool hostinfoStressReset;
static Bool hostInfoStressRound;

/*
 * Local functions
 */

static Bool HostinfoPCInit(void);

/*
 *----------------------------------------------------------------------
 *
 * HostinfoOSVersionInit --
 *
 *      Compute the OS version information
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      hostinfoOS* variables are filled in.
 *
 *----------------------------------------------------------------------
 */

void
HostinfoOSVersionInit(void)
{
   OSVERSIONINFO info;
   OSVERSIONINFOEX infoEx;

   if (hostinfoOSVersionInitialized) {
      return;
   }

   info.dwOSVersionInfoSize = sizeof (info);
   if (!GetVersionEx(&info)) {
      Warning("Unable to get OS version.\n");
      NOT_IMPLEMENTED();
   }
   ASSERT(ARRAYSIZE(hostinfoOSVersion) >= 4);
   hostinfoOSVersion[0] = info.dwMajorVersion;
   hostinfoOSVersion[1] = info.dwMinorVersion;
   hostinfoOSVersion[2] = info.dwBuildNumber & 0xffff;

   /*
    * Get the service pack number. We don't care much about NT4 hosts
    * so we can use OSVERSIONINFOEX without checking for Windows NT 4.0 SP6 
    * or later versions.
    */

   infoEx.dwOSVersionInfoSize = sizeof infoEx;
   if (GetVersionEx((OSVERSIONINFO *) &infoEx)) {
      hostinfoOSVersion[3] = infoEx.wServicePackMajor;
   }
   hostinfoOSPlatform = info.dwPlatformId;

   hostinfoOSVersionInitialized = TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_OSIsWinNT --
 *
 *      This is Windows NT or descendant.
 *
 * Results:
 *      TRUE if Windows NT or descendant.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
Hostinfo_OSIsWinNT(void)
{
   HostinfoOSVersionInit();

   return hostinfoOSPlatform == VER_PLATFORM_WIN32_NT;
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_OSVersion --
 *
 *      Host OS release info.
 *
 * Results:
 *      The i-th component of a dotted release string.
 *	0 if i is greater than the number of components we support.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Hostinfo_OSVersion(int i)
{
   HostinfoOSVersionInit();

   return i < ARRAYSIZE(hostinfoOSVersion) ? hostinfoOSVersion[i] : 0;
}

#endif // __MINGW32__

/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_GetTimeOfDay --
 *
 *      Return the current time of day according to the host.  We want
 *      UTC time (seconds since Jan 1, 1970).
 *
 * Results:
 *      Time of day in microseconds.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void 
Hostinfo_GetTimeOfDay(VmTimeType *time)
{
   struct _timeb t;

   _ftime(&t);

   *time = (t.time * 1000000) + t.millitm * 1000;
}


#ifndef __MINGW32__


/*
 *----------------------------------------------------------------------------
 *
 * Hostinfo_GetSystemBitness --
 *
 *      Determines the operating system's bitness.
 *
 * Return value:
 *      32 or 64 on success, negative value on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------------
 */

int
Hostinfo_GetSystemBitness(void)
{
   typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, BOOL *);
   typedef void (WINAPI *LPFN_GETNATIVESYSTEMINFO) (LPSYSTEM_INFO);

   SYSTEM_INFO si;
   LPFN_ISWOW64PROCESS pfnIsWow64Process;
   LPFN_GETNATIVESYSTEMINFO pfnGetNativeSystemInfo;
   BOOL bIsWow64;
   HMODULE handle = Win32U_GetModuleHandle("kernel32");

   pfnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(handle,
                                                            "IsWow64Process");

   pfnGetNativeSystemInfo = (LPFN_GETNATIVESYSTEMINFO) GetProcAddress(handle,
                                                       "GetNativeSystemInfo");

   if (pfnIsWow64Process != NULL &&
       pfnGetNativeSystemInfo != NULL &&
       pfnIsWow64Process(GetCurrentProcess(), &bIsWow64) &&
       bIsWow64) {
      pfnGetNativeSystemInfo(&si);
   } else {
      GetSystemInfo(&si);
   }

   if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
      return 64;
   } else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
      return 32;
   }

   return -1;
}


/*
 *-----------------------------------------------------------------------------
 *
 * HostinfoGetProductType --
 *
 *      Get the productType
 *
 * Return value:
 *      A productType
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static DWORD
HostinfoProductType(void)
{
   FARPROC pGPI;
   DWORD productType;
   HMODULE handle = Win32U_GetModuleHandle("kernel32");

   pGPI = (FARPROC) GetProcAddress(handle, "GetProductInfo");

   if (pGPI) {
      if (!pGPI(6, 0, 0, 0, &productType)) {
         productType = PRODUCT_UNDEFINED;
      }
   } else {
      productType = PRODUCT_UNDEFINED;
   }

   return productType;
}


/*
 *-----------------------------------------------------------------------------
 *
 * HostinfoTheVistaMess --
 *
 *      Deal with the Vista naming mess...
 *
 * Return value:
 *      Vista names
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
HostinfoTheVistaMess(OSVERSIONINFOEX *os,    // OUT:
                     char *localOSName,      // OUT:
                     char *localOSFullName)  // OUT:
{
   Bool vistaSystem = FALSE;
   Bool supportLegacyVistaNames = TRUE;
   DWORD productType = HostinfoProductType();

#define POPULATENAMES(macroName) do {                              \
   Str_Strcpy(localOSName,                                              \
              Hostinfo_GetSystemBitness() == 64 ? macroName ## _X64     \
                                                : macroName,            \
              MAX_OS_NAME_LEN);                                         \
   Str_Strcpy(localOSFullName, macroName ## _FULL, MAX_OS_FULLNAME_LEN);\
} while (0)

tryDefault:
   /*
    * Items listed in the same order as #define'd above, which happens to be
    * in alphabetical order (as listed by MSDN).
    */

    switch (productType)
    {
    case PRODUCT_BUSINESS:
       vistaSystem = TRUE;
       POPULATENAMES(STR_OS_WIN_VISTA_BUSINESS);
       break;
   case PRODUCT_CLUSTER_SERVER:
       POPULATENAMES(STR_OS_WIN_2008_CLUSTER);
       break;
   case PRODUCT_DATACENTER_SERVER:
   case PRODUCT_DATACENTER_SERVER_V:
       POPULATENAMES(STR_OS_WIN_2008_DATACENTER);
       break;
   case PRODUCT_DATACENTER_SERVER_CORE:
   case PRODUCT_DATACENTER_SERVER_CORE_V:
       POPULATENAMES(STR_OS_WIN_2008_DATACENTER_CORE);
       break;
   case PRODUCT_ENTERPRISE:
   case PRODUCT_ENTERPRISE_N:
       vistaSystem = TRUE;
       POPULATENAMES(STR_OS_WIN_VISTA_ENTERPRISE);
       break;
   case PRODUCT_ENTERPRISE_SERVER:
   case PRODUCT_ENTERPRISE_SERVER_V:
       POPULATENAMES(STR_OS_WIN_2008_ENTERPRISE);
       break;
   case PRODUCT_ENTERPRISE_SERVER_CORE:
   case PRODUCT_ENTERPRISE_SERVER_CORE_V:
       POPULATENAMES(STR_OS_WIN_2008_ENTERPRISE_CORE);
       break;
   case PRODUCT_ENTERPRISE_SERVER_IA64:
       /* XXX IA64?  Really? */
       POPULATENAMES(STR_OS_WIN_2008_ENTERPRISE_ITANIUM);
       break;
   case PRODUCT_HOME_BASIC:
   case PRODUCT_HOME_BASIC_N:
       vistaSystem = TRUE;
       POPULATENAMES(STR_OS_WIN_VISTA_HOME_BASIC);
       break;
   case PRODUCT_HOME_PREMIUM:
   case PRODUCT_HOME_PREMIUM_N:
       vistaSystem = TRUE;
       POPULATENAMES(STR_OS_WIN_VISTA_HOME_PREMIUM);
       break;
   case PRODUCT_MEDIUMBUSINESS_SERVER_MANAGEMENT:
       POPULATENAMES(STR_OS_WIN_2008_MEDIUM_MANAGEMENT);
       break;
   case PRODUCT_MEDIUMBUSINESS_SERVER_MESSAGING:
      POPULATENAMES(STR_OS_WIN_2008_MEDIUM_MESSAGING);
      break;
   case PRODUCT_MEDIUMBUSINESS_SERVER_SECURITY:
      POPULATENAMES(STR_OS_WIN_2008_MEDIUM_SECURITY);
      break;
   case PRODUCT_SERVER_FOR_SMALLBUSINESS:
   case PRODUCT_SERVER_FOR_SMALLBUSINESS_V:
      POPULATENAMES(STR_OS_WIN_2008_SERVER_FOR_SMALLBUSINESS);
      break;
   case PRODUCT_SMALLBUSINESS_SERVER:
      POPULATENAMES(STR_OS_WIN_2008_SMALL_BUSINESS);
      break;
   case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
      POPULATENAMES(STR_OS_WIN_2008_SMALL_BUSINESS_PREMIUM);
      break;
   case PRODUCT_STARTER:
      vistaSystem = TRUE;
      POPULATENAMES(STR_OS_WIN_VISTA_STARTER);
      break;
   case PRODUCT_STANDARD_SERVER:
      POPULATENAMES(STR_OS_WIN_2008_STANDARD);
      break;
   case PRODUCT_STANDARD_SERVER_CORE:
      POPULATENAMES(STR_OS_WIN_2008_STANDARD_CORE);
      break;
   case PRODUCT_STORAGE_ENTERPRISE_SERVER:
      POPULATENAMES(STR_OS_WIN_2008_STORAGE_ENTERPRISE);
      break;
   case PRODUCT_STORAGE_EXPRESS_SERVER:
      POPULATENAMES(STR_OS_WIN_2008_STORAGE_EXPRESS);
      break;
   case PRODUCT_STORAGE_STANDARD_SERVER:
      POPULATENAMES(STR_OS_WIN_2008_STORAGE_STANDARD);
      break;
   case PRODUCT_STORAGE_WORKGROUP_SERVER:
      POPULATENAMES(STR_OS_WIN_2008_STORAGE_WORKGROUP);
      break;
   case PRODUCT_ULTIMATE:
   case PRODUCT_ULTIMATE_N:
      vistaSystem = TRUE;
      POPULATENAMES(STR_OS_WIN_VISTA_ULTIMATE);
      break;
   case PRODUCT_WEB_SERVER:
      POPULATENAMES(STR_OS_WIN_2008_WEB_SERVER);
      break;

   default:
      productType = os->wProductType == VER_NT_WORKSTATION ?
                               PRODUCT_HOME_BASIC : PRODUCT_STANDARD_SERVER;
     goto tryDefault;
   }

   /*
    * It seems that most/all flavors of Windows Vista and Windows 2008 can
    * come in 32 or 64 bit mode. Append the suffix to the full name for any
    * edition of these guests.
    */

   if (Hostinfo_GetSystemBitness() == 64) {
      Str_Strcat(localOSFullName, STR_OS_WIN_64_BIT_EXTENSION,
                 MAX_OS_FULLNAME_LEN);
   } else {
      Str_Strcat(localOSFullName, STR_OS_WIN_32_BIT_EXTENSION,
                 MAX_OS_FULLNAME_LEN);
   }

   /*
    * If this was a Vista system, then we may need to revert to the old form
    * of Vista names. This discards the edition information.
    */

   if ((supportLegacyVistaNames) && (vistaSystem)) {
      if (Hostinfo_GetSystemBitness() == 64) {
         Str_Strcpy(localOSFullName, STR_OS_WIN_VISTA_X64_FULL,
                    MAX_OS_FULLNAME_LEN);
         Str_Strcpy(localOSName, STR_OS_WIN_VISTA_X64, MAX_OS_NAME_LEN);
      } else {
         Str_Strcpy(localOSFullName, STR_OS_WIN_VISTA_FULL,
                    MAX_OS_FULLNAME_LEN);
         Str_Strcpy(localOSName, STR_OS_WIN_VISTA, MAX_OS_NAME_LEN);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * HostinfoTheWindows7Mess --
 *
 *      Deal with the Windows 7 naming mess...
 *
 * Return value:
 *      Windows 7 names
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static void
HostinfoTheWindows7Mess(char *localOSName,      // OUT:
                        char *localOSFullName)  // OUT:
{
   /*
    * Make the default Windows 7 with the appropriate "bit-ness". We'll
    * override the default as appropriate discovery information is found.
    */

   if (Hostinfo_GetSystemBitness() == 64) {
      Str_Strcpy(localOSName, STR_OS_WIN_SEVEN_X64, MAX_OS_NAME_LEN);
   } else {
      Str_Strcpy(localOSName, STR_OS_WIN_SEVEN, MAX_OS_NAME_LEN);
   }

   Str_Strcpy(localOSFullName, STR_OS_WIN_SEVEN_GENERIC, MAX_OS_FULLNAME_LEN);

   /*
    * Examine the productInfo signature and override the short and long
    * OS names as appropriate.
    *
    * All Server 2008 R2 offerings are 64 bit only at the time of writing.
    */

   switch (HostinfoProductType()) {
   case PRODUCT_SERVER_FOUNDATION:
      Str_Strcpy(localOSName, STR_OS_WIN_2008R2_X64, MAX_OS_NAME_LEN);
      Str_Strcpy(localOSFullName, STR_OS_WIN_2008R2_FOUNDATION_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_ENTERPRISE_SERVER:
   case PRODUCT_ENTERPRISE_SERVER_V:
   case PRODUCT_ENTERPRISE_SERVER_CORE:
   case PRODUCT_ENTERPRISE_SERVER_CORE_V:
      Str_Strcpy(localOSName, STR_OS_WIN_2008R2_X64, MAX_OS_NAME_LEN);
      Str_Strcpy(localOSFullName, STR_OS_WIN_2008R2_ENTERPRISE_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_STANDARD_SERVER:
   case PRODUCT_STANDARD_SERVER_V:
   case PRODUCT_STANDARD_SERVER_CORE:
   case PRODUCT_STANDARD_SERVER_CORE_V:
      Str_Strcpy(localOSName, STR_OS_WIN_2008R2_X64, MAX_OS_NAME_LEN);
      Str_Strcpy(localOSFullName, STR_OS_WIN_2008R2_STANDARD_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_DATACENTER_SERVER:
   case PRODUCT_DATACENTER_SERVER_V:
   case PRODUCT_DATACENTER_SERVER_CORE:
   case PRODUCT_DATACENTER_SERVER_CORE_V:
      Str_Strcpy(localOSName, STR_OS_WIN_2008R2_X64, MAX_OS_NAME_LEN);
      Str_Strcpy(localOSFullName, STR_OS_WIN_2008R2_DATACENTER_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_WEB_SERVER:
   case PRODUCT_WEB_SERVER_CORE:
      Str_Strcpy(localOSName, STR_OS_WIN_2008R2_X64, MAX_OS_NAME_LEN);
      Str_Strcpy(localOSFullName, STR_OS_WIN_2008R2_WEB_SERVER_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_STARTER:
      Str_Strcpy(localOSFullName, STR_OS_WIN_SEVEN_STARTER_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_HOME_BASIC:
      Str_Strcpy(localOSFullName, STR_OS_WIN_SEVEN_HOME_BASIC_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_HOME_PREMIUM:
      Str_Strcpy(localOSFullName, STR_OS_WIN_SEVEN_HOME_PREMIUM_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_ULTIMATE:
      Str_Strcpy(localOSFullName, STR_OS_WIN_SEVEN_ULTIMATE_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_BUSINESS:
   case PRODUCT_BUSINESS_N:
      Str_Strcpy(localOSFullName, STR_OS_WIN_SEVEN_PROFESSIONAL_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_ENTERPRISE:
      Str_Strcpy(localOSFullName, STR_OS_WIN_SEVEN_ENTERPRISE_FULL,
                 MAX_OS_FULLNAME_LEN);
      break;

   case PRODUCT_UNDEFINED:
   default:
      /* The defaults were already set above */
      break;
   }

   /*
    * Append the 32/64 bit suffix to the full name.
    */

   if (Hostinfo_GetSystemBitness() == 64) {
       Str_Strcat(localOSFullName, STR_OS_WIN_64_BIT_EXTENSION,
                  MAX_OS_FULLNAME_LEN);
   } else {
       Str_Strcat(localOSFullName, STR_OS_WIN_32_BIT_EXTENSION,
                  MAX_OS_FULLNAME_LEN);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * HostinfoOSData --
 *
 *      Determine the name, long name, OS generic type and OS specific type.
 *
 * Return value:
 *      Returns TRUE on success and FALSE on failure.
 *
 * Side effects:
 *      Cache values are set when returning TRUE;
 *
 *-----------------------------------------------------------------------------
 */

Bool
HostinfoOSData(void)
{
   OSVERSIONINFOEX os;
   BOOL bOsVersionInfoEx;
   char szServicePack[MAX_VALUE_LEN];

   OS_TYPE localOSType;
   OS_DETAIL_TYPE localOSDetailType;
   char localOSName[MAX_OS_NAME_LEN];
   char localOSFullName[MAX_OS_FULLNAME_LEN];

   static Atomic_uint32 mutex = {0};

   /*
    * In case nothing works out, we return empty strings.
    */

    Str_Strcpy(localOSFullName, STR_OS_EMPTY, MAX_OS_FULLNAME_LEN);
    Str_Strcpy(localOSName, STR_OS_EMPTY, MAX_OS_NAME_LEN);
    localOSType = OS_UNKNOWN;
    localOSDetailType = OS_DETAIL_UNKNOWN;

   /*
    * Try calling GetVersionEx using the OSVERSIONINFOEX structure.
    * If that fails, try using the OSVERSIONINFO structure.
    *
    * Based on the MSDN documentation, os.dwOSVersionInfoSize has to be set
    * to sizeof (OSVERSIONINFOEX) if we are using GetVersionEx and to
    * sizeof (OSVERSIONINFO) if we are using GetVersion().
    */

   ZeroMemory(&os, sizeof(OSVERSIONINFOEX));
   os.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

   if (!(bOsVersionInfoEx = GetVersionEx((OSVERSIONINFO *) &os))) {
      os.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);

      if (!GetVersionEx((OSVERSIONINFO *) &os)) {
         return FALSE;
      }
   }

   switch (os.dwPlatformId)
   {
      /*
       * Test for the Windows NT product family.
       */

      case VER_PLATFORM_WIN32_NT:
      {
         /*
          * First, remember default strings, in case we cannot figure out the
          * details later. When we get more detailed information later we will
          * overwrite these default values.
          */

         if (os.dwMajorVersion == 6 && os.dwMinorVersion >= 1) {
            if (os.wProductType == VER_NT_WORKSTATION) {
               /* Windows 7 */
               localOSDetailType = OS_DETAIL_WINSEVEN;
            } else {
               /* WIN2K8R2 */
               localOSDetailType = OS_DETAIL_WIN2K8R2;
            }
         } else if (os.dwMajorVersion == 6 && os.dwMinorVersion == 0) {
            if (os.wProductType == VER_NT_WORKSTATION) {
               /* Vista */
               localOSDetailType = OS_DETAIL_VISTA;
            } else {
               /* WIN2K8 */
               localOSDetailType = OS_DETAIL_WIN2K8;
            }
         } else if (os.dwMajorVersion == 5 && os.dwMinorVersion >= 2) {
            localOSDetailType = OS_DETAIL_WIN2K3;
         } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 1) {
            localOSDetailType = OS_DETAIL_WINXP;
         } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 0) {
            localOSDetailType = OS_DETAIL_WIN2K;
         } else if (os.dwMajorVersion <= 4) {
            localOSDetailType = OS_DETAIL_WINNT;
         }

         if (os.dwMajorVersion == 6 && os.dwMinorVersion >= 1) {
            HostinfoTheWindows7Mess(localOSName, localOSFullName);
         } else if (os.dwMajorVersion == 6 && os.dwMinorVersion == 0) {
            HostinfoTheVistaMess(&os, localOSName, localOSFullName);
         } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 2) {
            Str_Strcpy(localOSFullName, STR_OS_WIN_2003_FULL,
                       MAX_OS_FULLNAME_LEN);
            Str_Strcpy(localOSName, STR_OS_WIN_2003, MAX_OS_NAME_LEN);
         } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 1) {
            Str_Strcpy(localOSFullName, STR_OS_WIN_XP_FULL,
                       MAX_OS_FULLNAME_LEN);
            Str_Strcpy(localOSName, STR_OS_WIN_XP, MAX_OS_NAME_LEN);
         } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 0) {
            Str_Strcpy(localOSFullName, STR_OS_WIN_2000_FULL,
                       MAX_OS_FULLNAME_LEN);
            Str_Strcpy(localOSName, STR_OS_WIN_2000, MAX_OS_NAME_LEN);
         } else {
            Str_Strcpy(localOSFullName, STR_OS_WIN_NT_FULL,
                       MAX_OS_FULLNAME_LEN);
            Str_Strcpy(localOSName, STR_OS_WIN_NT, MAX_OS_NAME_LEN);
         }

         /* Test for specific product on Windows NT 4.0 SP6 and later. */
         if (bOsVersionInfoEx) {
            if (os.wProductType == VER_NT_WORKSTATION) {
               if (os.dwMajorVersion == 4) {
                  localOSDetailType = OS_DETAIL_WINNT;
                  Str_Strcat(localOSFullName, STR_OS_DELIMITER,
                             MAX_OS_FULLNAME_LEN);
                  Str_Strcat(localOSFullName, STR_OS_WRKSTAT_4_FULL,
                             MAX_OS_FULLNAME_LEN);
               } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 2) {
                  /* XP x64 Edition */
	          localOSDetailType = OS_DETAIL_WINXP_X64_PRO;
                  Str_Strcpy(localOSFullName, STR_OS_WIN_XP_PRO_X64_FULL,
                             MAX_OS_FULLNAME_LEN);
                  Str_Strcpy(localOSName, STR_OS_WIN_XP_PRO_X64,
                             MAX_OS_NAME_LEN);
               } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 1) {
                  /* XP */
                  if (os.wSuiteMask & VER_SUITE_PERSONAL) {
                     /* Home */
                     localOSDetailType = OS_DETAIL_WINXP_HOME;
                     Str_Strcpy(localOSFullName, STR_OS_WIN_XP_HOME_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_XP_HOME,
                                MAX_OS_NAME_LEN);
                  } else {
                     /* Professional */
                     localOSDetailType = OS_DETAIL_WINXP_PRO;
                     Str_Strcpy(localOSFullName, STR_OS_WIN_XP_PRO_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_XP_PRO,
                                MAX_OS_NAME_LEN);
                  }
               } else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 0) {
                  /* 2000 Professional */
                  localOSDetailType = OS_DETAIL_WIN2K_PRO;
                  Str_Strcpy(localOSFullName, STR_OS_WIN_2000_PRO_FULL,
                             MAX_OS_FULLNAME_LEN);
                  Str_Strcpy(localOSName, STR_OS_WIN_2000_PRO,
                             MAX_OS_NAME_LEN);
               }
            }
            else if (os.wProductType == VER_NT_SERVER ||
                     os.wProductType == VER_NT_DOMAIN_CONTROLLER) {
               if (os.dwMajorVersion == 5 && os.dwMinorVersion == 2) {
                  /* 2003 */
                  localOSDetailType = OS_DETAIL_WIN2K3_ST;

                  if (os.wSuiteMask & VER_SUITE_DATACENTER) {
                     /* Datacenter */
                     localOSDetailType = OS_DETAIL_WIN2K3_BUS;
                     Str_Strcpy(localOSFullName, STR_OS_WIN_NET_DC_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_NET_DC,
                                MAX_OS_NAME_LEN);
                  } else if (os.wSuiteMask & VER_SUITE_ENTERPRISE) {
                     /* Enterprise */
                     localOSDetailType = OS_DETAIL_WIN2K3_EN;
                     Str_Strcpy(localOSFullName, STR_OS_WIN_NET_EN_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_NET_EN,
                                MAX_OS_NAME_LEN);
                  } else if (os.wSuiteMask & VER_SUITE_BLADE) {
                     /* Web Edition */
                     localOSDetailType = OS_DETAIL_WIN2K3_WEB;
                     Str_Strcpy(localOSFullName, STR_OS_WIN_NET_WEB_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_NET_WEB,
                                MAX_OS_NAME_LEN);
                  } else if (os.wSuiteMask & 0x00004000 /* VER_SUITE_COMPUTE_SERVER */) { // missing from toolchain
                     /* Compute Cluster Edition */
                     Str_Strcpy(localOSFullName,
                                STR_OS_WIN_NET_COMPCLUSTER_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName,
                                STR_OS_WIN_NET_COMPCLUSTER, MAX_OS_NAME_LEN);
                  } else if (os.wSuiteMask & 0x00002000 /* VER_SUITE_STORAGE_SERVER */) {  // missing from toolchain
                     /* Storage Server */
                     Str_Strcpy(localOSFullName,
                                STR_OS_WIN_NET_STORAGESERVER_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName,
                                STR_OS_WIN_NET_STORAGESERVER, MAX_OS_NAME_LEN);
                  } else if (((os.wSuiteMask & VER_SUITE_SMALLBUSINESS) &&
                              (os.wSuiteMask & VER_SUITE_SMALLBUSINESS_RESTRICTED))) {
                     /*
                      * Testing VER_SUITE_SMALLBUSINESS alone is not reliable.
                      * See http://msdn2.microsoft.com/en-us/library/ms724833.aspx
                      */

                     /* Business */
                     Str_Strcpy(localOSFullName, STR_OS_WIN_NET_BUS_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_NET_BUS,
                                MAX_OS_NAME_LEN);
                  } else {
                     /* Standard */
                     Str_Strcpy(localOSFullName, STR_OS_WIN_NET_ST_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_NET_ST,
                                MAX_OS_NAME_LEN);
                  }
                  if (Hostinfo_GetSystemBitness() == 64) {
                     /*
                      * x64 only for Datacenter, Enterprise and Standard
                      * editions: http://support.microsoft.com/kb/888733
                      */

                     Str_Strcat(localOSName, STR_OS_64BIT_SUFFIX,
                                MAX_OS_NAME_LEN);
                     Str_Strcat(localOSFullName,
                                STR_OS_64BIT_SUFFIX_FULL, MAX_OS_FULLNAME_LEN);
                  }
               }
               else if (os.dwMajorVersion == 5 && os.dwMinorVersion == 0) {
                  /* 2000 */
                  if (os.wSuiteMask & VER_SUITE_DATACENTER) {
                     /* Data Center server */
                     localOSDetailType = OS_DETAIL_WIN2K_SERV;
                     Str_Strcpy(localOSFullName,
                                STR_OS_WIN_2000_DATACENT_SERV_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_2000_DATACENT_SERV,
                                MAX_OS_NAME_LEN);
                  } else if (os.wSuiteMask & VER_SUITE_ENTERPRISE) {
                     /* Advanced server */
                     localOSDetailType = OS_DETAIL_WIN2K_ADV_SERV;
                     Str_Strcpy(localOSFullName,
                                STR_OS_WIN_2000_ADV_SERV_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_2000_ADV_SERV,
                                MAX_OS_NAME_LEN);
                  } else {
                     /* Standard server */
                     localOSDetailType = OS_DETAIL_WIN2K_SERV;
                     Str_Strcpy(localOSFullName, STR_OS_WIN_2000_SERV_FULL,
                                MAX_OS_FULLNAME_LEN);
                     Str_Strcpy(localOSName, STR_OS_WIN_2000_SERV,
                                MAX_OS_NAME_LEN);
                  }
               }
            }
         } else {
            /*
             * To get the full name for Windows NT 4.0 SP5 and earlier
             * we need to look in the registry for specific keys and values.
             */

            HKEY hKey;
            wchar_t szProductType[MAX_VALUE_LEN];
            DWORD dwBufLen = (DWORD) sizeof szProductType;
            LONG lRet;

           lRet = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions",
                                0, KEY_QUERY_VALUE, &hKey);

            if (lRet != ERROR_SUCCESS) {
               return FALSE;
            }

            lRet = RegQueryValueExW(hKey, L"ProductType", NULL, NULL,
                                    (LPBYTE) szProductType, &dwBufLen);

            if ((lRet != ERROR_SUCCESS) || (dwBufLen > MAX_VALUE_LEN)) {
               return FALSE;
            }

            RegCloseKey(hKey);

            if (wcscmp(STR_OS_WORKST, szProductType) == 0) {
               Str_Strcat(localOSFullName, STR_OS_DELIMITER,
                          MAX_OS_FULLNAME_LEN);
               Str_Strcat(localOSFullName, STR_OS_WORKST_FULL,
                          MAX_OS_FULLNAME_LEN);
            } else if (wcscmp(STR_OS_SERVER, szProductType) == 0) {
               Str_Strcat(localOSFullName, STR_OS_DELIMITER,
                          MAX_OS_FULLNAME_LEN);
               Str_Strcat(localOSFullName, STR_OS_SERVER_FULL,
                          MAX_OS_FULLNAME_LEN);
            } else if (wcscmp(STR_OS_SERVERENT, szProductType) == 0) {
               Str_Strcat(localOSFullName, STR_OS_DELIMITER,
                          MAX_OS_FULLNAME_LEN);
               Str_Strcat(localOSFullName, STR_OS_SERVERENT_FULL,
                          MAX_OS_FULLNAME_LEN);
            }
         }

         /* Display service pack (if any) and build number. */
         if (os.dwMajorVersion == 4 &&
             lstrcmpi(os.szCSDVersion, STR_OS_SP_6_FULL) == 0) {
            HKEY hKey;
            LONG lRet;

            /*  Test for SP6 versus SP6a. */
            lRet = Win32U_RegOpenKeyEx(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Hotfix\\Q246009",
                                       0, KEY_QUERY_VALUE, &hKey);

            if (lRet == ERROR_SUCCESS) {
               Str_Sprintf(szServicePack, MAX_VALUE_LEN, "%s%s (Build %d)",
                           STR_OS_DELIMITER, STR_OS_SP_6A_FULL,
                           os.dwBuildNumber & 0xFFFF);
            } else {
               /* Windows NT 4.0 prior to SP6a */
               Str_Sprintf(szServicePack, MAX_VALUE_LEN, "%s%s (Build %d)",
                           STR_OS_DELIMITER, os.szCSDVersion,
                           os.dwBuildNumber & 0xFFFF);
            }
            RegCloseKey(hKey);
         } else {
            /* Windows NT 3.51 and earlier or Windows 2000 and later */
            Str_Sprintf(szServicePack, MAX_VALUE_LEN, "%s%s (Build %d)",
                        STR_OS_DELIMITER, os.szCSDVersion,
                        os.dwBuildNumber & 0xFFFF);
         }

         /* Append the Service Pack Info to the localOSName. */

         Str_Strcat(localOSFullName, szServicePack,
                    MAX_OS_FULLNAME_LEN);

         break;

     }
      /* Test for the Windows 95 product family. */
      case VER_PLATFORM_WIN32_WINDOWS:
      {
         if (os.dwMajorVersion == 4 && os.dwMinorVersion == 0) {
             localOSDetailType = OS_DETAIL_WIN95;
             Str_Strcpy(localOSFullName, STR_OS_WIN_95_FULL,
                        MAX_OS_FULLNAME_LEN);
             Str_Strcpy(localOSName, STR_OS_WIN_95, MAX_OS_NAME_LEN);

             if (os.szCSDVersion[1] == C_OS_WIN_95_C ||
                 os.szCSDVersion[1] == C_OS_WIN_95_B) {
                Str_Strcat(localOSFullName, STR_OS_DELIMITER,
                           MAX_OS_FULLNAME_LEN);
                Str_Strcat(localOSFullName, STR_OS_OSR2, MAX_OS_FULLNAME_LEN);
             }
         } else if (os.dwMajorVersion == 4 && os.dwMinorVersion == 10) {
             localOSDetailType = OS_DETAIL_WIN98;
             Str_Strcpy(localOSFullName, STR_OS_WIN_98_FULL,
                        MAX_OS_FULLNAME_LEN);
             Str_Strcpy(localOSName, STR_OS_WIN_98, MAX_OS_NAME_LEN);
             if (os.szCSDVersion[1] == C_OS_WIN_98_A) {
                Str_Strcat(localOSFullName, STR_OS_DELIMITER,
                           MAX_OS_FULLNAME_LEN);
                Str_Strcat(localOSFullName, STR_OS_SE, MAX_OS_FULLNAME_LEN);
             }
         }
         if (os.dwMajorVersion == 4 && os.dwMinorVersion == 90) {
             localOSDetailType = OS_DETAIL_WINME;
             Str_Strcpy(localOSFullName, STR_OS_WIN_ME_FULL,
                        MAX_OS_FULLNAME_LEN);
             Str_Strcpy(localOSName, STR_OS_WIN_ME, MAX_OS_NAME_LEN);
         }
         break;
      }
      case VER_PLATFORM_WIN32s:
      {
         Str_Strcpy(localOSFullName, STR_OS_WIN32_FULL, MAX_OS_FULLNAME_LEN);
         break;
      }
   }
   switch (localOSDetailType) {
      case OS_DETAIL_WIN95:
         localOSType = OS_WIN95;
         break;
      case OS_DETAIL_WIN98:
         localOSType = OS_WIN98;
         break;
      case OS_DETAIL_WINME:
         localOSType = OS_WINME;
         break;
      case OS_DETAIL_WINNT:
         localOSType = OS_WINNT;
         break;
      case OS_DETAIL_WIN2K:
      case OS_DETAIL_WIN2K_PRO:
      case OS_DETAIL_WIN2K_SERV:
      case OS_DETAIL_WIN2K_ADV_SERV:
         localOSType = OS_WIN2K;
         break;
      case OS_DETAIL_WINXP:
      case OS_DETAIL_WINXP_HOME:
      case OS_DETAIL_WINXP_PRO:
      case OS_DETAIL_WINXP_X64_PRO:
         localOSType = OS_WINXP;
         break;
      case OS_DETAIL_WIN2K3:
      case OS_DETAIL_WIN2K3_WEB:
      case OS_DETAIL_WIN2K3_ST:
      case OS_DETAIL_WIN2K3_EN:
      case OS_DETAIL_WIN2K3_BUS:
         localOSType = OS_WIN2K3;
         break;
      case OS_DETAIL_WIN2K8:
      case OS_DETAIL_VISTA:
         localOSType = OS_VISTA;
         break;
      case OS_DETAIL_WIN2K8R2:
      case OS_DETAIL_WINSEVEN:
         localOSType = OS_WINSEVEN;
         break;
      case OS_DETAIL_UNKNOWN:
      default:
         localOSType = OS_UNKNOWN;
         break;
   }

   /*
    * Serialize access. Collisions should be rare - plus the value will
    * get cached and this won't get called anymore.
    */

   while (Atomic_ReadWrite(&mutex, 1)); // Spinlock.

   if (!HostinfoOSNameCacheValid) {
      hostinfoCachedOSType = localOSType;
      hostinfoCachedOSDetailType = localOSDetailType;

      Str_Strcpy(HostinfoCachedOSName, localOSName, MAX_OS_NAME_LEN);
      Str_Strcpy(HostinfoCachedOSFullName, localOSFullName,
                 MAX_OS_FULLNAME_LEN);

      HostinfoOSNameCacheValid = TRUE;
   }

   Atomic_Write(&mutex, 0);  // unlock

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_GetOSDetailType --
 *
 *    Returns an enum of the current OS type.
 *
 * Results:
 *    OS_DETAIL_TYPE of the OS the caller is running.
 *    OS_DETAIL_UNKNOWN if error.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

OS_DETAIL_TYPE
Hostinfo_GetOSDetailType(void)
{
   OS_DETAIL_TYPE osType;

   if (HostinfoOSNameCacheValid) {
      osType = hostinfoCachedOSDetailType;
   } else {
      osType = HostinfoOSData() ? hostinfoCachedOSDetailType :
                                  OS_DETAIL_UNKNOWN;
   }

   return osType;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_GetOSType --
 *
 *    Returns an enum of the current OS type.
 *
 * Results:
 *    OS_TYPE of the OS the caller is running.
 *    OS_UNKNOWN if error.
 *
 * Side effects:
 *    None.
 *
 *-----------------------------------------------------------------------------
 */

OS_TYPE
Hostinfo_GetOSType(void)
{
   OS_TYPE osType;

   if (HostinfoOSNameCacheValid) {
      osType = hostinfoCachedOSType;
   } else {
      osType = HostinfoOSData() ? hostinfoCachedOSType : OS_UNKNOWN;
   }

   return osType;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_NumCPUs--
 *
 *      Get the number of logical CPUs on the host.  If the cpus are
 *      hyperthread capable, this number may be larger than the number of
 *      physical cpus.  For example, if the host has four hyperthreaded
 *      physical cpus with 2 logical cpus apiece, this function returns 8.  
 *
 *      This function returns the number of cpus that the host presents to
 *      applications, which is what we want in the vast majority of cases.  We
 *      would only ever care about the number of physical cpus for licensing
 *      purposes.
 *
 * Results:
 *      The number of CPUs (> 0) as presented to us by the host on success
 *      0xFFFFFFFF (-1) on failure
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

uint32
Hostinfo_NumCPUs(void)
{
   static int32 count = 0;
   SYSTEM_INFO sysInfo;

   if (count <= 0) {
      GetSystemInfo(&sysInfo);
      count = sysInfo.dwNumberOfProcessors;
   }

   if (count <= 0) {
      return -1;
   }

   return count;
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_OSIsSMP --
 *
 *      Host OS SMP capability. 
 *      XXX
 *      This functionality is slightly different than on the linux side.
 *      The linux code returns true if the kernel is SMP-enabled, regardless
 *      of the number of processors on the machine. Finding this out on the
 *      Windows side is somewhat difficult, and there are no known bugs
 *      associated with running an SMP kernel on a UP host under Windows.
 *	Therefore we just punt and return true if this is an SMP machine.
 *
 * Results:
 *      TRUE if the number of processors is > 1.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
Hostinfo_OSIsSMP(void)
{
   return Hostinfo_NumCPUs() > 1;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_NameGet --
 *
 *      Return the fully qualified host name of the host.
 *      Thread-safe. --hpreg
 *
 * Results:
 *      The (memorized) name on success
 *      NULL on failure
 *
 * Side effects:
 *      A host name resolution can occur.
 *
 *-----------------------------------------------------------------------------
 */

Unicode
Hostinfo_NameGet(void)
{
   Unicode result;

   static Atomic_Ptr state; /* Implicitly initialized to NULL. --hpreg */

   result = Atomic_ReadPtr(&state);

   if (UNLIKELY(result == NULL)) {
      Unicode before;

      result = Hostinfo_HostName();

      before = Atomic_ReadIfEqualWritePtr(&state, NULL, result);

      if (before) {
         Unicode_Free(result);

         result = before;
      }
   }

   return result;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_GetUser --
 *
 *      Return current user name, or NULL if can't tell.
 *
 * Results:
 *      User name.
 *
 * Side effects:
 *	No.
 *
 *-----------------------------------------------------------------------------
 */

Unicode
Hostinfo_GetUser(void)
{
   utf16_t name[UNLEN + 1];
   DWORD len = ARRAYSIZE(name);
   Unicode uname = NULL;

   if (GetUserNameW(name, &len)) {
      uname = Unicode_AllocWithUTF16(name);
   } 

   return uname;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_GetLoadAverage --
 *
 *      Returns system average load * 100.
 *
 * Results:
 *      FALSE (not implemented)
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Hostinfo_GetLoadAverage(uint32 *l)
{
   /* Not implemented. */
   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_LogLoadAverage --
 *
 *      Logs system average load.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *-----------------------------------------------------------------------------
 */

void
Hostinfo_LogLoadAverage(void)
{
   /* Not implemented. */
}


/*
 *----------------------------------------------------------------------
 *
 *  Hostinfo_TouchBackDoor --
 *
 *      Access the backdoor. This is used to determine if we are 
 *      running in a VM or on a physical host. On a physical host
 *      this should generate a GP which we catch and thereby determine
 *      that we are not in a VM. However some OSes do not handle the
 *      GP correctly and the process continues running returning garbage.
 *      In this case we check the EBX register which should be 
 *      BDOOR_MAGIC if the IN was handled in a VM. Based on this we
 *      return either TRUE or FALSE.
 *
 * Results:
 *      TRUE if we succesfully accessed the backdoor, FALSE or segfault
 *      if not.
 *
 * Side effects:
 *	Exception if not in a VM.
 *
 *----------------------------------------------------------------------
 */

Bool
Hostinfo_TouchBackDoor(void)
{
   uint32 ebxval;

#if defined(_WIN64)
   Backdoor_proto bp;

   bp.in.ax.quad = BDOOR_MAGIC;
   bp.in.size = ~BDOOR_MAGIC;
   bp.in.cx.quad = BDOOR_CMD_GETVERSION;
   bp.in.dx.quad = BDOOR_PORT;

   Hostinfo_BackdoorInOut(&bp);

   ebxval = bp.out.bx.words.low;
#else // _WIN64
   _asm {	  
         push edx
	 push ecx
	 push ebx
	 mov ecx, BDOOR_CMD_GETVERSION
         mov ebx, ~BDOOR_MAGIC
         mov eax, BDOOR_MAGIC
         mov dx, BDOOR_PORT
         in eax, dx
	 mov ebxval, ebx
	 pop ebx
	 pop ecx
         pop edx
   }
#endif // _WIN64

   return (ebxval == BDOOR_MAGIC) ? TRUE : FALSE;
}

 
/*
 *----------------------------------------------------------------------
 *
 *  Hostinfo_NestingSupported --
 *
 *      Access the backdoor with a nesting control query. This is used
 *      to determine if we are running in a VM that supports nesting.
 *      This function should only be called after determining that the
 *	backdoor is present with Hostinfo_TouchBackdoor().
 *
 * Results:
 *      TRUE if the outer VM supports nesting.
 *      FALSE otherwise.
 *
 * Side effects:
 *      Exception if not in a VM, so don't do that!
 *
 *----------------------------------------------------------------------
 */

Bool
Hostinfo_NestingSupported(void)
{
   uint32 cmd = NESTING_CONTROL_QUERY << 16 | BDOOR_CMD_NESTING_CONTROL;
   uint32 result;

#if defined(_WIN64)
   Backdoor_proto bp;

   bp.in.ax.quad = BDOOR_MAGIC;
   bp.in.cx.quad = cmd;
   bp.in.dx.quad = BDOOR_PORT;

   Hostinfo_BackdoorInOut(&bp);

   result = bp.out.ax.words.low;
#else
   _asm {
         push edx
         push ecx
         mov ecx, cmd
         mov eax, BDOOR_MAGIC
         mov dx, BDOOR_PORT
         in eax, dx
         mov result, eax
         pop ecx
         pop edx
   }
#endif

   if (result >= NESTING_CONTROL_QUERY && result != ~0U) {
      return TRUE;
   }
   return FALSE;
}

void
Hostinfo_LogMemUsage(void)
{
   ;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_OSIsWow64 --
 *
 *      Determine whether we are running on Wow64 (Windows-on-Windows 64,
 *      the execution environment for a 32-bit process on a 64-bit version
 *      of Windows).
 *
 * Results:
 *      TRUE if Wow64, FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Hostinfo_OSIsWow64(void)
{
   HMODULE kernel32 = Win32U_GetModuleHandle("kernel32.dll");

   if (kernel32) {
      BOOL isWow64;

      IsWow64ProcessFn pfnIsWow64Process =
         (IsWow64ProcessFn) GetProcAddress(kernel32, "IsWow64Process");

      if (pfnIsWow64Process && pfnIsWow64Process(GetCurrentProcess(),
          &isWow64)) {
         if (isWow64) {
            return TRUE;
         }
      }
   }

   return FALSE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_GetAllCpuid --
 *
 *      Collect CPUID information on all logical CPUs.
 *
 *      XXX Consolidate this code with
 *          vmx/main/cpucountWin32.c::CpucountGetAllCPUIDHost()
 *
 *      'query->numLogicalCPUs' is the size of the 'query->logicalCPUs' output
 *      array.
 *
 * Results:
 *      On success: TRUE. 'query->logicalCPUs' is filled and
 *                  'query->numLogicalCPUs' is adjusted accordingly.
 *      On failure: FALSE. Happens if 'query->numLogicalCPUs' was too small.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool
Hostinfo_GetAllCpuid(CPUIDQuery *query)  // IN/OUT:
{
   HANDLE curThread;
   HANDLE curProcess;
   DWORD_PTR processAffinity;
   DWORD_PTR systemAffinity;

   /*
    * Win95 doesn't export SetProcessAffinityMask().
    * XXX Who cares? It seems the tools do not use this function.
    */

   BOOL (WINAPI *SetProcessAffinityMaskPtr)(HANDLE, DWORD_PTR);
   DWORD_PTR affinityMask;
   DWORD_PTR origThreadAffinity;
   uint32 numLogicalCPUs = 0;
   Bool ret = FALSE;

   ASSERT(query);

   curThread = GetCurrentThread();
   curProcess = GetCurrentProcess();

   if (!GetProcessAffinityMask(curProcess, &processAffinity, 
                               &systemAffinity)) {
      Warning("%s: GetProcessAffinityMask failed: %s\n",
              __FUNCTION__, Err_ErrString());

      return FALSE;
   }
   /* This code must run on a processor. */
   ASSERT(processAffinity != 0);
   /* The process affinity must be a subset of the system affinity. */
   ASSERT((processAffinity & ~systemAffinity) == 0);

   /*
    * The user might not have rights for SetProcessAffinityMask(). 
    * On the other hand SetThreadAffinityMask() works only for
    * CPUs that are in processAffinity.
    */

   if (processAffinity != systemAffinity) {
      SetProcessAffinityMaskPtr = (BOOL (WINAPI *)(HANDLE, DWORD_PTR))
         GetProcAddress(Win32U_GetModuleHandle("kernel32.lib"),
	                "SetProcessAffinityMask");
      if (SetProcessAffinityMaskPtr == NULL) { // It is very unlikely.
	 return FALSE;
      }
      if (!SetProcessAffinityMaskPtr(curProcess, systemAffinity)) {
         Warning("%s: Could not set process affinity from %p to %p: %s\n", 
                  __FUNCTION__, processAffinity, systemAffinity,
                 Err_ErrString());

	 return FALSE;
      }
   }

   /*
    * For each processor, pin ourselves to the processor, execute CPUID with
    * eax/ecx set as specified, and store the resulting GPR values.
    */

   origThreadAffinity = 0;

   for (affinityMask = 1;
           affinityMask // Number of bits in a DWORD_PTR exceeded.
        && affinityMask <= systemAffinity;
        affinityMask <<= 1) {
      DWORD_PTR previousThreadAffinity;

      /*
       * Pin ourselves to all processors, not just the ones that were
       * in our affinity mask.  If we were to only count the
       * processors in the current affinity mask, users could easily
       * trick us into seeing too few processors by setting the
       * process affinity in the task manager.
       */

      if (!(affinityMask & systemAffinity)) {
         /* This processor does not exist in the system. */
	 continue;
      }

      previousThreadAffinity = SetThreadAffinityMask(curThread, affinityMask);
      if (previousThreadAffinity == 0) {
 	 Warning("%s: Could not set processor affinity to %p: %s\n", 
                 __FUNCTION__, affinityMask, Err_ErrString());
	 goto end;
      }
      /* There is no GetThreadAffinityMask(). */
      if (numLogicalCPUs == 0) {
	 origThreadAffinity = previousThreadAffinity;
      }

      if (numLogicalCPUs >= query->numLogicalCPUs) {
         Warning("Output array is too small.\n");
         goto end;
      }

      ASSERT_ON_COMPILE(   sizeof affinityMask
                        <= sizeof query->logicalCPUs[numLogicalCPUs].tag);
      query->logicalCPUs[numLogicalCPUs].tag = affinityMask;
      __GET_CPUID2(query->eax, query->ecx,
                   &query->logicalCPUs[numLogicalCPUs].regs);
      numLogicalCPUs++;
   }

   ASSERT(numLogicalCPUs <= query->numLogicalCPUs);
   query->numLogicalCPUs = numLogicalCPUs;
   ret = TRUE;
   
end:
   /* 
    * Restore the original process and thread affinity. There is a race here 
    * if someone changed the process affinity some time between now and 
    * when we called GetProcessAffinityMask().
    */

   if (processAffinity != systemAffinity) {
      if (!SetProcessAffinityMaskPtr(curProcess, processAffinity)) {
	 Warning("%s: Could not restore process affinity to %p\n",
                 __FUNCTION__, processAffinity);
      }
   }
   if (origThreadAffinity != 0) {
      if (!SetThreadAffinityMask(curThread, origThreadAffinity)) {
	 Warning("%s: Could not restore thread affinity to %p\n", 
	         __FUNCTION__, origThreadAffinity);
      }
   }

   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * HostinfoGetSystemTimes --
 *
 *      Return system uptime and total idle time for all CPUs in microseconds.
 *
 *      NOTE! This routine uses the GetSystemTimes() function which is present
 *      in Windows XP SP1 and later. Zero will be returned for all earlier
 *      systems.
 *
 *      XXX This routine should use the registry perfmon APIs instead. See
 *          bug #42567 for details.
 *
 * Results:
 *      The system uptime and total idle time for all CPUs in microseconds or
 *      zeroes in case of a failure.
 *
 * Side effects:
 *	Can output warnings.
 *
 *-----------------------------------------------------------------------------
 */

static void
HostinfoGetSystemTimes(uint64 *upTime,	  // OUT: system uptime in microseconds
		       uint64 *idleTime)  // OUT: total idle time for all CPUs
{
   typedef BOOL (WINAPI *GetSystemTimesPtr_t) (uint64 *, uint64 *, uint64 *);
   static GetSystemTimesPtr_t GetSystemTimesPtr;
   static BOOL initialized = FALSE;
   uint64 idle = 0, kernel = 0 , user = 0;
   DWORD status;

   /* 
    * The initilization is idempotent so we don't need to worry
    * too much about synchronization.
    */

   if (!initialized) { 
      initialized = TRUE;
      /* GetSystemTimes() requires XP SP1 or above. */
      GetSystemTimesPtr = (GetSystemTimesPtr_t)
	 GetProcAddress(Win32U_GetModuleHandle("kernel32"), "GetSystemTimes");
   }

   if (GetSystemTimesPtr != NULL) {
      ASSERT_ON_COMPILE(sizeof(FILETIME) == sizeof (uint64));

      status = GetSystemTimesPtr(&idle, &kernel, &user);
      if (status == 0) {
	 status = GetLastError();
	 idle = kernel = user = 0;
         Warning("%s: failed to get system times: %d.\n", __FUNCTION__,
                 status);
      }
   }
   if (upTime != NULL) {
      /* The kernel time reported by GetSystemTimes includes idle time */
      *upTime = (kernel + user) / Hostinfo_NumCPUs() / 10;
   }
   if (idleTime != NULL) {
      *idleTime = idle / 10;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_SystemIdleTime --
 *
 *      Return total idle time for all CPUs in microseconds
 *
 *      NOTE! This routine uses the GetSystemTimes() function which is present
 *      in Windows XP SP1 and later. Zero will be returned for all earlier
 *      systems.
 *
 * Results:
 *      The total system idle time for all CPUs in microseconds or zero in
 *      case of a failure.
 *
 * Side effects:
 *	Can output warnings.
 *
 *-----------------------------------------------------------------------------
 */

uint64
Hostinfo_SystemIdleTime(void)
{
   uint64 idleTime;

   HostinfoGetSystemTimes(NULL, &idleTime);

   return idleTime;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_SystemUpTime --
 *
 *      Return the system uptime in microseconds.
 *
 *      Please note that the actual resolution of this "clock" is undefined -
 *      it varies between OSen and OS versions. Use Hostinfo_SystemTimerUS
 *      whenever possible.
 *
 *      NOTE! This routine uses the GetSystemTimes() function which is present
 *      in Windows XP SP1 and later. Zero will be returned for all earlier
 *      systems.
 *
 * Results:
 *      The system uptime in microseconds or zero in case of a failure.
 *
 * Side effects:
 *      Can output warnings.
 *
 *-----------------------------------------------------------------------------
 */

VmTimeType
Hostinfo_SystemUpTime(void)
{
   uint64 upTime;

   HostinfoGetSystemTimes(&upTime, NULL);

   return upTime;
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_GetCpuDescription --
 *
 *      Get the descriptive name associted with a given CPU. 
 *
 * Results:
 *      TRUE on success, FALSE on failure
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Hostinfo_GetCpuDescription(uint32 cpuNumber)  // IN:
{
   char val[256];
   char *s, *e;
   HKEY currentKey;
   LONG result;
   DWORD type = REG_SZ;
   DWORD identifierSize;
   char szSubKey[100];
   const char szTemplate[] = "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%d";

   Str_Snprintf(szSubKey, sizeof(szSubKey), szTemplate, cpuNumber);
   result = Win32U_RegOpenKeyEx(HKEY_LOCAL_MACHINE, szSubKey, 0, KEY_READ,
			 &currentKey);
   if (result != ERROR_SUCCESS) {
      Warning("%s: Failed to RegOpenKeyEx(),  %s\n", __FUNCTION__,
              Err_Errno2String(result));

      return NULL;
   }
   
   identifierSize = sizeof val;
   result = Win32U_RegQueryValueEx(currentKey, "ProcessorNameString", NULL,
                            &type, val, &identifierSize);

   RegCloseKey(currentKey);
   
   if (result != ERROR_SUCCESS ||
       !(identifierSize > 0 && identifierSize <= sizeof val)) {
      Warning("%s: Failed to RegQueryValueEx(), %s\n", __FUNCTION__,
              Err_Errno2String(result));

      return NULL;
   }
   val[identifierSize - 1] = '\0';

   /* Skip leading and trailing while spaces */
   s = val;
   e = s + strlen(s);
   for (; s < e && CType_IsSpace(*s); s++);
   for (; s < e && CType_IsSpace(e[-1]); e--);
   *e = 0;

   return Util_SafeStrdup(s);
}


/*
 *----------------------------------------------------------------------------
 *
 * Hostinfo_GetModulePath --
 *
 *      Get the full path for the executable that is calling this function.
 *
 * Results:  
 *      The full path or NULL on failure.
 *
 * Side effects:
 *      Memory is allocated.
 * 
 *----------------------------------------------------------------------------
 */

Unicode
Hostinfo_GetModulePath(uint32 priv)  // IN:
{
   if ((priv != HGMP_PRIVILEGE) && (priv != HGMP_NO_PRIVILEGE)) {
      Warning("%s: invalid privilege parameter\n", __FUNCTION__);

      return NULL;
   }

   return Win32U_GetModuleFileName(NULL);
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_GetMhzOfProcessor --
 *
 *      Reports the 1) current Mhz of processor, and/or 2) fastest possible Mhz
 *      of the processor.  Caller can pass -1 for processNumber to obtain the
 *      max of #1 and/or #2 across all processors.  If the caller is only
 *      interested in #2 then it's recommended the caller cache the result.
 *
 * Results:
 *      TRUE on success, otherwise FALSE.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Bool 
Hostinfo_GetMhzOfProcessor(int32 processorNumber, // IN: number of processor
                           uint32 *currentMhz,    // OUT: current MHz of CPU (or CPUs)
                           uint32 *maxMhz)        // OUT: maximum MHz of CPU (or CPUs)
{
#ifndef NTSTATUS
#define NTSTATUS LONG
#endif

#define MAX_PROCS 64

   typedef struct _PROCESSOR_POWER_INFORMATION {
     ULONG Number;
     ULONG MaxMhz;
     ULONG CurrentMhz;
     ULONG MhzLimit;
     ULONG MaxIdleState;
     ULONG CurrentIdleState;
   } PROCESSOR_POWER_INFORMATION, *PPROCESSOR_POWER_INFORMATION;

   typedef NTSTATUS (WINAPI *PowerInformation)(POWER_INFORMATION_LEVEL, PVOID, 
					       ULONG, PVOID, ULONG);

   HMODULE powerProfDll;
   PowerInformation PowerInfoProc = NULL;

   PROCESSOR_POWER_INFORMATION ppi[MAX_PROCS];
   NTSTATUS status;
   unsigned int i;
   uint32 maxSpeedAcrossAll = 0;
   uint32 currentSpeedAcrossAll = 0;

   ASSERT(currentMhz != NULL || maxMhz != NULL);

   powerProfDll = Win32U_LoadLibrary("powrprof.dll");
   if (powerProfDll == NULL) {
      Warning("%s: Failed to load powrprof.dll\n", __FUNCTION__);

      return FALSE;
   }
   PowerInfoProc = (PowerInformation) GetProcAddress(powerProfDll,
                                                     "CallNtPowerInformation");
   if (PowerInfoProc == NULL) {
      Warning("%s: Failed to load power status function\n", __FUNCTION__);
      FreeLibrary(powerProfDll);

      return FALSE;
   }

   /* 
    * Put known invalid values into struct so we can tell how many CPUs are
    * in the system.  An alternative is to call Hostinfo_NumCPUs and hope
    * that the result produces a result consistent with
    * CallNtPowerInformation().
    */

   for (i = 0; i < MAX_PROCS; ++i) {
      ppi[i].Number = ~0;
   }

   status = PowerInfoProc(ProcessorInformation, NULL, 0, ppi, sizeof ppi);
   FreeLibrary(powerProfDll);
   if (status != ERROR_SUCCESS /* STATUS_SUCCESS is more accurate */) {
      Warning("%s: Failed to query processor speed: %d\n",
              __FUNCTION__, status);

      return FALSE;
   }

   for (i = 0; i < MAX_PROCS; ++i) {
      if (ppi[i].Number == ~0) {
         break;
      }

      /* If caller has requested a specific processor. */
      if (processorNumber >= 0) {
	 if (ppi[i].Number != processorNumber) {
	    continue;
	 }
	 if (currentMhz != NULL) {
	    *currentMhz = ppi[i].CurrentMhz;
	 }
	 if (maxMhz != NULL) {
	    *maxMhz = ppi[i].MaxMhz;
	 }

	 return TRUE;
      } 

      if (ppi[i].CurrentMhz > currentSpeedAcrossAll) {
	 currentSpeedAcrossAll = ppi[i].CurrentMhz;
      }
      if (ppi[i].MaxMhz > maxSpeedAcrossAll) {
	 maxSpeedAcrossAll = ppi[i].MaxMhz;
      }
   }

   /* We didn't find the requested processor number. */
   if (processorNumber >= 0) {
      return FALSE;
   }

   if (currentMhz != NULL) {
      *currentMhz = currentSpeedAcrossAll;
   }
   if (maxMhz != NULL) {
      *maxMhz = maxSpeedAcrossAll;
   }

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_GetRatedCpuMhz --
 *
 *      Get the rated CPU speed of a given processor. 
 *      Return value is in MHz.
 *
 * Results:
 *      TRUE on success, FALSE on failure
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
Hostinfo_GetRatedCpuMhz(int32 cpuNumber,  // IN:
                        uint32 *mHz)      // OUT:
{
   return Hostinfo_GetMhzOfProcessor(cpuNumber, NULL, mHz);
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_GetMemoryInfoInPages
 *
 *      Obtain the minimum memory to be maintained, total memory available,
 *      and free memory available on the host in pages.
 *
 * Results:
 *	TRUE on success.
 *      The required info in out parameters.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Bool
Hostinfo_GetMemoryInfoInPages(unsigned int *minSize,
			      unsigned int *maxSize,
			      unsigned int *currentSize)
{
   HMODULE libKernel;
   typedef BOOL (WINAPI *PFN_MEMORYSTATUSEX)(MEMORYSTATUSEX *);
   PFN_MEMORYSTATUSEX pGetMemoryStatusEx;

   libKernel = Win32U_GetModuleHandle("kernel32.dll");
   ASSERT_NOT_IMPLEMENTED(libKernel);

   pGetMemoryStatusEx = (PFN_MEMORYSTATUSEX) GetProcAddress(libKernel, 
                                             "GlobalMemoryStatusEx");

   if (pGetMemoryStatusEx) {
      MEMORYSTATUSEX memoryStatusEx;
      DWORDLONG value;

      memset(&memoryStatusEx, 0, sizeof(MEMORYSTATUSEX));
      memoryStatusEx.dwLength = sizeof(MEMORYSTATUSEX);
      
      pGetMemoryStatusEx(&memoryStatusEx);

      value = memoryStatusEx.ullTotalPhys / PAGE_SIZE;
      *maxSize = value;
      value = memoryStatusEx.ullAvailPhys / PAGE_SIZE;
      *currentSize = value;
   } else {
      MEMORYSTATUS memoryStatus;

      memset(&memoryStatus, 0, sizeof(MEMORYSTATUS));
      memoryStatus.dwLength = sizeof(MEMORYSTATUS);

      GlobalMemoryStatus(&memoryStatus);

      *maxSize = memoryStatus.dwTotalPhys / PAGE_SIZE;
      *currentSize = memoryStatus.dwAvailPhys / PAGE_SIZE;
   }

   *minSize = 0;

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * Hostinfo_GetPCFrequency --
 *
 *      Get the Windows performance counter frequency.
 *
 * Results:
 *      TRUE on success.
 *
 * Side effects:
 *      Yes.
 *
 *-----------------------------------------------------------------------------
 */

Bool
Hostinfo_GetPCFrequency(uint64 *pcHz)	// OUT
{
   if (hostinfoNoPC) {
      return FALSE;
   }
   if (!hostinfoHasPC && !HostinfoPCInit()) {
      return FALSE;
   }

   ASSERT(hostinfoPCHz != 0);
   *pcHz = hostinfoPCHz;

   return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * HostinfoGetCriticalSectionPtr --
 *
 *      Get the pointer to the CRITICAL_SECTION protecting the time
 *      calculations.
 *
 * Results:
 *      The CRITICAL_SECTION pointer.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static CRITICAL_SECTION *
HostinfoGetCriticalSectionPtr(void)
{
   CRITICAL_SECTION *criticalSection;

   criticalSection = Atomic_ReadPtr(&hostinfoCSMemory);

   if (criticalSection == NULL) {
      CRITICAL_SECTION *local;

      local = (CRITICAL_SECTION *) Util_SafeMalloc(sizeof(CRITICAL_SECTION));

      if (InitializeCriticalSectionAndSpinCount(local, 0x80000400) == 0) {
         /* hopefully another thread succeeded in an initialization */
         Warning("%s: InitializeCriticalSectionAndSpinCount failure!\n",
                 __FUNCTION__);
         free(local);

         criticalSection = NULL;
      } else {
         if (Atomic_ReadIfEqualWritePtr(&hostinfoCSMemory, NULL, local)) {
            DeleteCriticalSection(local);
            free(local);
         }

         criticalSection = Atomic_ReadPtr(&hostinfoCSMemory);
         ASSERT_NOT_IMPLEMENTED(criticalSection != NULL);
      }
   }

   return criticalSection;
}


/*
 *-----------------------------------------------------------------------------
 *
 * HostinfoPCInit --
 *
 *      One-time initialization of Windows performance counter
 *      information. Thread-safe because we take a lock around
 *      interaction with static variables.
 *
 * Results:
 *      TRUE on success.
 *
 * Side effects:
 *      Yes.
 *
 *-----------------------------------------------------------------------------
 */

Bool
HostinfoPCInit(void)
{
   LARGE_INTEGER pc;
   LARGE_INTEGER freq;
   CRITICAL_SECTION *criticalSection;

   if (hostinfoNoPC) {
      return FALSE;
   }
   if (hostinfoHasPC) {
      return TRUE;
   }

   /*
    * Figure out whether we can use the performance counter.
    */

   if (!QueryPerformanceFrequency(&freq)) {
      Warning("%s: Unable to get Windows performance counter frequency: %d\n",
	      __FUNCTION__, GetLastError());
      hostinfoNoPC = TRUE;

      return FALSE;
   }

   if (!QueryPerformanceCounter(&pc)) {
      Warning("%s: Unable to get Windows performance counter: %d\n",
	       __FUNCTION__, GetLastError());
      hostinfoNoPC = TRUE;

      return FALSE;
   }

   criticalSection = HostinfoGetCriticalSectionPtr();
   EnterCriticalSection(criticalSection);

   /*
    * Compute parameters to convert performance counter to microsecond time.
    */
   
   if (!RateConv_ComputeParams(freq.QuadPart, pc.QuadPart, 1000000, 0,
			       &hostinfoPCToUS)) {
      LeaveCriticalSection(criticalSection);

      Warning("%s: Bad Windows performance counter frequency: %"FMT64"d\n",
	      __FUNCTION__, freq.QuadPart);
      hostinfoNoPC = TRUE;

      return FALSE;
   }

   RateConv_LogParams("HOSTINFO", freq.QuadPart, pc.QuadPart, 1000000, 0,
		      &hostinfoPCToUS);

   hostinfoPCHz = freq.QuadPart;
   LeaveCriticalSection(criticalSection);

   hostinfoHasPC = TRUE;

   return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * HostinfoEnforceMonotonicity --
 *
 *      This is a helper function for Hostinfo_SystemTimerUS() to ensure
 *      that the result of the rate conversion is monotonic.  If this
 *      would be violated, change the result to be lastResult.  If the
 *      time source went back by more than 'tolerance', then adjust the
 *      'add' portion of the rate conversion.
 *
 *      Callers must have taken a lock that will protect 'lastResult' and
 *      'params'.
 *
 *      We make Hostinfo_SystemTimerUS monotonic, rather than strictly
 *      increasing because back to back calls to Hostinfo_SystemTimerUS can
 *      happen in the span of a microsecond.
 *
 * Results:
 *      The corrected result.
 *
 * Side effects:
 *      'lastResult' is updated and 'params' might be updated.
 *
 *----------------------------------------------------------------------
 */

static INLINE VmTimeType
HostinfoEnforceMonotonicity(VmTimeType result,        // IN:
                            VmTimeType *lastResult,   // IN/OUT:
                            RateConv_Params *params,  // IN/OUT:
                            uint64 tolerance)         // IN:
{
   if (result < *lastResult) {
      /* If time went backwards significantly, update the RateConv. */
      if (result < *lastResult - tolerance) {
         params->add += *lastResult - result;
      }
      result = *lastResult;
   }
   *lastResult = result;

   return result;
}


/*
 *----------------------------------------------------------------------
 *
 * HostinfoRawTimer
 *
 *      Read the raw value of the timer performance counter.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static VmTimeType 
HostinfoRawTimer(void)
{
   LARGE_INTEGER pc;
   Bool status;

   status = QueryPerformanceCounter(&pc);
   ASSERT_NOT_IMPLEMENTED(status);

   return pc.QuadPart;
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_RawSystemTimerUS --
 *
 *      Read the raw value of the timer performance counter.
 *      The timer is implemented via a performance counter.
 *      There is no protection from this value going backwards.
 *
 * Results:
 *      Relative time in microseconds or zero if a failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

VmTimeType 
Hostinfo_RawSystemTimerUS(void)
{
   if (!hostinfoHasPC && !HostinfoPCInit()) {
      NOT_IMPLEMENTED();
   }

   ASSERT(hostinfoHasPC);

   return RateConv_Unsigned(&hostinfoPCToUS, HostinfoRawTimer());
}


/*
 *----------------------------------------------------------------------
 *
 * Hostinfo_SystemTimerUS --
 *
 *      This is the routine to use when performing timing measurements. It
 *      is valid (finish-time - start-time) only within a single process.
 *      Don't send a time obtained this way to another process and expect
 *      a relative time measurement to be correct.
 *
 *      The timer is implemented via a performance counter.
 *
 * Results:
 *      Relative time in microseconds or zero if a failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

VmTimeType 
Hostinfo_SystemTimerUS(void)
{
   uint64 timerValue;
   VmTimeType result;
   static VmTimeType lastResult = 1; // zero indicates a failure, PR287297. 

   /* Variables for stress options. */
   static uint64 timerValueStressOffset;
   static unsigned count;
   static VmTimeType stressResult;
   uint64 timerValueOriginal;
   CRITICAL_SECTION *criticalSection;

   if (!hostinfoHasPC && !HostinfoPCInit()) {
      NOT_IMPLEMENTED();
   }

   ASSERT(hostinfoHasPC);

   criticalSection = HostinfoGetCriticalSectionPtr();
   EnterCriticalSection(criticalSection);

   timerValue = HostinfoRawTimer();

   if (vmx86_debug && hostinfoStressReset) {
      timerValueOriginal = timerValue;
      timerValue -= timerValueStressOffset;
   }

   result = RateConv_Unsigned(&hostinfoPCToUS, timerValue);
   result = HostinfoEnforceMonotonicity(result, &lastResult, 
                                        &hostinfoPCToUS, hostinfoPCHz);

   if (vmx86_debug && hostinfoStressReset) {
      count++;
      if (timerValue > hostinfoPCHz) {
         timerValueStressOffset = timerValueOriginal;
         count = 0;
      }
   }

   LeaveCriticalSection(criticalSection);

   if (vmx86_debug && hostinfoStressReset && count < 2) {
      Log("%s reset PC %"FMT64"d %"FMT64"d %"FMT64"d\n", __FUNCTION__,
          result, timerValue, timerValueOriginal);
   }

   if (vmx86_debug && hostInfoStressRound) {
      /* Round down to the nearest 5 seconds. */
      result = result - (result % 5000000);
   }

   ASSERT(result >= 0);

   return result;
}


#endif // __MINGW32__
