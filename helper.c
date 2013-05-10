#include "helper.h"
#include "bus.h"

#include <dbus/dbus.h>
#include <vdr/plugin.h>
#include <sys/wait.h>
#include <unistd.h>


cString  cDBusHelper::_pluginConfigDir;

bool cDBusHelper::IsValidUtf8(const char *text)
{
  int na = 0;
  int nb = 0;
  const char *c;
  
  for (c = text; *c; c += (nb + 1)) {
      if ((*c & 0x80) == 0x00)
         nb = 0;
      else if ((*c & 0xc0) == 0x80)
         return false;
      else if ((*c & 0xe0) == 0xc0)
         nb = 1;
      else if ((*c & 0xf0) == 0xe0)
         nb = 2;
      else if ((*c & 0xf8) == 0xf0)
         nb = 3;
      else if ((*c & 0xfc) == 0xf8)
         nb = 4;
      else if ((*c & 0xfe) == 0xfc)
         nb = 5;
      for (na = 1; *(c + na) && (na <= nb); na++) {
          if ((*(c + na) & 0xc0) != 0x80)
             return false;
          }
      }
  return true;
}

void cDBusHelper::ToUtf8(cString &text)
{
  if ((cCharSetConv::SystemCharacterTable() == NULL) || (strcmp(cCharSetConv::SystemCharacterTable(), "UTF-8") == 0)) {
     if (!cDBusHelper::IsValidUtf8(*text)) {
        static const char *fromCharSet = "ISO6937";
        static const char *charSetOverride = getenv("VDR_CHARSET_OVERRIDE");
        if (charSetOverride)
           fromCharSet = charSetOverride;
        dsyslog("dbus2vdr: ToUtf8: create charset converter from %s to UTF-8", fromCharSet);
        static cCharSetConv converter(fromCharSet);
        static cMutex mutex;
        mutex.Lock();
        text = converter.Convert(*text);
        mutex.Unlock();
        }
     }
  else {
     dsyslog("dbus2vdr: ToUtf8: create charset converter to UTF-8");
     static cCharSetConv converter(NULL, "UTF-8");
     static cMutex mutex;
     mutex.Lock();
     text = converter.Convert(*text);
     mutex.Unlock();
     }
}

void  cDBusHelper::AddKeyValue(GVariantBuilder *Array, const char *Key, const gchar *Type, void **Value)
{
  GVariantBuilder *element = g_variant_builder_new(G_VARIANT_TYPE("(sv)"));

  g_variant_builder_add(element, "s", Key);

  GVariantBuilder *variant = g_variant_builder_new(G_VARIANT_TYPE("v"));
  g_variant_builder_add(variant, Type, *Value);
  g_variant_builder_add_value(element, g_variant_builder_end(variant));

  g_variant_builder_add_value(Array, g_variant_builder_end(element));

  g_variant_builder_unref(variant);
  g_variant_builder_unref(element);
}

void  cDBusHelper::SendReply(GDBusMethodInvocation *Invocation, int  ReplyCode, const char *ReplyMessage)
{
  GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("(is)"));
  g_variant_builder_add(builder, "i", ReplyCode);
  g_variant_builder_add(builder, "s", ReplyMessage);
  g_dbus_method_invocation_return_value(Invocation, g_variant_builder_end(builder));
  g_variant_builder_unref(builder);
}

cDBusTcpAddress *cDBusHelper::GetNetworkAddress(void)
{
  cString filename = cString::sprintf("%s/network-address.conf", *_pluginConfigDir);
  dsyslog("dbus2vdr: loading network address from file %s", *filename);
  FILE *f = fopen(*filename, "r");
  if (f == NULL)
     return NULL;
  cReadLine r;
  char *line = r.Read(f);
  fclose(f);
  if (line == NULL)
     return NULL;
  DBusError err;
  dbus_error_init(&err);
  DBusAddressEntry **addresses = NULL;
  int len = 0;
  if (!dbus_parse_address(line, &addresses, &len, &err)) {
     esyslog("dbus2vdr: errorparsing address %s: %s", line, err.message);
     dbus_error_free(&err);
     return NULL;
     }
  cDBusTcpAddress *address = NULL;
  for (int i = 0; i < len; i++) {
      if (addresses[i] != NULL) {
         if (strcmp(dbus_address_entry_get_method(addresses[i]), "tcp") == 0) {
            const char *host = dbus_address_entry_get_value(addresses[i], "host");
            const char *port = dbus_address_entry_get_value(addresses[i], "port");
            if ((host != NULL) && (port != NULL) && isnumber(port)) {
               address = new cDBusTcpAddress(host, atoi(port));
               break;
               }
            }
         }
      }
  dbus_address_entries_free(addresses);
  return address;
}


cExitPipe::cExitPipe(void)
{
  pid = -1;
  f = NULL;
}

cExitPipe::~cExitPipe()
{
  Close();
}

bool cExitPipe::Open(const char *Command, const char *Mode)
{
  int fd[2];

  if (pipe(fd) < 0) {
     LOG_ERROR;
     return false;
     }
  if ((pid = fork()) < 0) { // fork failed
     LOG_ERROR;
     close(fd[0]);
     close(fd[1]);
     return false;
     }

  const char *mode = "w";
  int iopipe = 0;

  if (pid > 0) { // parent process
     if (strcmp(Mode, "r") == 0) {
        mode = "r";
        iopipe = 1;
        }
     close(fd[iopipe]);
     if ((f = fdopen(fd[1 - iopipe], mode)) == NULL) {
        LOG_ERROR;
        close(fd[1 - iopipe]);
        }
     return f != NULL;
     }
  else { // child process
     int iofd = STDOUT_FILENO;
     if (strcmp(Mode, "w") == 0) {
        mode = "r";
        iopipe = 1;
        iofd = STDIN_FILENO;
        }
     close(fd[iopipe]);
     if (dup2(fd[1 - iopipe], iofd) == -1) { // now redirect
        LOG_ERROR;
        close(fd[1 - iopipe]);
        _exit(-1);
        }
     else {
        int MaxPossibleFileDescriptors = getdtablesize();
        for (int i = STDERR_FILENO + 1; i < MaxPossibleFileDescriptors; i++)
            close(i); //close all dup'ed filedescriptors
        if (execl("/bin/sh", "sh", "-c", Command, NULL) == -1) {
           LOG_ERROR_STR(Command);
           close(fd[1 - iopipe]);
           _exit(-1);
           }
        }
     _exit(0);
     }
}

int cExitPipe::Close(void)
{
  int ret = -1;

  if (f) {
     fclose(f);
     f = NULL;
     }

  if (pid > 0) {
     int status = 0;
     int i = 5;
     while (i > 0) {
           ret = waitpid(pid, &status, WNOHANG);
           if (ret < 0) {
              if (errno != EINTR && errno != ECHILD) {
                 LOG_ERROR;
                 break;
                 }
              }
           else if (ret == pid)
              break;
           i--;
           cCondWait::SleepMs(100);
           }
     if (!i) {
        kill(pid, SIGKILL);
        ret = -1;
        }
     else if (ret == -1 || !WIFEXITED(status))
        ret = -1;
     else if (WIFEXITED(status))
        ret = WEXITSTATUS(status);
     pid = -1;
     }

  return ret;
}
