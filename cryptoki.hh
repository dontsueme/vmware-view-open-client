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
 * cryptoki.hh --
 *
 *      PKCS #11-based smartcard support.
 */


#ifndef CDK_CRYPTOKI_HH
#define CDK_CRYPTOKI_HH


#include <boost/signal.hpp>
#include <gmodule.h>
#include <list>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <pkcs11.h>
#include <set>
#include <vector>


#include "util.hh"


namespace cdk {


class Cryptoki
{
public:
   Cryptoki();
   ~Cryptoki();

   unsigned int LoadModules(const Util::string &dirPath);
   bool LoadModule(const Util::string &filePath);

   bool GetHasSlots();
   bool GetHasTokens();

   bool GetHadEvent();

   std::list<X509 *> GetCertificates(STACK_OF(X509_NAME) *CAs);
   EVP_PKEY *GetPrivateKey(const X509 *cert);

   std::vector<Util::string> GetSlotNames();

   void CloseAllSessions();

   boost::signal2<char *, const Util::string &, const X509 *> requestPin;

   static void FreeCertificates(std::list<X509 *> &certs);

private:
   class Module
   {
   public:
      Module(Cryptoki *cryptoki);
      ~Module();

      bool Load(const Util::string &filePath);
      void GetCertificates(std::list<X509 *> &certs, STACK_OF(X509_NAME) *CAs);
      void GetSlotNames(std::set<Util::string> &slots);

      bool GetHasSlots();
      bool GetHasTokens();

      bool GetHadEvent();

      void CloseAllSessions();

      CK_FUNCTION_LIST GetFunctions() const { return mFuncs; }
      Cryptoki *GetCryptoki() const { return mCryptoki; }

   private:
      std::list<CK_SLOT_ID> GetSlots();

      Cryptoki *mCryptoki;
      CK_FUNCTION_LIST mFuncs;
      Util::string mLabel;
      GModule *mModule;
   };

   class Session
   {
   public:
      Session(Module *module);
      void AddRef() { ++mRefCount; }
      void Release();

      CK_RV Open(CK_SLOT_ID slot);
      void GetCertificates(std::list<X509 *> &certs, STACK_OF(X509_NAME) *CAs);
      EVP_PKEY *GetPrivateKey(const X509 *cert);

   private:
      static const RSA_METHOD *GetRsaMethod();
      static int RsaSign(int type,
                         const unsigned char *m, unsigned int m_length,
                         unsigned char *sigret, unsigned int *siglen,
                         const RSA *rsa);

      static Util::string Id2String(const GByteArray *id);

      ~Session();

      Module *mModule;
      Util::string mLabel;
      CK_SESSION_HANDLE mSession;
      unsigned int mRefCount;
      bool mNeedLogin;
   };

   template <class T>
   class ExData
   {
   public:
      static const GByteArray *GetId(const T *t);
      static void SetId(T *t, const GByteArray *id);

      static CK_OBJECT_HANDLE GetObject(const T *t);
      static void SetObject(T *t, CK_OBJECT_HANDLE obj);

      static Session *GetSession(const T *t);
      static void SetSession(T *, Session *session);

      static void SetClassIdx(int classIdx);
   private:
      static int GetIdIdx();
      static int GetObjectIdx();
      static int GetSessionIdx();

      static int DupId(CRYPTO_EX_DATA *to, CRYPTO_EX_DATA *from, void *from_d,
                       int idx, long argl, void *argp);
      static void FreeId(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
                         int idx, long argl, void *argp);

      static int DupSession(CRYPTO_EX_DATA *to, CRYPTO_EX_DATA *from,
                            void *from_d, int idx, long argl, void *argp);
      static void FreeSession(void *parent, void *ptr, CRYPTO_EX_DATA *ad,
                              int idx, long argl, void *argp);

      static int sClassIdx;
   };

   std::list<Module *> mModules;
};


} // namespace cdk


#endif // CDK_CRYPTOKI_HH
