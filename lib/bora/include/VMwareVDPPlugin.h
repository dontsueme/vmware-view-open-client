/* **********************************************************
 * Copyright (C) 2008 VMware, Inc.
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
 * **********************************************************/

/*
 * VMwareVDPPlugin.h --
 */

#ifndef _VMWAREVDPPLUGIN_H_
#define _VMWAREVDPPLUGIN_H_

/*
 * VDP Connection result
 */
typedef enum VDPConnectionResult
{
    VDPCONNECT_SUCCESS = 0,            // Successful connect/disconnect
    VDPCONNECT_FAILURE,                // Failed to connect or disconnected due to a generic failure
    VDPCONNECT_TIMEOUT,                // Failed to connect or disconnected due to a timeout
    VDPCONNECT_REJECTED,               // Connection rejected
    VDPCONNECT_NETWORK_FAILURE,        // Failed to connect or disconnected due to a network failure
    VDPCONNECT_CONNECTION_LOST,        // Disconnect due to lost connection to server
    VDPCONNECT_SERVER_DISCONNECTED,    // Server initiated generic disconnect
    VDPCONNECT_SERVER_ERROR,           // Server initiated disconnect due to a generic server error
    VDPCONNECT_DISPLAY_NOT_ENOUGH_MEM, // Client's memory is not sufficient for the required display configuration
    VDPCONNECT_RESULT_UNSPECIFIED,     // We dont know the result. We could be disconnected and yet not aware about the reason.
    VDPCONNECT_SERVER_DISCONNECTED_EXPIRED,       // Server initiated disconnect. Session expired due to timeout.
    VDPCONNECT_SERVER_DISCONNECTED_MANUAL_LOGOUT, // User initiated disconnect or logout.
    VDPCONNECT_SERVER_DISCONNECTED_ADMIN_MANUAL,  // Server initiated disconnect. Admin manually disconnected the session from admin ui.
    VDPCONNECT_SERVER_DISCONNECTED_RECONNECT,     // Server initiated disconnect as a precursor to pending reconnect.
    VDPCONNECT_SERVER_SVGA_DRIVER_INCOMPATIBLE,   // Server failed to attach to svgadevtap due to an incompatible version of svga drivers.
    VDPCONNECT_TERA_DISCONNECT_HOST_DRIVER_MANUAL_USER_DISCONNECT,      // Tera 1 host card user disconnect.
    VDPCONNECT_TERA_DISCONNECT_HOST_DRIVER_INCOMPATIBLE_DRIVER_VERSION, // Tera 1 host card driver version mismatch.
    VDPCONNECT_ENCRYPTION_MISMATCH,               // Mismatch in client/server encryption algorithms.
    VDPCONNECT_MAX = 100,                         // Add any new error before this. Marker for our connection state reason boundary value.
} VDPConnectionResult;


#endif /* _VMWAREMKSPLUGIN_H_ */
