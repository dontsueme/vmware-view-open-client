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
 * procHelper.cc --
 *
 *    Child process helper.
 */


#include <sys/types.h>
#include <unistd.h>     /* For read/write/close */

// XXX - use autoconf HAVE_XXX instead of MINGW32
#ifndef __MINGW32__
#include <sys/socket.h> /* For socketpair */
#include <sys/wait.h>   /* For waitpid */
#endif
#include <signal.h>     /* For kill */
#include <glib.h>


#include "procHelper.hh"

extern "C" {
#include "vm_assert.h"
#include "err.h"
#include "log.h"
#include "poll.h"
#include "posix.h"
#include "util.h" /* For DIRSEPS */
}


namespace cdk {


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::ProcHelper --
 *
 *      Constructor.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ProcHelper::ProcHelper()
   : mPid((GPid)-1),
#ifdef __MINGW32__
     mSourceId(0),
#endif
     mErrFd(-1)
{
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::~ProcHelper --
 *
 *      Destructor.  Calls Kill.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

ProcHelper::~ProcHelper()
{
   Kill();

#ifdef __MINGW32__
   if (mSourceId) {
      g_source_remove(mSourceId);
      mSourceId = 0;
   }
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::GetIsInPath --
 *
 *      Predicate to determine if executable program is in the
 *      effective path.
 *
 * Results:
 *      true if input program found in path, false otherwise.
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

bool
ProcHelper::GetIsInPath(const Util::string &programName) // IN
{
    char *fullyQualifiedProgramName = g_find_program_in_path(programName.c_str());
    g_free(fullyQualifiedProgramName);
    return fullyQualifiedProgramName != NULL;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::Start --
 *
 *      Fork and exec child process.  procName is the friendly name of the
 *      process, to be used in Log messages.  procPath is the path to the
 *      binary, passed to Posix_Execvp.
 *
 *      In the case of VIEW_GTK, screen should be a GdkScreen * or NULL.
 *
 *      The stdIn string is written all at once to the process's stdin.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Creates two pipes for stdio, and forks a child.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelper::Start(Util::string procName,          // IN
                  Util::string procPath,          // IN
                  std::vector<Util::string> args, // IN/OPT
                  int argsCensorMask,             // IN/OPT
                  gpointer screen,                // IN/OPT
                  Util::string stdIn)             // IN/OPT
{
   ASSERT(mPid == (GPid)-1);
   ASSERT(!procPath.empty());
   ASSERT(!procName.empty());

   Util::string cmd = procPath;
   for (unsigned int i = 0; i < args.size(); i++) {
      cmd += " '";
      if (i <= 8 * sizeof(argsCensorMask) && ((1 << i) & argsCensorMask)) {
         cmd += "[omitted]";
      } else {
         cmd += args[i];
      }
      cmd += "'";
   }
   Log("Starting child: %s\n", cmd.c_str());

   char **argList = (char **)g_new0(char *, args.size() + 2);
   int argIdx = 0;

   argList[argIdx++] = (char *)procPath.c_str();
   for (std::vector<Util::string>::iterator i = args.begin();
        i != args.end(); i++) {
      argList[argIdx++] = (char *)i->c_str();
   }
   argList[argIdx++] = NULL;

   GPid gpid = (GPid)-1;
   pid_t pid = -1;
   int childIn = -1;
   int childErr = -1;
   GError *err = NULL;
   bool spawned;
   GSpawnFlags flags = (GSpawnFlags)(G_SPAWN_SEARCH_PATH |
                                     G_SPAWN_DO_NOT_REAP_CHILD);

#ifdef VIEW_GTK
   if (GDK_IS_SCREEN(screen)) {
      spawned = gdk_spawn_on_screen_with_pipes(GDK_SCREEN(screen), NULL,
                                               argList, NULL, flags, NULL,
                                               NULL, &pid, &childIn, NULL,
                                               &childErr, &err);
   } else
#endif
   {
      spawned = g_spawn_async_with_pipes(NULL, argList, NULL,
                                         flags, NULL, NULL, &gpid,
                                         &childIn, NULL, &childErr, &err);
   }

   if (!spawned) {
      Warning("Spawn of %s failed: %s\n", procName.c_str(), err->message);
      g_error_free(err);
   } else {
      if (!stdIn.empty()) {
         write(childIn, stdIn.c_str(), stdIn.size());
      }
      close(childIn);

      mProcName = procName;
      mPid = (pid != -1) ? (GPid)pid : gpid;
      mErrFd = childErr;

#ifdef __MINGW32__
      mSourceId = g_child_watch_add(mPid, ProcHelper::OnProcExit, this);
      /*
       * We use periodic polling on mingw32, rather than one shot polling,
       * because Poll's GIOChannel logic misbehaves when a channel is
       * recreated for a file descriptor which was already associated
       * with a channel.
       */
      Poll_CB_Device_With_Flags(&OnErr, this, mErrFd, true, POLL_FLAG_FD);
#else
      Poll_CB_Device(&OnErr, this, mErrFd, false);
#endif
   }

   g_free(argList);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::Kill --
 *
 *      Kill the child process, if running.  If mPid is set, send it a SIGTERM.
 *      If mErrFd is set, close it and remove the poll callback.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Sends SIGTERM to child.  Emits onExit if the child has exited.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelper::Kill()
{
   if (mErrFd > -1) {
      Poll_CB_DeviceRemove(&ProcHelper::OnErr, this, false);
#ifndef __MINGW32__
      /*
       * We're not allowed to manipulate a file descriptor managed by
       * a GIOChannel so we don't close it here and instead rely on
       * Poll_CB_DeviceRemove to shut it down for us.
       */
      close(mErrFd);
#endif
      mErrFd = -1;
   }

   if ((int)mPid < 0) {
      return;
   }

   int status;
#ifdef _WIN32
   DWORD exitCode;
   if (!TerminateProcess(mPid, 0)) {
      Log("Unable to terminate process '%s' (%lu)\n", mProcName.c_str(), mPid);
   }
   if (!GetExitCodeProcess(mPid, &exitCode)) {
      exitCode = ~0L;
   }
   g_spawn_close_pid(mPid);
   status = (int)exitCode;
#else
   if (kill(mPid, SIGTERM) && errno != ESRCH) {
      Log("Unable to kill %s(%d): %s\n", mProcName.c_str(), mPid,
          Err_ErrString());
   }
   GPid rv;
   do {
      rv = waitpid(mPid, &status, 0);
   } while (rv < 0 && EINTR == errno);

   if (rv < 0) {
      Log("Unable to waitpid on %s(%d): %s\n", mProcName.c_str(), mPid,
          Err_ErrString());
   } else if (rv == mPid) {
      if (WIFEXITED(status)) {
         if (WEXITSTATUS(status)) {
            Warning("%s(%d) exited with status: %d\n",
                    mProcName.c_str(), mPid, WEXITSTATUS(status));
         } else {
            Warning("%s(%d) exited normally.\n", mProcName.c_str(), mPid);
         }
      } else {
         Warning("%s(%d) exited due to signal %d.\n",
                 mProcName.c_str(), mPid, WTERMSIG(status));
      }
   } else {
      // It wasn't a normal exit, but what value should we use?
      status = 0xff00;
   }
#endif
   mPid = (GPid)-1;
   onExit(status);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::OnErr --
 *
 *      Stderr poll callback for the child process. Reads and logs the output,
 *      and passes the line to the onErr signal.  Keeps a line buffer for
 *      appending partial reads so that we only log/emit full lines.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Calls Poll_CB_Device to schedule more IO.
 *
 *-----------------------------------------------------------------------------
 */

void
ProcHelper::OnErr(void *data) // IN: this
{
#ifdef __MINGW32__
   void **cbData = reinterpret_cast<void **>(data);
   ProcHelper *that = reinterpret_cast<ProcHelper *>(cbData[0]);
   GIOChannel *channel = reinterpret_cast<GIOChannel *>(cbData[1]);
#else
   ProcHelper *that = reinterpret_cast<ProcHelper*>(data);
#endif
   ASSERT(that);

   if (that->mErrFd == -1) {
      return;
   }

   char buf[1024];
   int count;

#ifdef __MINGW32__
   if (channel) {
      GIOError err;
      gsize n;
      /*
       * If a channel is associated with this event then we must use
       * it to read from the underlying device (see http://library.gnome.org/
       * devel/glib/stable/glib-IO-Channels.html#g-io-channel-win32-new-fd
       * for details.)
       */
      err = g_io_channel_read(channel, buf, sizeof(buf) - 1, &n);
      if (err != G_IO_ERROR_NONE) {
         count = -1;
      } else {
         count = (int)n;
      }
   } else {
      count = read(that->mErrFd, buf, sizeof(buf) - 1);
   }
#else
   count = read(that->mErrFd, buf, sizeof(buf) - 1);
#endif

   if (count <= 0) {
      Warning("%s(%d) died.\n", that->mProcName.c_str(), that->mPid);
      that->Kill();
      return;
   }

   buf[count] = 0;
   char *line = buf;

   /*
    * Look for full/unterminated lines in the read buffer. For full lines,
    * replace \n with \0 and log/emit the line minus the terminator.  For a
    * trailing unterminated line, store it as the prefix for future reads.
    */
   while (*line) {
      char *newline = strchr(line, '\n');
      if (!newline) {
         that->mErrPartialLine += line;
         break;
      }

      *newline = 0; // Terminate line
#ifdef __MINGW32__
      *(newline - 1) = 0; // Stomp the '\r' which preceeds '\n' in Windows.
#endif
      Util::string fullLine = that->mErrPartialLine + line;
      that->mErrPartialLine = "";
      line = &newline[1];

      Warning("%s(%d): %s\n", that->mProcName.c_str(), that->mPid,
              fullLine.c_str());

      that->onErr(fullLine);
   }

#ifndef __MINGW32__
   /*
    * No need to add to Poll again because we poll periodically and
    * are therefore not removed by the poller once the event has fired.
    */
   Poll_CB_Device(&ProcHelper::OnErr, that, that->mErrFd, false);
#endif
}


/*
 *-----------------------------------------------------------------------------
 *
 * cdk::ProcHelper::OnProcExit --
 *
 *      Handle process termination on Windows/MingW32.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *-----------------------------------------------------------------------------
 */

#ifdef __MINGW32__
void
ProcHelper::OnProcExit(GPid pid,      // IN/UNUSED
                       int status,    // IN/UNUSED
                       gpointer data) // IN
{
   ProcHelper *helper = reinterpret_cast<ProcHelper *>(data);
   ASSERT(helper);
   helper->Kill();
}
#endif


} // namespace cdk
