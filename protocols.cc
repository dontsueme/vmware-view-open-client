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
 * cdkProtocols.cc --
 *
 *      Implementation of cdk::Protocols.
 */

#include "protocols.hh"


#define PROTOCOL_RDP "RDP"
#define PROTOCOL_RDP_LABEL _("Microsoft RDP")
#define PROTOCOL_RDP_MNEMONIC _("Microsoft _RDP")

#define PROTOCOL_RGS "RGS"
#define PROTOCOL_RGS_LABEL _("HP RGS")
#define PROTOCOL_RGS_MNEMONIC _("HP R_GS")

#define PROTOCOL_PCOIP "PCOIP"
#define PROTOCOL_PCOIP_LABEL _("PCoIP")
#define PROTOCOL_PCOIP_MNEMONIC _("_PCoIP")

#define PROTOCOL_LOCALVM "localvm"
#define PROTOCOL_LOCALVM_LABEL _("Local")
#define PROTOCOL_LOCALVM_MNEMONIC _("_Local")


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Protocols::GetName --
 *
 *      Gets the name of a protocol.
 *
 * Results:
 *      The protocol's name, as used by our XML protocol.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Protocols::GetName(ProtocolType proto) // IN
{
   switch (proto) {
   case UNKNOWN:
      return "UNKNOWN";
   case RDP:
      return PROTOCOL_RDP;
   case RGS:
      return PROTOCOL_RGS;
   case PCOIP:
      return PROTOCOL_PCOIP;
   case LOCALVM:
      return PROTOCOL_LOCALVM;
   default:
      NOT_REACHED();
      return "INVALID";
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Protocols::GetLabel --
 *
 *      Gets a user-readable label for the protocol.
 *
 * Results:
 *      A translated string, usable in a menu item, for a random example.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Protocols::GetLabel(ProtocolType proto) // IN
{
   switch (proto) {
   case UNKNOWN:
      return _("Unknown Protocol");
   case RDP:
      return PROTOCOL_RDP_LABEL;
   case RGS:
      return PROTOCOL_RGS_LABEL;
   case PCOIP:
      return PROTOCOL_PCOIP_LABEL;
   case LOCALVM:
      return PROTOCOL_LOCALVM_LABEL;
   default:
      NOT_REACHED();
      return _("Invalid Protocol");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Protocols::GetLabel --
 *
 *      Get a user-readable label based on a protocol name.
 *
 * Results:
 *      A translated string, possibly indicating the protocol is unknown.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Protocols::GetLabel(const Util::string &name) // IN
{
   ProtocolType proto = GetProtocolFromName(name);
   return proto == UNKNOWN
      ? Util::Format(_("Unknown Protocol (%s)"), name.c_str())
      : GetLabel(proto);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Protocols::GetMnemonic --
 *
 *      Gets a user-readable mnemonic for the protocol.
 *
 * Results:
 *      A translated string, usable in a menu item, for a random example.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Protocols::GetMnemonic(ProtocolType proto) // IN
{
   switch (proto) {
   case UNKNOWN:
      return _("Unknown Protocol");
   case RDP:
      return PROTOCOL_RDP_MNEMONIC;
   case RGS:
      return PROTOCOL_RGS_MNEMONIC;
   case PCOIP:
      return PROTOCOL_PCOIP_MNEMONIC;
   case LOCALVM:
      return PROTOCOL_LOCALVM_MNEMONIC;
   default:
      NOT_REACHED();
      return _("Invalid Protocol");
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Protocols::GetMnemonic --
 *
 *      Get a user-readable mnemonic based on a protocol name.
 *
 * Results:
 *      A translated string, possibly indicating the protocol is unknown.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Protocols::GetMnemonic(const Util::string &name) // IN
{
   ProtocolType proto = GetProtocolFromName(name);
   return proto == UNKNOWN
      ? Util::Format(_("Unknown Protocol (%s)"), name.c_str())
      : GetMnemonic(proto);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Protocols::GetProtocolFromName --
 *
 *      Get the protocol ID given a protocol name.
 *
 * Results:
 *      A protocol.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Protocols::ProtocolType
Protocols::GetProtocolFromName(const Util::string &name) // IN
{
#define CHECK_PROTO(proto)                                        \
   if (Util::Utf8Casecmp (name.c_str(), PROTOCOL_##proto) == 0) { \
      return proto;                                               \
   }

   CHECK_PROTO(RDP);
   CHECK_PROTO(RGS);
   CHECK_PROTO(PCOIP);
   CHECK_PROTO(LOCALVM);

#undef CHECK_PROTO

   return UNKNOWN;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Protocols::GetProtocolFromLabel --
 *
 *      Get the protocol ID given a translated label.
 *
 * Results:
 *      A protocol.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Protocols::ProtocolType
Protocols::GetProtocolFromLabel(const Util::string &label) // IN
{
#define CHECK_PROTO(proto)                      \
   if (label == PROTOCOL_##proto##_LABEL) {     \
      return proto;                             \
   }

   CHECK_PROTO(RDP);
   CHECK_PROTO(RGS);
   CHECK_PROTO(PCOIP);
   CHECK_PROTO(LOCALVM);

#undef CHECK_PROTO

   return UNKNOWN;
}


} // namespace cdk
