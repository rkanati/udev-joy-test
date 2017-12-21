
#include <iostream>
#include <system_error>
#include <stdexcept>

#include <unistd.h>
#include <fcntl.h>
#include <libudev.h>
#include <libevdev/libevdev.h>
#include <sys/epoll.h>

template<typename Fn>
class Guard {
  Fn fn;
  bool on_duty = true;
public:
  Guard (Fn fn) : fn (fn) { }
  Guard (Guard const&) = delete;
  Guard (Guard&&) = default;
  ~Guard () { if (on_duty) fn (); }
  void relieve () { on_duty = false; }
};

template<typename Fn>
Guard<Fn> guard (Fn fn) { return { fn }; }

class EvDev {
  libevdev* p = nullptr;

public:
  EvDev () = default;
  explicit EvDev (int fd) {
    int err = libevdev_new_from_fd (fd, &p);
    if (err < 0) throw std::system_error (err, std::system_category ());
  }
  EvDev (EvDev const&) = delete;
  EvDev (EvDev&& other) : p (other.p) { other.p = nullptr; }
  ~EvDev () { if (p) libevdev_free (p); }
  explicit operator bool () const { return p != nullptr; }

  class Iter {
    libevdev* p = nullptr;
    bool resync = false;
    input_event ev;

    friend class EvDev;
    Iter () = default;
    explicit Iter (libevdev* p) : p (p) { pump (); }

    void pump () {
      int flags = resync? LIBEVDEV_READ_FLAG_SYNC : LIBEVDEV_READ_FLAG_NORMAL;
      int res = libevdev_next_event (p, flags, &ev);
      if (res == -EAGAIN) {
        if (resync) { resync = false; pump (); }
        else { p = nullptr; }
      }
      else if (res == LIBEVDEV_READ_STATUS_SYNC) { resync = true; pump (); }
      else if (res < 0) { throw std::system_error (res, std::system_category ()); }
    }

  public:
    bool operator == (Iter const& other) const { return !p && !other.p; }
    bool operator != (Iter const& other) const { return p || other.p; }
    input_event operator * () const { return ev; }
    Iter& operator ++ () { pump (); return *this; }
  };

  Iter begin () const { return Iter (p); }
  Iter end () const { return { }; }

  char const* name () const { return libevdev_get_name (p); }
  int fd () const { return libevdev_get_fd (p); }
};

EvDev open_pad () {
  auto* udev = udev_new ();
  if (!udev) return { };
  auto udev_guard = guard ([udev] { udev_unref (udev); });

  auto* devenum = udev_enumerate_new (udev);
  if (!devenum) return { };
  auto devenum_guard = guard ([devenum] { udev_enumerate_unref (devenum); });

  udev_enumerate_add_match_subsystem (devenum, "input");
  udev_enumerate_add_match_property (devenum, "ID_INPUT_JOYSTICK", "1");
  udev_enumerate_scan_devices (devenum);

  for (auto*
    p = udev_enumerate_get_list_entry (devenum);
    p;
    p = udev_list_entry_get_next (p))
  {
    char const* name = udev_list_entry_get_name (p);

    auto* dev = udev_device_new_from_syspath (udev, name);
    if (!dev) continue;
    auto dev_guard = guard ([dev] { udev_device_unref (dev); });

    char const* node = udev_device_get_devnode (dev);
    if (!node) continue;

    int fd = open (node, O_RDWR | O_NONBLOCK);
    if (fd == -1) continue;
    auto fd_guard = guard ([fd] { close (fd); });

    EvDev evdev (fd);
    fd_guard.relieve ();
    return evdev;
  }

  return { };
}

void handle (input_event const& ev) {
  switch (ev.type) {
    case EV_KEY:
  /*case EV_ABS:
    case EV_REL:*/{
      char const* msg = libevdev_event_code_get_name (ev.type, ev.code);
      std::cout << msg << ": " << ev.value << "\n";
    }

    default:;
  }
}

int main () {
  auto pad = open_pad ();
  if (!pad) {
    std::cout << "No game controllers detected\n";
    return 1;
  }

  std::cout << "Using " << pad.name () << "\n";

  int epoll = epoll_create1 (0);
  epoll_event spec_ev { };
  spec_ev.events = EPOLLIN;
  spec_ev.data.fd = pad.fd ();
  epoll_ctl (epoll, EPOLL_CTL_ADD, spec_ev.data.fd, &spec_ev);

  for (;;) {
    epoll_event ep_ev;
    int n = epoll_wait (epoll, &ep_ev, 1, -1);
    if (n < 1) break;

    if (ep_ev.events & EPOLLHUP) {
      std::cout << "Disconnected\n";
      return 0;
    }
    else if (ep_ev.events & EPOLLERR) {
      std::cout << "Device error\n";
      return 1;
    }
    else if (ep_ev.events & EPOLLIN) {
      for (input_event ev : pad)
        handle (ev);
    }
  }
}

