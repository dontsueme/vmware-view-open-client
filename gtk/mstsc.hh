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
 * mstsc.hh --
 *
 *    Simple command line wrapper for mstsc.
 *
 */

#ifndef MSTSC_HH
#define MSTSC_HH


#include <vector>


#include "brokerXml.hh"
#include "procHelper.hh"


namespace cdk {


class Mstsc
   : virtual public ProcHelper
{
public:
   virtual ~Mstsc() { }
   static inline bool GetIsProtocolAvailable() {
        return GetIsInPath(MstscBinary);
   }
   void Start(const BrokerXml::DesktopConnection &connection,
              const Util::Rect *geometry,
              GdkScreen *screen = NULL);

private:
   static const Util::string MstscBinary;
};


} // namespace cdk


#endif // MSTSC_HH
