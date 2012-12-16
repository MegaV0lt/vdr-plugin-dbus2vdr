#ifndef __DBUS2VDR_BUS_H
#define __DBUS2VDR_BUS_H

#include <dbus/dbus.h>

#include <vdr/tools.h>

// abstraction of a DBus message bus

class cDBusBus
{
private:
  cString          _name;
  cString          _busname;
  DBusConnection  *_conn;

protected:
  DBusError  _err;

  virtual DBusConnection *GetConnection(void) = 0;

public:
  cDBusBus(const char *name, const char *busname);
  virtual ~cDBusBus(void);

  const char *Name(void) const { return *_name; }
  const char *Busname(void) const { return *_busname; }

  DBusConnection*  Connect(void);

  virtual void  Disconnect(void);
};

class cDBusSystemBus : public cDBusBus
{
protected:
  virtual DBusConnection *GetConnection(void);

public:
  cDBusSystemBus(const char *busname);
  virtual ~cDBusSystemBus(void);
};

class cDBusTcpAddress
{
private:
  cString _address;

public:
  const cString Host;
  const int     Port;

  cDBusTcpAddress(const char *host, int port)
   :Host(host),Port(port) {}

  const char *Address(void)
  {
    if (*_address == NULL) {
       char *host = dbus_address_escape_value(*Host);
       _address = cString::sprintf("tcp:host=%s,port=%d", host, Port);
       dbus_free(host);
       }
    return *_address;
  }
};

class cDBusCustomBus : public cDBusBus
{
private:
  cDBusTcpAddress  *_address;

protected:
  virtual DBusConnection *GetConnection(void);

public:
  cDBusCustomBus(const char *busname, cDBusTcpAddress *address);
  virtual ~cDBusCustomBus(void);
};

#endif
