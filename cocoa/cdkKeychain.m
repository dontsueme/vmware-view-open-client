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
 * cdkKeychain.m --
 *
 *      Implementation of CdkKeychain!
 */

extern "C" {
#include "vm_basic_types.h"
#define _UINT64
}


#import <openssl/err.h>
#import <openssl/rsa.h>
#import <openssl/x509.h>


#import "cdkKeychain.h"
#import "util.hh"


/*
 * d2i_X509() changed signature between 0.9.7 and 0.9.8, which we use
 * on OS X 10.5.
 */
#if OPENSSL_VERSION_NUMBER < 0x00908000L
#define D2I_X509_CONST
#else
#define D2I_X509_CONST const
#endif

#define SIGNING_RETRIES 3


@interface CdkKeychain (Private)
+(const RSA_METHOD *)sharedRsaMethod;
+(int)identityIdx;
@end // @interface CdkKeychain (Private)


/*
 *-----------------------------------------------------------------------------
 *
 * CdkKeychainRsaSign --
 *
 *      Sign data using the identity stored in rsa.
 *
 *      The data to sign is stored in m, and is of length m_length.
 *      sigret is the destination, and is of siglen length; the length
 *      of the signature should be stored here to return to the
 *      caller.
 *
 *      To get to the low-level signing function, we need to jump
 *      through a few hoops to get from an identity to what we need to
 *      sign data.  We wouldn't have to do all of this if we switched
 *      to OS X's networking code instead of curl.
 *
 * Results:
 *      0 on error, 1 on success.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static int
CdkKeychainRsaSign(int type,               // IN
                   const unsigned char *m, // IN
                   unsigned int m_length,  // IN
                   unsigned char *sigret,  // IN/OUT
                   unsigned int *siglen,   // IN/OUT
                   const RSA *rsa)         // IN
{
   int ret = 0;

   Log("RsaSign: need to sign %u bytes of data\n", m_length);

   SecIdentityRef ident =
      (SecIdentityRef)CRYPTO_get_ex_data(&rsa->ex_data,
                                         [CdkKeychain identityIdx]);

   if (!ident) {
      Warning("Could not find and identity for rsa object %p\n", rsa);
      return 0;
   }

   SecKeyRef privKey = NULL;
   SecKeychainRef keychain = NULL;
   CSSM_CC_HANDLE ctx = 0;
   int tries = 1;
   int rv = 0;

   require_noerr(SecIdentityCopyPrivateKey(ident, &privKey), rsaSignExit);
   require_noerr(SecKeychainItemCopyKeychain((SecKeychainItemRef)privKey,
                                             &keychain), rsaSignExit);

   /*
    * The remaining objects' lifetimes are controlled by privKey and
    * keychain above, and don't need to be released on their own.
    */
   CSSM_CSP_HANDLE csp;
   require_noerr(SecKeychainGetCSPHandle(keychain, &csp), rsaSignExit);

   const CSSM_ACCESS_CREDENTIALS *creds;
   require_noerr(SecKeyGetCredentials(privKey,
                                      CSSM_ACL_AUTHORIZATION_SIGN,
                                      kSecCredentialTypeWithUI,
                                      &creds), rsaSignExit);

   const CSSM_KEY *key;
   require_noerr(SecKeyGetCSSMKey(privKey, &key), rsaSignExit);

   CSSM_DATA signIn;
   CSSM_DATA signOut;
   signIn.Length = m_length;
   signIn.Data = (uint8 *)m;
   signOut.Length = 0;
   signOut.Data = NULL;

tryToSign:
   /*
    * The signature context is what our authentication is tied to, so
    * to be able to retry the PIN, we need to do this again.
    */
   require_noerr(CSSM_CSP_CreateSignatureContext(csp, CSSM_ALGID_RSA, creds,
                                                 key, &ctx), rsaSignExit);

   /*
    * Finally, we have collected all of the runes, and may actually
    * sign the data.
    *
    * This may prompt the user whether to allow us to use this
    * keychain object, and for the smart card PIN/keychain's password.
    */
   rv = CSSM_SignData(ctx, &signIn, 1, CSSM_ALGID_NONE, &signOut);
   Log("Attempt #%d for signing data: %#x\n", tries, rv);
   switch (rv) {
   case noErr:
      *siglen = signOut.Length;
      memcpy(sigret, signOut.Data, signOut.Length);
      free(signOut.Data);
      Log("Returned %u bytes of signed data\n", *siglen);
      // Success!
      ret = 1;
      break;
   case CSSMERR_DL_INVALID_SAMPLE_VALUE:
   case CSSMERR_CSP_PRIVATE_KEY_NOT_FOUND:
      /*
       * These may not be the only error code that indicates an
       * invalid PIN.
       */
      if (++tries <= SIGNING_RETRIES) {
         CSSM_DeleteContext(ctx);
         ctx = 0;
         goto tryToSign;
      }
      // fall through...
   case CSSMERR_CSP_USER_CANCELED:
      // User canceled, just exit as well.
   default: {
      NSString *err = (NSString *)SecCopyErrorMessageString(rv, NULL);
      Warning("Could not sign RSA data: %s\n", [err UTF8String]);
      [err release];
      break;
   }
   }

rsaSignExit:
   if (ctx) {
      CSSM_DeleteContext(ctx);
   }
   if (privKey) {
      CFRelease(privKey);
   }
   if (keychain) {
      CFRelease(keychain);
   }
   return ret;
}


