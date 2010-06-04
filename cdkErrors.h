/*********************************************************
 * Copyright (C) 2010 VMware, Inc. All rights reserved.
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
 * cdkErrors.h --
 *
 *    Error codes used when view client exits abnormally.
 *    Must be kept in sync with wswc/include/wswc_constants.h
 *    (replace "RETVAL_" prefix with "CDK_ERR_").
 */

#ifndef CDK_ERRORS_H
#define CDK_ERRORS_H


#include <glib.h>


G_BEGIN_DECLS


typedef enum {
   CDK_ERR_KIOSK_FATAL_FAILURE = -1,
   CDK_ERR_SUCCESS,
   CDK_ERR_CONNECT_FAILURE,
   CDK_ERR_LOGON_FAILURE,
   CDK_ERR_DESKOP_START_FAILURE,
   CDK_ERR_RDP_STARTUP_FAILURE,
   CDK_ERR_RDP_LOST_FAILURE,
   CDK_ERR_TUNNEL_LOST_FAILURE,
   CDK_ERR_MVDI_TRANSFER_FAILURE,
   CDK_ERR_MVDI_CHECKIN_FAILURE,
   CDK_ERR_MVDI_CHECKOUT_FAILURE,
   CDK_ERR_MVDI_ROLLBACK_FAILURE,
   CDK_ERR_AUTH_UNKNOWN_RESULT,
   CDK_ERR_AUTH_ERROR,
   CDK_ERR_AUTH_UNKNOWN_METHOD_REQUEST,
   CDK_ERR_INVALID_SERVER_RESPONSE,
   CDK_ERR_DESKTOP_DISCONNECTED,
   CDK_ERR_TUNNEL_DISCONNECTED,
   CDK_ERR_CVP_DOWNLOAD_ERROR,
   CDK_ERR_CVP_UNABLE_TO_CANCEL_DOWNLOAD,
   CDK_ERR_KIOSK_UNSUPPORTED_OP,
   CDK_ERR_RMKS_CONNECTION_ERROR,
   CDK_ERR_PIN_ERROR,
   CDK_ERR_PIN_MISMATCH,
   CDK_ERR_PASSWORD_MISMATCH,
   CDK_ERR_CONNECTION_SERVER_ERROR,
   CDK_ERR_DESKTOP_NOT_AVAILABLE
} CdkError;


G_END_DECLS


#endif // CDK_ERRORS_H
