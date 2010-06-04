/*********************************************************
 * Copyright (C) 2010 VMware, Inc. All rights reserved.
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

#ifndef WINSIDTYPES_H
#define WINSIDTYPES_H


typedef enum  {
  WinNullSid                                    = 0,
  WinWorldSid                                   = 1,
  WinLocalSid                                   = 2,
  WinCreatorOwnerSid                            = 3,
  WinCreatorGroupSid                            = 4,
  WinCreatorOwnerServerSid                      = 5,
  WinCreatorGroupServerSid                      = 6,
  WinNtAuthoritySid                             = 7,
  WinDialupSid                                  = 8,
  WinNetworkSid                                 = 9,
  WinBatchSid                                   = 10,
  WinInteractiveSid                             = 11,
  WinServiceSid                                 = 12,
  WinAnonymousSid                               = 13,
  WinProxySid                                   = 14,
  WinEnterpriseControllersSid                   = 15,
  WinSelfSid                                    = 16,
  WinAuthenticatedUserSid                       = 17,
  WinRestrictedCodeSid                          = 18,
  WinTerminalServerSid                          = 19,
  WinRemoteLogonIdSid                           = 20,
  WinLogonIdsSid                                = 21,
  WinLocalSystemSid                             = 22,
  WinLocalServiceSid                            = 23,
  WinNetworkServiceSid                          = 24,
  WinBuiltinDomainSid                           = 25,
  WinBuiltinAdministratorsSid                   = 26,
  WinBuiltinUsersSid                            = 27,
  WinBuiltinGuestsSid                           = 28,
  WinBuiltinPowerUsersSid                       = 29,
  WinBuiltinAccountOperatorsSid                 = 30,
  WinBuiltinSystemOperatorsSid                  = 31,
  WinBuiltinPrintOperatorsSid                   = 32,
  WinBuiltinBackupOperatorsSid                  = 33,
  WinBuiltinReplicatorSid                       = 34,
  WinBuiltinPreWindows2000CompatibleAccessSid   = 35,
  WinBuiltinRemoteDesktopUsersSid               = 36,
  WinBuiltinNetworkConfigurationOperatorsSid    = 37,
  WinAccountAdministratorSid                    = 38,
  WinAccountGuestSid                            = 39,
  WinAccountKrbtgtSid                           = 40,
  WinAccountDomainAdminsSid                     = 41,
  WinAccountDomainUsersSid                      = 42,
  WinAccountDomainGuestsSid                     = 43,
  WinAccountComputersSid                        = 44,
  WinAccountControllersSid                      = 45,
  WinAccountCertAdminsSid                       = 46,
  WinAccountSchemaAdminsSid                     = 47,
  WinAccountEnterpriseAdminsSid                 = 48,
  WinAccountPolicyAdminsSid                     = 49,
  WinAccountRasAndIasServersSid                 = 50,
  WinNTLMAuthenticationSid                      = 51,
  WinDigestAuthenticationSid                    = 52,
  WinSChannelAuthenticationSid                  = 53,
  WinThisOrganizationSid                        = 54,
  WinOtherOrganizationSid                       = 55,
  WinBuiltinIncomingForestTrustBuildersSid      = 56,
  WinBuiltinPerfMonitoringUsersSid              = 57,
  WinBuiltinPerfLoggingUsersSid                 = 58,
  WinBuiltinAuthorizationAccessSid              = 59,
  WinBuiltinTerminalServerLicenseServersSid     = 60,
  WinBuiltinDCOMUsersSid                        = 61,
  WinBuiltinIUsersSid                           = 62,
  WinIUserSid                                   = 63,
  WinBuiltinCryptoOperatorsSid                  = 64,
  WinUntrustedLabelSid                          = 65,
  WinLowLabelSid                                = 66,
  WinMediumLabelSid                             = 67,
  WinHighLabelSid                               = 68,
  WinSystemLabelSid                             = 69,
  WinWriteRestrictedCodeSid                     = 70,
  WinCreatorOwnerRightsSid                      = 71,
  WinCacheablePrincipalsGroupSid                = 72,
  WinNonCacheablePrincipalsGroupSid             = 73,
  WinEnterpriseReadonlyControllersSid           = 74,
  WinAccountReadonlyControllersSid              = 75,
  WinBuiltinEventLogReadersGroup                = 76,
  WinNewEnterpriseReadonlyControllersSid        = 77,
  WinBuiltinCertSvcDComAccessGroup              = 78
} WELL_KNOWN_SID_TYPE;


#endif // WINSIDTYPES_H
