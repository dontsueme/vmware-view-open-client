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
 * cryptoki.cc --
 *
 *      PKCS #11-based smartcard support.
 */


#include <dirent.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>


#include "cryptoki.hh"


/*
 * d2i_X509() changed signature between 0.9.7 and 0.9.8, which we use
 * on OS X 10.5.
 */
#if OPENSSL_VERSION_NUMBER < 0x00908000L
#define D2I_X509_CONST
#else
#define D2I_X509_CONST const
#endif


namespace cdk {


template<class T> int Cryptoki::ExData<T>::sClassIdx = -1;


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Cryptoki --
 *
 *      Cryptoki constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Cryptoki::Cryptoki()
{
   /*
    * These need to be initialized somewhere, may as well be here.
    * Additional invocations are ignored, anyway.
    */
   ExData<RSA>::SetClassIdx(CRYPTO_EX_INDEX_RSA);
   ExData<X509>::SetClassIdx(CRYPTO_EX_INDEX_X509);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::~Cryptoki --
 *
 *      Cryptoki destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Cryptoki::~Cryptoki()
{
   for (std::list<Module *>::iterator i = mModules.begin();
        i != mModules.end(); i++) {
      delete *i;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::LoadModules --
 *
 *      Attempt to load and initialize all modules in a given
 *      directory.
 *
 * Results:
 *      Number of modules loaded.
 *
 * Side effects:
 *      Modules added to mModules.
 *
 *-----------------------------------------------------------------------------
 */

unsigned int
Cryptoki::LoadModules(const Util::string &dirPath) // IN
{
#ifdef __MINGW32__
   return 0;
#else
   DIR *dir = opendir(dirPath.c_str());
   if (!dir) {
      Warning("Could not open module directory path %s: %s\n", dirPath.c_str(),
              strerror(errno));
      return 0;
   }

   unsigned int loadedCount = mModules.size();

   dirent file;
   dirent *filep = NULL;
   while (!readdir_r(dir, &file, &filep) && filep) {
      // Skip .la, .a, .so.0*, etc. files.
      char *ext = strstr(file.d_name, ".so");
      if (ext && ext[3] == '\0') {
         char *module = g_module_build_path(dirPath.c_str(), file.d_name);
         LoadModule(module);
         g_free(module);
      }
   }
   closedir(dir);

   loadedCount = mModules.size() - loadedCount;
   Log("Loaded %u modules from %s\n", loadedCount, dirPath.c_str());
   return loadedCount;
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::LoadModule --
 *
 *      Load a cryptoki module, and initialize it.
 *
 * Results:
 *      true if module was loaded and initialized, false otherwise.
 *
 * Side effects:
 *      Module added to mModules.
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::LoadModule(const Util::string &filePath) // IN
{
   Module *module = new Module(this);
   if (!module->Load(filePath)) {
      delete module;
      return false;
   }
   mModules.push_back(module);
   return true;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetCertificates --
 *
 *      Find all certificates on cards in loaded modules.
 *
 * Results:
 *      A list of certificates.
 *
 * Side effects:
 *      May start a session with inserted cards.
 *
 *-----------------------------------------------------------------------------
 */

std::list<X509 *>
Cryptoki::GetCertificates(std::list<Util::string> &trustedIssuers) // IN
{
   std::list<X509 *> certs;

   for (std::list<Module *>::iterator i = mModules.begin();
        i != mModules.end(); i++) {
      (*i)->GetCertificates(certs, trustedIssuers);
   }

   Log("Found %d certificates.\n", (int)certs.size());

   return certs;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Login --
 *
 *      Attempt to log in to the token which contains cert using the
 *      provided pin.
 *
 * Results:
 *      true if the user is now authenticated to the token.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::Login(const X509 *cert, // IN
                const char *pin,  // IN
                GError **error)   // OUT
{
   Session *session = ExData<X509>::GetSession(cert);
   if (!session) {
      g_set_error(error, CDK_CRYPTOKI_ERROR, ERR_SESSION_NOT_FOUND,
                  _("No smart card sessions for your certificate could be "
                    "found"));
      return false;
   }
   return session->Login(cert, pin, error);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetPrivateKey --
 *
 *      Get the private key for a certificate.
 *
 * Results:
 *      A private key object that will sign data using the cryptoki
 *      module the cert came from, or NULL on error.
 *
 * Side effects:
 *      sRsaMethod may be initialized.
 *
 *-----------------------------------------------------------------------------
 */

EVP_PKEY *
Cryptoki::GetPrivateKey(const X509 *cert) // IN
{
   Session *session = ExData<X509>::GetSession(cert);
   return session ? session->GetPrivateKey(cert) : NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetSlotNames --
 *
 *      Go through all available modules, and find the available,
 *      unique slot names.
 *
 * Results:
 *      A list of slot names.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

std::vector<Util::string>
Cryptoki::GetSlotNames()
{
   std::set<Util::string> slots;
   for (std::list<Module *>::iterator i = mModules.begin();
        i != mModules.end(); i++) {
      (*i)->GetSlotNames(slots);
   }
   std::vector<Util::string> ret;
   for (std::set<Util::string>::iterator i = slots.begin();
        i != slots.end(); i++) {
      ret.push_back(*i);
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetSlotName --
 *
 *      Determines the name of the slot on which a certificate was
 *      stored.
 *
 * Results:
 *      A possibly empty string name.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Cryptoki::GetSlotName(const X509 *cert) // IN
{
   Session *session = ExData<X509>::GetSession(cert);
   return session ? session->GetSlotName() : "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetTokenName --
 *
 *      Get the name of the token on which a certificate is stored.
 *
 * Results:
 *      A possibly empty string name.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Cryptoki::GetTokenName(const X509 *cert) // IN
{
   Session *session = ExData<X509>::GetSession(cert);
   return session ? session->GetTokenName() : "";
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::DupCert --
 *
 *      Duplicate a certificate, copying over the cert's session and ID.
 *
 * Results:
 *      A new certificate.
 *
 * Side effects:
 *      Session ref count is increased.
 *
 *-----------------------------------------------------------------------------
 */

X509 *
Cryptoki::DupCert(X509 *cert) // IN
{
   if (!cert) {
      return NULL;
   }
   X509 *ret = X509_dup(cert);
   if (!ret) {
      return NULL;
   }
   ExData<X509>::SetSession(ret, ExData<X509>::GetSession(cert));
   ExData<X509>::SetId(ret, ExData<X509>::GetId(cert));
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::FreeCert --
 *
 *      Release a cert created with DupCert.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Session's ref count is decreased.
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::FreeCert(X509 *cert) // IN
{
   if (cert) {
      ExData<X509>::SetSession(cert, NULL);
      ExData<X509>::SetId(cert, NULL);
      X509_free(cert);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetIsInserted --
 *
 *      Return whether a cert's token is still inserted.
 *
 * Results:
 *      See above.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::GetIsInserted(const X509 *cert) // IN
{
   Session *session = ExData<X509>::GetSession(cert);
   if (!session) {
      Warning("Couldn't find session for cert %p\n", cert);
   }
   return session ? session->GetIsInserted() : false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::CloseAllSessions --
 *
 *      Close all active sessions on all devices.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::CloseAllSessions()
{
   Log("Closing all cryptoki sessions.\n");
   for (std::list<Module *>::iterator i = mModules.begin();
        i != mModules.end(); i++) {
      (*i)->CloseAllSessions();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::FreeCertificates --
 *
 *      Helper function to free certificates returned by
 *      GetCertificates().
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Certificates in certs are no longer valid objects.
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::FreeCertificates(std::list<X509 *> &certs) // IN/OUT
{
   for (std::list<X509 *>::iterator i = certs.begin();
        i != certs.end(); i++) {
      X509_free(*i);
   }
   certs.clear();
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetHasSlots --
 *
 *      Determine if any modules have any slots available.
 *
 * Results:
 *      true if any modules have available slots; false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::GetHasSlots()
{
   for (std::list<Module *>::iterator i = mModules.begin();
        i != mModules.end(); i++) {
      if ((*i)->GetHasSlots()) {
         return true;
      }
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetHasTokens --
 *
 *      Determine if any slots have tokens inserted.
 *
 * Results:
 *      true if any slots have tokens inserted; false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::GetHasTokens()
{
   for (std::list<Module *>::iterator i = mModules.begin();
        i != mModules.end(); i++) {
      if ((*i)->GetHasTokens()) {
         return true;
      }
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetHadEvent --
 *
 *      Determine if any slots had a token inserted or removed.
 *
 * Results:
 *      true if any slots had tokens inserted or removed; false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::GetHadEvent()
{
   for (std::list<Module *>::iterator i = mModules.begin();
        i != mModules.end(); i++) {
      if ((*i)->GetHadEvent()) {
         return true;
      }
   }
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::GetErrorQuark --
 *
 *      Returns the quark used for crytpoki GErrors.
 *
 * Results:
 *      A quark.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

GQuark
Cryptoki::GetErrorQuark()
{
   return g_quark_from_static_string("cdk-crytpoki-error-quark");
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::Module --
 *
 *      Cryptoki::Module constructor; initialize fields to NULL.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Cryptoki::Module::Module(Cryptoki *cryptoki) // IN
   : mCryptoki(cryptoki),
     mModule(NULL)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::~Module --
 *
 *      Cryptoki::Module destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Cryptoki::Module::~Module()
{
   if (mModule) {
      CloseAllSessions();
      mFuncs.C_Finalize(NULL);
      g_module_close(mModule);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::Load --
 *
 *      Attempt to load and initialize a cryptoki module.
 *
 * Results:
 *      true if module was loaded and initialized successfully, false
 *      otherwise.
 *
 * Side effects:
 *      Module may be loaded.
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::Module::Load(const Util::string &filePath) // IN
{
   ASSERT(!mModule);
   Log("Attempting to load %s...\n", filePath.c_str());
   mModule = g_module_open(
      filePath.c_str(),
      (GModuleFlags)(G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL));

   if (!mModule) {
      Warning("Could not open module %s: %s\n", filePath.c_str(),
              g_module_error());
      goto invalidate_module;
   }

   CK_C_Initialize cInit;
   if (!g_module_symbol(mModule, "C_Initialize", (gpointer *)&cInit)) {
      Warning("Could not resolve C_Initialize from %s\n", filePath.c_str());
      goto close_module;
   }

   CK_C_GetFunctionList cGetFuncs;
   if (!g_module_symbol(mModule, "C_GetFunctionList", (gpointer *)&cGetFuncs)) {
      Warning("Could not resolve C_GetFunctionList from %s\n",
              filePath.c_str());
      goto close_module;
   }

   CK_C_Finalize cFinalize;
   if (!g_module_symbol(mModule, "C_Finalize", (gpointer *)&cFinalize)) {
      Warning("Could not resolve C_Finalize from %s\n", filePath.c_str());
      goto close_module;
   }

   CK_RV rv;
   rv = cInit(NULL);
   if (rv != CKR_OK) {
      Warning("C_Initialize failed: %#lx (%s)\n", rv, filePath.c_str());
      goto close_module;
   }

   CK_FUNCTION_LIST_PTR funcs;
   rv = cGetFuncs(&funcs);
   if (rv != CKR_OK) {
      Warning("C_GetFunctionList failed: %#lx (%s)\n", rv, filePath.c_str());
      goto finalize_module;
   }
   if (!funcs) {
      Warning("C_GetFunctionList returned NULL function list (%s)\n",
              filePath.c_str());
      goto finalize_module;
   }
   mFuncs = *funcs;

   CK_INFO info;
   rv = mFuncs.C_GetInfo(&info);
   if (rv != CKR_OK) {
      Warning("C_GetInfo failed: %#lx (%s)\n", rv, filePath.c_str());
      goto finalize_module;
   }
   char *label;
   // Library description is 32 bytes, space padded, and NOT NULL terminated.
   label = g_strndup((const char *)info.libraryDescription, 32);
   mLabel = g_strchomp(label);
   g_free(label);

   Log("Loaded [%s] v%hhu.%hhu from %s\n", mLabel.c_str(),
       info.libraryVersion.major, info.libraryVersion.minor,
       filePath.c_str());
   return true;

  finalize_module:
   cFinalize(NULL);

  close_module:
   g_module_close(mModule);

  invalidate_module:
   mModule = NULL;
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::GetCertificates --
 *
 *      Add available certificates from this module to certs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May log in / start sessions on tokens.
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::Module::GetCertificates(std::list<X509 *> &certs,         // IN/OUT
                                  std::list<Util::string> &issuers) // IN
{
   Log("Getting certificates for module %s\n", mLabel.c_str());
   std::list<CK_SLOT_ID> slots = GetSlots();
   for (std::list<CK_SLOT_ID>::iterator i = slots.begin();
        i != slots.end(); i++) {
      Session *session = new Session(this);
      CK_RV rv = session->Open(*i);
      if (rv == CKR_OK) {
         session->GetCertificates(certs, issuers);
      }
      session->Release();
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::GetSlotNames --
 *
 *      Add the slot names found with this module to the passed-in
 *      set.  A set is used to avoid duplicate slots showing up if
 *      multiple modules are being used.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::Module::GetSlotNames(std::set<Util::string> &slots) // IN/OUT
{
   Log("Getting SmartCard slot names for Crytpki module %s\n", mLabel.c_str());
   std::list<CK_SLOT_ID> slotIds = GetSlots();
   for (std::list<CK_SLOT_ID>::iterator i = slotIds.begin();
        i != slotIds.end(); i++) {
      Util::string label = GetSlotName(*i);
      if (!label.empty()) {
         slots.insert(label);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::GetSlotName --
 *
 *      Get the name of a given slot.  Removes trailing whitespace
 *      from the slot name.
 *
 * Results:
 *      A possibly-empty string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Cryptoki::Module::GetSlotName(CK_SLOT_ID slot)
{
   Util::string ret;
   CK_SLOT_INFO info;
   if (mFuncs.C_GetSlotInfo(slot, &info) == CKR_OK) {
      /*
       * slotDescription is 64 bytes, space padded, and NOT NULL
       * terminated, although rdesktop works without the trailing
       * spaces.
       */
      char *tmpLabel = g_strndup((char *)info.slotDescription, 64);
      ret = g_strchomp(tmpLabel);
      g_free(tmpLabel);
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::CloseAllSessions --
 *
 *      Close all sessions for this module.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::Module::CloseAllSessions()
{
   std::list<CK_SLOT_ID> slots = GetSlots();
   CK_RV rv;
   for (std::list<CK_SLOT_ID>::iterator i = slots.begin();
        i != slots.end(); i++) {
      Session *session = new Session(this);
      if (CKR_OK == session->Open(*i)) {
         session->Logout();
      }
      session->Release();
      rv = mFuncs.C_CloseAllSessions(*i);
      if (rv != CKR_OK) {
         Warning("C_CloseAllSessions for module [%s], slot %lu failed: %#lx\n",
                 mLabel.c_str(), *i, rv);
      }
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::GetSlots --
 *
 *      Get the list of slots for this module.
 *
 * Results:
 *      A list of slot IDs.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

std::list<CK_SLOT_ID>
cdk::Cryptoki::Module::GetSlots()
{
   std::list<CK_SLOT_ID> slots;

   // Call once to get the number of slots.
   CK_ULONG count;
   CK_RV rv = mFuncs.C_GetSlotList(true, NULL, &count);
   if (rv != CKR_OK) {
      Warning("C_GetSlotList: cannot get number of slots: %#lx (%s)\n", rv,
              mLabel.c_str());
      return slots;
   }

   Log("%lu slots with tokens found (%s)\n", count, mLabel.c_str());
   if (!count) {
      return slots;
   }

   // And again to get the actual slot ids.
   CK_SLOT_ID tmpSlots[count];
   rv = mFuncs.C_GetSlotList(true, tmpSlots, &count);
   if (rv != CKR_OK) {
      Warning("C_GetSlotList: cannot get slot ids: %#lx (%s)\n", rv,
              mLabel.c_str());
      return slots;
   }

   for (CK_ULONG i = 0; i < count; i++) {
      slots.push_back(tmpSlots[i]);
   }
   return slots;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::GetHasSlots --
 *
 *      Determine if this module has any slots available.
 *
 * Results:
 *      true if there are available slots; false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::Module::GetHasSlots()
{
   CK_ULONG count;
   return CKR_OK == mFuncs.C_GetSlotList(false, NULL, &count) && count > 0;
}



/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::GetHasTokens --
 *
 *      Determine whether this module has any tokens inserted into its
 *      slots.
 *
 * Results:
 *      true if any slots have a token; false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::Module::GetHasTokens()
{
   CK_ULONG count;
   return CKR_OK == mFuncs.C_GetSlotList(true, NULL, &count) && count > 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Module::GetHadEvent --
 *
 *      Check if a this module has had a slot event.
 *
 * Results:
 *      true if a slot on this module has had an insertion or removal
 *      event.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
cdk::Cryptoki::Module::GetHadEvent()
{
   CK_SLOT_ID slot;
   return CKR_OK == mFuncs.C_WaitForSlotEvent(CKF_DONT_BLOCK, &slot, NULL_PTR);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::Session --
 *
 *      Session constructor
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Cryptoki::Session::Session(Module *module) // IN
   : mModule(module),
     mSession(CK_INVALID_HANDLE),
     mSlot(0),
     mRefCount(1),
     mNeedLogin(false)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::~Session --
 *
 *      Session destructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Session is closed.
 *
 *-----------------------------------------------------------------------------
 */

Cryptoki::Session::~Session()
{
   if (mSession != CK_INVALID_HANDLE) {
      Log("Closing session for token [%s]\n", mLabel.c_str());
      mModule->GetFunctions().C_CloseSession(mSession);
      mSession = CK_INVALID_HANDLE;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::Release --
 *
 *      Releases a reference to this Session object.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Session may be deleted.
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::Session::Release()
{
   ASSERT(mRefCount > 0);
   if (0 == --mRefCount) {
      delete this;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::Open --
 *
 *      Open a session with the given slot, logging in if necessary.
 *
 * Results:
 *      Cryptoki return value.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

CK_RV
Cryptoki::Session::Open(CK_SLOT_ID slot) // IN
{
   ASSERT(mSession == CK_INVALID_HANDLE);

   CK_FUNCTION_LIST funcs = mModule->GetFunctions();

   CK_TOKEN_INFO info;
   CK_RV rv = funcs.C_GetTokenInfo(slot, &info);
   if (rv != CKR_OK) {
      Warning("C_GetTokenInfo(%lu) failed: %#lx\n", slot, rv);
      goto invalid_session;
   }

   // Token label is 32 bytes, space padded, and NOT NULL terminated.
   char *tmpLabel;
   tmpLabel = g_strndup((char *)info.label, 32);
   mLabel = g_strchomp(tmpLabel);
   g_free(tmpLabel);

   rv = funcs.C_OpenSession(slot, CKF_SERIAL_SESSION, NULL, NULL, &mSession);
   if (rv != CKR_OK) {
      Warning("C_OpenSession failed: %#lx [%s]\n", rv, mLabel.c_str());
      goto invalid_session;
   }

   mNeedLogin = info.flags & CKF_LOGIN_REQUIRED;
   mSlot = slot;

   Log("Opened a new session for token [%s] hw v%hhu.%hhu fw v%hhu.%hhu\n",
       mLabel.c_str(),
       info.hardwareVersion.major, info.hardwareVersion.minor,
       info.firmwareVersion.major, info.firmwareVersion.minor);
   return CKR_OK;

  invalid_session:
   mSession = CK_INVALID_HANDLE;
   return rv;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::Login --
 *
 *      Attempts to log in to this token if necessary.
 *
 * Results:
 *      Whether the user is authenticated to the token.
 *
 * Side effects:
 *      mNeedLogin set to false on success.
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::Session::Login(const X509 *cert, // IN
                         const char *pin,  // IN
                         GError **error)   // OUT
{
   if (!mNeedLogin) {
      return true;
   }

   CK_FUNCTION_LIST funcs = mModule->GetFunctions();
   CK_RV rv;
   CK_TOKEN_INFO info;

   rv = funcs.C_GetTokenInfo(mSlot, &info);
   if (rv != CKR_OK) {
      Warning("C_GetTokenInfo(%lu) failed: %#lx\n", mSlot, rv);
      info.flags = 0;
   }

   if (info.flags & CKF_USER_PIN_LOCKED) {
      goto errLockedPin;
   }

   if (!pin) {
      Log("No PIN specified for [%s]\n", mLabel.c_str());
      if (info.flags & CKF_USER_PIN_FINAL_TRY) {
         goto errFinalTry;
      }
      g_set_error(error, CDK_CRYPTOKI_ERROR, ERR_INVALID_PIN,
                  _("A PIN is required to unlock this smartcard or "
                    "token."));
      return false;
   }

   rv = funcs.C_Login(mSession, CKU_USER, (unsigned char *)pin, strlen(pin));
   switch (rv) {
   case CKR_USER_ALREADY_LOGGED_IN:
      Log("Already logged in to card; continuing.\n");
      break;
   case CKR_OK:
      break;
   case CKR_PIN_LOCKED:
      goto errLockedPin;
   case CKR_PIN_INCORRECT:
      rv = funcs.C_GetTokenInfo(mSlot, &info);
      if (rv != CKR_OK) {
         Warning("C_GetTokenInfo(%lu) failed: %#lx\n", mSlot, rv);
         info.flags = 0;
      }
      if (info.flags & CKF_USER_PIN_FINAL_TRY) {
         goto errFinalTry;
      }
      g_set_error(error, CDK_CRYPTOKI_ERROR, ERR_INVALID_PIN,
                  _("Please try entering your PIN again."));
      return false;
   case CKR_DEVICE_REMOVED:
      /*
       * UI will go back to "insert smart card" so we don't want to
       * display an error in this case.
       */
      g_set_error(error, CDK_CRYPTOKI_ERROR, ERR_DEVICE_REMOVED,
                  _("Your smart card or token has been removed."));
      return false;
   default:
      Warning("C_Login attempt failed: %#lx [%s]\n", rv, mLabel.c_str());
      g_set_error(error, CDK_CRYPTOKI_ERROR, ERR_UNKNOWN,
                  _("There was an error logging in to your smart card or "
                    "token.\n\n"
                    "The error code was %#lx."), rv);
      return false;
   }

   mNeedLogin = false;
   return true;

errLockedPin:
   g_set_error(error, CDK_CRYPTOKI_ERROR, ERR_PIN_LOCKED,
               _("Your smart card or token has been locked.  Please "
                 "contact your administrator to unlock it."));
   return false;

errFinalTry:
   g_set_error(error, CDK_CRYPTOKI_ERROR, ERR_PIN_FINAL_TRY,
               _("An incorrect PIN entry will result in your "
                 "smart card or token being locked."));
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::Logout --
 *
 *      Logout from a cryptoki session.  This causes private objects
 *      (such as private keys) to become invalid, requiring a new
 *      login.
 *
 * Results:
 *      Result of the PKCS11 C_Logout function.
 *
 * Side effects:
 *      All sessions for this module are logged out.
 *
 *-----------------------------------------------------------------------------
 */

CK_RV
Cryptoki::Session::Logout()
{
   ASSERT(mSession != CK_INVALID_HANDLE);
   CK_RV rv = mModule->GetFunctions().C_Logout(mSession);
   switch (rv) {
   case CKR_OK:
      Log("Logged out of a session for token [%s]\n", mLabel.c_str());
      break;
   case CKR_USER_NOT_LOGGED_IN:
      // This is normal, no need to report it.
      break;
   default:
      Warning("C_Logout failed: %#lx [%s]\n", rv, mLabel.c_str());
      break;
   }
   return rv;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::GetCertificates --
 *
 *      Add valid certificates on this slot to certs.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

void
Cryptoki::Session::GetCertificates(std::list<X509 *> &certs,         // IN/OUT
                                   std::list<Util::string> &issuers) // IN
{
   CK_FUNCTION_LIST funcs = mModule->GetFunctions();

   CK_RV rv;
   // Initialize the search.
   CK_OBJECT_CLASS certClass = CKO_CERTIFICATE;
   CK_CERTIFICATE_TYPE certType = CKC_X_509;

   CK_ATTRIBUTE certAttrs[] = {
      { CKA_CLASS, &certClass, sizeof(certClass) },
      { CKA_CERTIFICATE_TYPE, &certType, sizeof(certType) }
   };
   rv = funcs.C_FindObjectsInit(mSession, certAttrs, G_N_ELEMENTS(certAttrs));
   if (rv != CKR_OK) {
      Warning("C_FindObjectsInit failed: %#lx [%s]\n", rv, mLabel.c_str());
      return;
   }

   // Iterate through objects, one at a time.
   GByteArray *id = NULL;
   while (true) {
      CK_ULONG objCount;
      CK_OBJECT_HANDLE obj;
      rv = funcs.C_FindObjects(mSession, &obj, 1, &objCount);
      if (rv != CKR_OK) {
         Warning("C_FindObjects failed: %#lx [%s]\n", rv, mLabel.c_str());
         break;
      }
      if (!objCount) {
         break;
      }

      CK_ATTRIBUTE attrs[] = {
         { CKA_VALUE, NULL, 0 },
         { CKA_ID, NULL, 0 },
      };
      rv = funcs.C_GetAttributeValue(mSession, obj, attrs, G_N_ELEMENTS(attrs));
      if (rv != CKR_OK) {
         Warning("C_GetAttributeValue failed: %#lx [%s]\n", rv, mLabel.c_str());
         continue;
      }

      Log("Found a cert that's %lu bytes long, id %lu bytes long\n",
          attrs[0].ulValueLen, attrs[1].ulValueLen);

      CK_BYTE_PTR cert = new CK_BYTE[attrs[0].ulValueLen];
      attrs[0].pValue = cert;

      if (id) {
         g_byte_array_free(id, true);
      }
      id = g_byte_array_sized_new(attrs[1].ulValueLen);
      g_byte_array_set_size(id, attrs[1].ulValueLen);
      attrs[1].pValue = id->data;

      rv = funcs.C_GetAttributeValue(mSession, obj, attrs, G_N_ELEMENTS(attrs));
      if (rv != CKR_OK) {
         Warning("C_GetAttributeValue 2 failed: %#lx [%s]\n", rv, mLabel.c_str());
         delete[] cert;
         continue;
      }

      Log("Cert ID: %s\n", Id2String(id).c_str());

      /*
       * from d2i_X509(3SSL):
       *
       * The ways that *in and *out are incremented after the
       * operation can trap the unwary. See the WARNINGS section
       * for some common errors.
       *
       * Basically, we need to remember the old cert value so we
       * can free it.
       */
      D2I_X509_CONST unsigned char *tmpCert = cert;
      X509 *x509 = d2i_X509(NULL, &tmpCert, attrs[0].ulValueLen);
      delete[] cert;

      if (!x509) {
         Warning("Could not parse cert: %s [%s]\n",
                 ERR_reason_error_string(ERR_get_error()),
                 mLabel.c_str());
         continue;
      }

      X509_NAME *issuer = X509_get_issuer_name(x509);
      char *dispName = X509_NAME_oneline(issuer, NULL, 0);
      if (!dispName) {
         X509_free(x509);
         continue;
      }
      std::list<Util::string>::iterator found =
         std::find(issuers.begin(), issuers.end(), dispName);

      if (found == issuers.end()) {
         Log("Cert issuer %s not accepted by server, ignoring cert.\n",
             dispName);
         OPENSSL_free(dispName);
         X509_free(x509);
         continue;
      }
      OPENSSL_free(dispName);

      // http://www.mail-archive.com/openssl-users@openssl.org/msg01662.html
      int idx = -1;
      while (true) {
         idx = X509_get_ext_by_NID(x509, NID_ext_key_usage, idx);
         if (idx < 0) {
            break;
         }
         STACK_OF(ASN1_OBJECT) *objs =
            (STACK_OF(ASN1_OBJECT) *)X509V3_EXT_d2i(X509_get_ext(x509, idx));
         for (int i = 0; i < sk_ASN1_OBJECT_num(objs); i++) {
            int nid = OBJ_obj2nid(sk_ASN1_OBJECT_value(objs, i));
            switch (nid) {
            case NID_ms_smartcard_login:
            case NID_client_auth:
               Log("Found a valid EKU: %s\n", OBJ_nid2ln(nid));
               goto validKeyUsage;
            default:
               Log("Skipping non-useful EKU: %s\n", OBJ_nid2ln(nid));
               break;
            }
         }
         sk_ASN1_OBJECT_pop_free(objs, ASN1_OBJECT_free);
      }
      Log("No valid EKUs were found; skipping cert.\n");
      continue;

   validKeyUsage:
      ExData<X509>::SetSession(x509, this);
      ExData<X509>::SetId(x509, id);

#ifdef VMX86_DEVEL
      fprintf(stderr, "Found a cert:\n");
      X509_print_fp(stderr, x509);
#endif

      // Success, at long last!
      certs.push_back(x509);
   }
   if (id) {
      g_byte_array_free(id, true);
   }

   funcs.C_FindObjectsFinal(mSession);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::GetPrivateKey --
 *
 *      Get the private key for a given X509 certificate.
 *
 * Results:
 *      An object representing the private key, or NULL if it could
 *      not be created.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

EVP_PKEY *
Cryptoki::Session::GetPrivateKey(const X509 *cert) // IN
{
   const GByteArray *id = ExData<X509>::GetId(cert);

   Log("Trying to get private key for id %s\n", Id2String(id).c_str());

   if (mNeedLogin) {
      return NULL;
   }

   CK_RV rv;
   CK_FUNCTION_LIST funcs = mModule->GetFunctions();
   CK_OBJECT_CLASS keyClass = CKO_PRIVATE_KEY;
   CK_KEY_TYPE keyType = CKK_RSA;

   CK_ATTRIBUTE attrs[] = {
      { CKA_CLASS, &keyClass, sizeof(keyClass) },
      { CKA_KEY_TYPE, &keyType, sizeof(keyType) },
      { CKA_ID, NULL, 0 }
   };
   attrs[2].pValue = id->data;
   attrs[2].ulValueLen = id->len;

   rv = funcs.C_FindObjectsInit(mSession, attrs, G_N_ELEMENTS(attrs));
   if (rv != CKR_OK) {
      Warning("C_FindObjectsInit failed: %#lx [%s]\n", rv, mLabel.c_str());
      return NULL;
   }

   // Iterate through objects, one at a time.
   EVP_PKEY *pkey = NULL;
   while (true) {
      CK_ULONG objCount;
      CK_OBJECT_HANDLE obj;

      rv = funcs.C_FindObjects(mSession, &obj, 1, &objCount);
      if (rv != CKR_OK) {
         Warning("C_FindObjects failed: %#lx [%s]\n", rv, mLabel.c_str());
         break;
      }
      if (!objCount) {
         break;
      }

      RSA *key = RSA_new();
      if (!key) {
         Warning("Could not create rsa key: %s\n",
                 ERR_reason_error_string(ERR_get_error()));
         continue;
      }
      RSA_set_method(key, GetRsaMethod());
      key->flags |= RSA_FLAG_SIGN_VER | RSA_FLAG_EXT_PKEY;

      ExData<RSA>::SetSession(key, this);
      ExData<RSA>::SetObject(key, obj);

      pkey = EVP_PKEY_new();
      if (!EVP_PKEY_assign_RSA(pkey, key)) {
         Warning("Could not assign pkey: %s\n",
                 ERR_reason_error_string(ERR_get_error()));
         RSA_free(key);
         continue;
      }
      break;
   }
   funcs.C_FindObjectsFinal(mSession);
   return pkey;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Crytoki::Session::GetSlotName --
 *
 *      Gets the name of this session's slot from its module.
 *
 * Results:
 *      A possibly empty string.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Cryptoki::Session::GetSlotName()
{
   ASSERT(mSession != CK_INVALID_HANDLE);
   return mModule->GetSlotName(mSlot);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::GetIsInserted --
 *
 *      Determines whether the token associated with this session is
 *      still inserted.
 *
 * Results:
 *      true if getting the session info for this session succeeds.
 *      false if the token was removed, or on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
Cryptoki::Session::GetIsInserted()
{
   ASSERT(mSession != CK_INVALID_HANDLE);
   CK_FUNCTION_LIST funcs = mModule->GetFunctions();
   CK_SESSION_INFO info;
   CK_RV rv = funcs.C_GetSessionInfo(mSession, &info);
   switch (rv) {
   case CKR_OK:
      Log("Token [%s] still inserted.\n", mLabel.c_str());
      return true;
   case CKR_DEVICE_REMOVED:
      Log("Token [%s] removed.\n", mLabel.c_str());
      return false;
   default:
      Warning("Error getting session info for [%s]: %s\n",
              mLabel.c_str(), ERR_reason_error_string(ERR_get_error()));
      return false;
   }
   NOT_REACHED();
   return false;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::GetRsaMethod --
 *
 *      Get the RSA_METHOD for use with a private key.
 *
 * Results:
 *      The RSA_METHOD.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

const RSA_METHOD *
Cryptoki::Session::GetRsaMethod()
{
   static RSA_METHOD rsa = { 0 };
   if (!rsa.name) {
      rsa = *RSA_get_default_method();
      rsa.name = "VMware Cryptoki RSA Method";
      rsa.rsa_sign = RsaSign;
      rsa.flags |= RSA_METHOD_FLAG_NO_CHECK;
   }
   return &rsa;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::RsaSign --
 *
 *      Sign data using the cryptoki module stored in rsa.
 *
 *      The data to sign is stored in m, and is of length m_length.
 *      sigret is the destination, and is of siglen length; the length
 *      of the signature should be stored here to return to the
 *      caller.
 *
 * Results:
 *      0 on error, 1 on success.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
Cryptoki::Session::RsaSign(int type,               // IN
                           const unsigned char *m, // IN
                           unsigned int m_length,  // IN
                           unsigned char *sigret,  // IN/OUT
                           unsigned int *siglen,   // IN/OUT
                           const RSA *rsa)         // IN
{
   Log("RsaSign: need to sign %u bytes of data\n", m_length);

   Session *session = ExData<RSA>::GetSession(rsa);
   ASSERT(session);

   CK_FUNCTION_LIST funcs = session->mModule->GetFunctions();

   // XXX: need to convert incoming type to the correct mechanism
   CK_MECHANISM mech = { CKM_RSA_PKCS, NULL, 0 };
   CK_RV rv = funcs.C_SignInit(session->mSession, &mech,
                               ExData<RSA>::GetObject(rsa));
   if (rv != CKR_OK) {
      Warning("C_SignInit failed: %#lx [%s]\n", rv, session->mLabel.c_str());
      return 0;
   }

   /*
    * The PKCS#11 API differs from OpenSSL in that the siglen
    * parameter is undefined in OpenSSL, but should be set to the
    * length of sigret in PKCS#11.  Unfortunately, since we're using a
    * "custom" private key, we can't get the size from RSA, so we do
    * the sign data call twice, the first with a NULL buffer to get
    * the length the card thinks we need.
    */
   unsigned long siglen_l = 0;
   // Once to get the length...
   rv = funcs.C_Sign(session->mSession, (unsigned char *)m, m_length, NULL,
                     &siglen_l);
   if (rv != CKR_OK) {
      Warning("C_Sign failed to get length: %#lx [%s]\n", rv,
              session->mLabel.c_str());
      return 0;
   }

   Log("RsaSign: %lu bytes needed for signature\n", siglen_l);

   // ...and once to do the signing.
   rv = funcs.C_Sign(session->mSession, (unsigned char *)m, m_length, sigret,
                     &siglen_l);

   if (rv != CKR_OK) {
      Warning("C_Sign failed: %#lx [%s]\n", rv, session->mLabel.c_str());
      return 0;
   }
   *siglen = siglen_l;
   Log("Returned %u bytes of signed data\n", *siglen);
   return 1;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::Session::Id2String --
 *
 *      Create a string representation of a cryptoki ID.
 *
 * Results:
 *      A string representing id.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

Util::string
Cryptoki::Session::Id2String(const GByteArray *id) // IN
{
   // Each byte in id will be two bytes in hex
   GString *str = g_string_sized_new(id->len * 2);
   for (unsigned int i = 0; i < id->len; i++) {
      g_string_append_printf(str, "%02x", id->data[i]);
   }
   Util::string ret(str->str);
   g_string_free(str, true);
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::GetSession --
 *
 *      Get the associated Session object from the ext data.
 *
 * Results:
 *      The Session object previously set on t, possibly NULL.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> Cryptoki::Session *
Cryptoki::ExData<T>::GetSession(const T *t) // IN
{
   return (Session *)CRYPTO_get_ex_data(&t->ex_data, GetSessionIdx());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::SetSession --
 *
 *      Set the session on the associated ex data.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      session is AddRefed.
 *
 *-----------------------------------------------------------------------------
 */

template<class T> void
Cryptoki::ExData<T>::SetSession(T *t,             // IN
                                Session *session) // IN
{
   Session *oldSession = (Session *)CRYPTO_get_ex_data(&t->ex_data,
                                                       GetSessionIdx());
   if (oldSession) {
      oldSession->Release();
   }
   if (session) {
      session->AddRef();
   }
   CRYPTO_set_ex_data(&t->ex_data, GetSessionIdx(), session);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::SetClassIdx --
 *
 *      Sets the ex data class index for this template's type.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> void
Cryptoki::ExData<T>::SetClassIdx(int classIdx) // IN
{
   if (sClassIdx == -1) {
      sClassIdx = classIdx;
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::GetObject --
 *
 *      Get the private key's object handle.
 *
 * Results:
 *      An object handle.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> CK_OBJECT_HANDLE
Cryptoki::ExData<T>::GetObject(const T *t) // IN
{
   return (CK_OBJECT_HANDLE)CRYPTO_get_ex_data(&t->ex_data, GetObjectIdx());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::SetObject --
 *
 *      Set the object handle for the private key.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> void
Cryptoki::ExData<T>::SetObject(T *t,                 // IN
                               CK_OBJECT_HANDLE obj) // IN
{
   CRYPTO_set_ex_data(&t->ex_data, GetObjectIdx(), (void *)obj);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::GetId --
 *
 *      Get the object handle associated with t.
 *
 * Results:
 *      An object handle.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> const GByteArray *
Cryptoki::ExData<T>::GetId(const T *t) // IN
{
   return (const GByteArray *)CRYPTO_get_ex_data(&t->ex_data, GetIdIdx());
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::SetId --
 *
 *      Set the object handle for an ex data object.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> void
Cryptoki::ExData<T>::SetId(T *t,                 // IN
                           const GByteArray *id) // IN
{
   GByteArray *oldId = (GByteArray *)CRYPTO_get_ex_data(&t->ex_data, GetIdIdx());
   if (!id) {
      if (oldId) {
         g_byte_array_free(oldId, true);
         CRYPTO_set_ex_data(&t->ex_data, GetIdIdx(), NULL);
      }
      return;
   }
   if (!oldId) {
      oldId = g_byte_array_sized_new(id->len);
      CRYPTO_set_ex_data(&t->ex_data, GetIdIdx(), oldId);
   }
   g_byte_array_set_size(oldId, id->len);
   memcpy(oldId->data, id->data, oldId->len);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::GetIdIdx --
 *
 *      Get the index for using cryptoki ids with this object type.
 *
 * Results:
 *      The ex data index for ids.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> int
Cryptoki::ExData<T>::GetIdIdx()
{
   static int idx = -1;
   if (idx == -1) {
      idx = CRYPTO_get_ex_new_index(sClassIdx, 0, NULL, NULL, DupId, FreeId);
   }
   return idx;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::GetObjectIdx --
 *
 *      Get the index for using cryptoki object handles with this ssl
 *      object type.
 *
 * Results:
 *      The ex data index for object handles.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> int
Cryptoki::ExData<T>::GetObjectIdx()
{
   static int idx = -1;
   if (idx == -1) {
      idx = CRYPTO_get_ex_new_index(sClassIdx, 0, NULL, NULL, NULL, NULL);
   }
   return idx;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::GetSessionIdx --
 *
 *      Get the index for using Session objects with this ssl object
 *      type.
 *
 * Results:
 *      The ex data index for Session objects.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> int
Cryptoki::ExData<T>::GetSessionIdx()
{
   static int idx = -1;
   if (idx == -1) {
      idx = CRYPTO_get_ex_new_index(sClassIdx, 0, NULL, NULL, DupSession,
                                    FreeSession);
   }
   return idx;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::DupId --
 *
 *      Duplicate the object's ID, if it has one.  Store the new ID in
 *      *from_d.
 *
 * Results:
 *      0 for success.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> int
Cryptoki::ExData<T>::DupId(CRYPTO_EX_DATA *to,   // IN/UNUSED
                           CRYPTO_EX_DATA *from, // IN/UNUSED
                           void *from_d,         // IN/OUT
                           int idx,              // IN/UNUSED
                           long argl,            // IN/UNUSED
                           void *argp)           // IN/UNUSED
{
   const GByteArray *oldId = *(const GByteArray **)from_d;
   if (!oldId) {
      return 0;
   }
   GByteArray *id = g_byte_array_sized_new(oldId->len);
   g_byte_array_set_size(id, oldId->len);
   memcpy(id->data, oldId->data, id->len);
   *(GByteArray **)from_d = id;
   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::FreeId --
 *
 *      Called when an X509 object is freed.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

template<class T> void
Cryptoki::ExData<T>::FreeId(void *parent,       // IN/UNUSED
                            void *ptr,          // IN
                            CRYPTO_EX_DATA *ad, // IN/UNUSED
                            int idx,            // IN/UNUSED
                            long argl,          // IN/UNUSED
                            void *argp)         // IN/UNUSED
{
   if (ptr) {
      g_byte_array_free((GByteArray *)ptr, true);
   }
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::DupSession --
 *
 *      Called when an object is duplicated.  Refs the associated
 *      session, if there is one.
 *
 * Results:
 *      returns 0
 *
 * Side effects:
 *      *from_d may be AddRefed.
 *
 *-----------------------------------------------------------------------------
 */

template<class T> int
Cryptoki::ExData<T>::DupSession(CRYPTO_EX_DATA *to,   // IN/UNUSED
                                CRYPTO_EX_DATA *from, // IN/UNUSED
                                void *from_d,         // IN/OUT
                                int idx,              // IN/UNUSED
                                long argl,            // IN/UNUSED
                                void *argp)           // IN/UNUSED
{
   Session **session = (Session **)from_d;
   if (*session) {
      (*session)->AddRef();
   }
   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::Cryptoki::ExData<T>::FreeSession --
 *
 *      Called when an X509 cert is freed.  Release the session, which
 *      may close the underlying session, and free itself.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      May release ptr.
 *
 *-----------------------------------------------------------------------------
 */

template<class T> void
Cryptoki::ExData<T>::FreeSession(void *parent,        // IN/UNUSED
                                 void *ptr,           // IN
                                 CRYPTO_EX_DATA *ad,  // IN/UNUSED
                                 int idx,             // IN/UNUSED
                                 long argl,           // IN/UNUSED
                                 void *argp)          // IN/UNUSED
{
   if (ptr) {
      ((Session *)ptr)->Release();
   }
}


} // namespace cdk
