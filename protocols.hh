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
 * cdkProtocols.hh --
 *
 *      Utility functions for manipulating remoting protocols.
 */

#ifndef CDK_PROTOCOLS_HH
#define CDK_PROTOCOLS_HH


#include "util.hh"


namespace cdk {


class Protocols
{
public:
   enum ProtocolType {
      UNKNOWN,
      RDP,
      RGS,
      PCOIP,
      LOCALVM
   };

   static Util::string GetName(ProtocolType proto);
   static Util::string GetName(const Util::string &label)
      { return GetName(GetProtocolFromLabel(label)); }

   static Util::string GetLabel(ProtocolType proto);
   static Util::string GetLabel(const Util::string &name);

   static Util::string GetMnemonic(ProtocolType proto);
   static Util::string GetMnemonic(const Util::string &name);

   static ProtocolType GetProtocolFromName(const Util::string &name);
   static ProtocolType GetProtocolFromLabel(const Util::string &label);
};


} // namespace cdk


#endif // CDK_PROTOCOLS_HH
