#include "osd.h"

#include "common.h"
#include "monitor.h"

#include <sys/time.h>

#include <png++/image.hpp>
#include <png++/rgba_pixel.hpp>

#include <vdr/videodir.h>

#define DBUSOSDDIR "/tmp/dbus2vdr"
//#define DBUSOSDDIR VideoDirectory


int   cDBusOsd::osd_number = 0;

cDBusOsd::cDBusOsd(int Left, int Top, uint Level)
 : cOsd(Left, Top, Level)
 ,osd_index(osd_number++)
 ,counter(0)
{
  osd_dir = cString::sprintf("%s/dbusosd-%04x", DBUSOSDDIR, osd_index);
  if (!MakeDirs(*osd_dir, true))
     esyslog("dbus2vdr: can't create %s", *osd_dir);
  isyslog("dbus2vdr: new osd %s: %d, %d, %d", *osd_dir, Left, Top, Level);
  DBusMessage *msg = dbus_message_new_signal("/OSD", DBUS_VDR_OSD_INTERFACE, "Open");
  if (msg != NULL) {
     DBusMessageIter args;
     dbus_message_iter_init_append(msg, &args);
     const char *data = *osd_dir;
     if (dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &data)) {
        if (dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &Left)) {
           if (dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &Top)) {
              if (cDBusMonitor::SendSignal(msg))
                 msg = NULL;
              }
           }
        }
     if (msg != NULL)
        dbus_message_unref(msg);
     }
}

cDBusOsd::~cDBusOsd()
{
  DBusMessage *msg = dbus_message_new_signal("/OSD", DBUS_VDR_OSD_INTERFACE, "Close");
  if (msg != NULL) {
     DBusMessageIter args;
     dbus_message_iter_init_append(msg, &args);
     const char *data = *osd_dir;
     if (dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &data)) {
        if (cDBusMonitor::SendSignal(msg))
           msg = NULL;
        }
     if (msg != NULL)
        dbus_message_unref(msg);
     }

  isyslog("dbus2vdr: delete osd %s", *osd_dir);
  RemoveFileOrDir(*osd_dir, false);
}

void cDBusOsd::Flush(void)
{
  if (!Active())
      return;

  struct timeval start;
  struct timeval end;
  struct timezone timeZone;
  gettimeofday(&start, &timeZone);

  bool write = false;
  if (IsTrueColor()) {
    LOCK_PIXMAPS;
    int left = Left();
    int top = Top();
    const cRect* vp;
    int vx, vy, vw, vh;
    int x, y;
    const uint8_t *pixel;
    png::image<png::rgba_pixel> *pngfile;
    while (cPixmapMemory *pm = RenderPixmaps()) {
          write = true;
          vp = &pm->ViewPort();
          vx = vp->X();
          vy = vp->Y();
          vw = vp->Width();
          vh = vp->Height();
          pixel = pm->Data();
          pngfile = new png::image<png::rgba_pixel>(vw, vh);
          for (y = 0; y < vh ; y++) {
              for (x = 0; x < vw; x++) {
                  (*pngfile)[y][x] = png::rgba_pixel(pixel[2], pixel[1], pixel[0], pixel[3]);
                  pixel += 4;
                  }
              }
          cString filename = cString::sprintf("%s/%04x-%d-%d-%d-%d.png", *osd_dir, counter, left, top, vx, vy);
          pngfile->write(*filename);

          DBusMessage *msg = dbus_message_new_signal("/OSD", DBUS_VDR_OSD_INTERFACE, "Display");
          if (msg != NULL) {
             DBusMessageIter args;
             dbus_message_iter_init_append(msg, &args);
             const char *data = *filename;
             if (dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &data)) {
                if (dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &left)) {
                   if (dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &top)) {
                      if (dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &vx)) {
                         if (dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &vy)) {
                            if (cDBusMonitor::SendSignal(msg))
                               msg = NULL;
                            }
                         }
                      }
                   }
                }
             if (msg != NULL)
                dbus_message_unref(msg);
             }

          counter++;
          delete pngfile;
          delete pm;
          }
  }

  if (write) {
     gettimeofday(&end, &timeZone);
     int timeNeeded = end.tv_usec - start.tv_usec;
     timeNeeded += (end.tv_sec - start.tv_sec) * 1000000;
     isyslog("dbus2vdr: flushing osd %d needed %d\n", osd_index, timeNeeded);
     }
}


cDBusOsdProvider::cDBusOsdProvider(void)
{
  isyslog("dbus2vdr: new osd provider");
}

cDBusOsdProvider::~cDBusOsdProvider()
{
  isyslog("dbus2vdr: delete osd provider");
}

cOsd *cDBusOsdProvider::CreateOsd(int Left, int Top, uint Level)
{
  return new cDBusOsd(Left, Top, Level);
}
