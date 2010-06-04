/*********************************************************
 * Copyright (C) 2010 VMware, Inc. All rights reserved.
 *
 * This file is part of VMware View Open Client.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
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
 * testUrl.cc --
 *
 *      Test CdkUrl_Parse().
 */

#include <string.h>


#include "cdkUrl.h"


/*
 *-----------------------------------------------------------------------------
 *
 * TestInt --
 *
 *      Test whether two strings are equal.
 *
 * Results:
 *      TRUE if ints are equal.
 *
 * Side effects:
 *      Prints error if not.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TestInt(const char *name, /* IN */
        int var,          /* IN */
        int exp)          /* IN */
{
    if (var != exp) {
        g_printerr("Expected %s %d, was: %d\n", name, exp, var);
        return FALSE;
    }
    return TRUE;
}


/*
 *-----------------------------------------------------------------------------
 *
 * TestStr --
 *
 *      Test whether two strings are equal.
 *
 * Results:
 *      TRUE if strings are equal.
 *
 * Side effects:
 *      Prints error if not.
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TestStr(const char *name, /* IN */
        const char *var,  /* IN */
        const char *exp)  /* IN */
{
    if (strcmp(var, exp)) {
        g_printerr("Expected %s %s, was: %s\n", name, exp, var);
        return FALSE;
    }
    return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 *
 * TestUrl --
 *
 *      Test URL Parsing.
 *
 * Results:
 *      TRUE if url parsed correctly (or, didn't parse and expProto was NULL).
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

static gboolean
TestUrl(const char *url,      /* IN */
        const char *expProto, /* IN/OPT */
        const char *expHost,  /* IN/OPT */
        gushort expPort,      /* IN/OPT */
        const char *expPath,  /* IN/OPT */
        gboolean expSecure)   /* IN/OPT */
{
#define TEST_INT(v, e) success = TestInt(#v, v, e) && success
#define TEST_STR(v, e) success = TestStr(#v, v, e) && success

#define PASS(s) g_print(   "    PASSED: %s -> %s\n", url, s)
#define FAIL(s) G_STMT_START { \
      g_printerr("!!! FAILED: %s => %s\n", url, s);     \
      success = FALSE;                                  \
   } G_STMT_END

    char *proto;
    char *host;
    unsigned short port;
    char *path;
    gboolean secure = FALSE;
    gboolean success = TRUE;

    if (CdkUrl_Parse(url, &proto, &host, &port, &path, &secure)) {
        if (expProto) {
            TEST_STR(proto, expProto);
            TEST_STR(host, expHost);
            TEST_INT(port, expPort);
            TEST_STR(path, expPath);
            TEST_INT(secure, expSecure);
            if (success) {
               PASS("parsed correctly");
            } else {
               FAIL("parsed incorrectly");
            }
        } else {
           FAIL("should not have parsed");
        }
        g_free(proto);
        g_free(host);
        g_free(path);
    } else if (expProto) {
       FAIL("did not parse");
    } else {
       PASS("did not parse");
    }
    return success;

#undef TEST_INT
#undef TEST_STR
#undef PASS
#undef FAIL
}


/*
 *-----------------------------------------------------------------------------
 *
 * main --
 *
 *      Main function.  Actual urls to test.
 *
 * Results:
 *      Number of failed tests.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

int
main(int argc,     /* IN/UNUSED */
     char *argv[]) /* IN/UNUSED */
{
#define TEST_URL(url, expProto, expHost, expPort, expPath, expSecure)   \
    if (TestUrl(url, expProto, expHost, expPort, expPath, expSecure)) { \
        passed++;                                                       \
    } else {                                                            \
        failed++;                                                       \
    }

#define TEST_URL_FAIL(url) TEST_URL(url, NULL, NULL, 0, NULL, 0)

   int passed = 0;
   int failed = 0;

   TEST_URL("a", "http", "a", 80, "/", FALSE);
   TEST_URL("a/", "http", "a", 80, "/", FALSE);
   TEST_URL("https://vmware.com:1088/foo", "https", "vmware.com", 1088, "/foo", TRUE);
   TEST_URL("03-Broker-VDM.vdm.int", "http", "03-Broker-VDM.vdm.int", 80, "/", FALSE);
   TEST_URL("cha-address-.ViewPro.com", "http", "cha-address-.ViewPro.com", 80, "/", FALSE);

   g_print("Passed %d%% of %d tests.\n",
           100 * (int)((float)passed / (passed + failed)), passed + failed);

   return failed;

#undef TEST_URL
#undef TEST_URL_FAIL
}