/*
 *-----------------------------------------------------------------------------
 *
 * CdkKeychainDupIdentity --
 *
 *      Callback when an RSA object is duplicated.
 *
 * Results:
 *      0 for success.
 *
 * Side effects:
 *      Identity may be duplicated.
 *
 *-----------------------------------------------------------------------------
 */

static int
CdkKeychainDupIdentity(CRYPTO_EX_DATA *to,   // IN/UNUSED
                       CRYPTO_EX_DATA *from, // IN/UNUSED
                       void *from_d,         // IN/OUT
                       int idx,              // IN/UNUSED
                       long argl,            // IN/UNUSED
                       void *argp)           // IN/UNUSED
{
   SecIdentityRef *ident = (SecIdentityRef *)from_d;
   if (*ident) {
      Log("Duplicating identity: %p\n", *ident);
      CFRetain(*ident);
   }
   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * CdkKeychainFreeIdentity --
 *
 *      Callback when an RSA object is freed.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Any corresponding identity is released.
 *
 *-----------------------------------------------------------------------------
 */

static void
CdkKeychainFreeIdentity(void *parent,        // IN/UNUSED
                        void *ptr,           // IN
                        CRYPTO_EX_DATA *ad,  // IN/UNUSED
                        int idx,             // IN/UNUSED
                        long argl,           // IN/UNUSED
                        void *argp)          // IN/UNUSED
{
   if (ptr) {
      Log("Freeing identity: %p\n", ptr);
      CFRelease((SecIdentityRef)ptr);
   }
}


@implementation CdkKeychain


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkKeychain sharedKeychain] --
 *
 *      Returns the shared CdkKeychain instance, creating it if
 *      necessary.
 *
 * Results:
 *      the shared CdkKeychain object, or nil on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(CdkKeychain *)sharedKeychain
{
   static CdkKeychain *keychain = nil;
   if (!keychain) {
      keychain = [[CdkKeychain alloc] init];
   }
   return keychain;
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkKeychain sharedRsaMethod] --
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

+(const RSA_METHOD *)sharedRsaMethod
{
   static RSA_METHOD rsa = { 0 };
   if (!rsa.name) {
      rsa = *RSA_get_default_method();
      rsa.name = "VMware Keychain RSA Method";
      rsa.rsa_sign = CdkKeychainRsaSign;
      rsa.flags |= RSA_METHOD_FLAG_NO_CHECK;
   }
   return &rsa;
}


/*
 *-----------------------------------------------------------------------------
 *
 * +[CdkKeychain identityIdx] --
 *
 *      When storing a SecIdentity with an RSA object, we need a
 *      corresponding ExData index.
 *
 * Results:
 *      Index to use when storing or retrieving an RSA object's SecIdentity.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

+(int)identityIdx
{
   static int idx = -1;
   if (idx == -1) {
      idx = CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_RSA, 0, NULL, NULL,
                                    CdkKeychainDupIdentity,
                                    CdkKeychainFreeIdentity);
   }
   return idx;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkKeychain certificateWithIdentity:] --
 *
 *      Create and return an OpenSSL X509 certificate from an identity.
 *
 *      Simply get the data from the cert, and use OpenSSL's data
 *      parser to create the object.
 *
 * Results:
 *      An X509 cert, or NULL on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(X509 *)certificateWithIdentity:(SecIdentityRef)identity // IN
{
   ASSERT(identity);

   X509 *x509 = NULL;
   SecCertificateRef cert = NULL;
   CSSM_DATA data = { 0 };
   D2I_X509_CONST unsigned char *tmpCert;

   require_noerr(SecIdentityCopyCertificate(identity, &cert), bail);
   require_noerr(SecCertificateGetData(cert, &data), bail);

   tmpCert = data.Data;
   x509 = d2i_X509(NULL, &tmpCert, data.Length);
   if (!x509) {
      Warning("Could not parse cert: %s\n",
              ERR_reason_error_string(ERR_get_error()));
   }

bail:
   if (cert) {
      CFRelease(cert);
   }
   return x509;
}


/*
 *-----------------------------------------------------------------------------
 *
 * -[CdkKeychain privateKeyWithIdentity:] --
 *
 *      Create and return an OpenSSL private key from an identity.
 *
 *      We create our own RSA object, which can sign data using the
 *      identity's private certificate, and use this to create an EVP key.
 *
 * Results:
 *      An OpenSSL private key, or NULL on error.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

-(EVP_PKEY *)privateKeyWithIdentity:(SecIdentityRef)identity // IN
{
   RSA *key = RSA_new();
   if (!key) {
      Warning("Could not create rsa key: %s\n",
              ERR_reason_error_string(ERR_get_error()));
      return NULL;
   }
   RSA_set_method(key, [CdkKeychain sharedRsaMethod]);
   key->flags |= RSA_FLAG_SIGN_VER | RSA_FLAG_EXT_PKEY;

   // This reference is released in CdkKeychainFreeIdentity.
   CFRetain(identity);
   CRYPTO_set_ex_data(&key->ex_data, [CdkKeychain identityIdx], identity);

   EVP_PKEY *pkey = EVP_PKEY_new();
   if (!EVP_PKEY_assign_RSA(pkey, key)) {
      Warning("Could not assign private key: %s\n",
              ERR_reason_error_string(ERR_get_error()));
      RSA_free(key);
      return NULL;
   }
   return pkey;
}


@end // @implementation CdkKeychain
