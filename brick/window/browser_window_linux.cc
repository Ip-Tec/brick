// Copyright (c) 2015 The Brick Authors.

#include "brick/window/browser_window.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <ctime>

#include "include/base/cef_logging.h"
#include "brick/window_util.h"
#include "brick/platform_util.h"
#include "brick/brick_app.h"


BrowserWindow::BrowserWindow()
    : window_handler_(NULL),
    hided_ (false),
    focused_ (true),
    visible_ (true),
    in_fullscreen_(false),
    resizable_ (true),
    closable_ (true),
    sticked_ (false),
    restore_last_position_ (false),
    last_x_ (0),
    last_y_ (0) {

  auto environment = platform_util::GetDesktopEnvironment();
  on_kde_ = (environment == platform_util::DESKTOP_ENVIRONMENT_KDE);
  on_unity_ = (environment == platform_util::DESKTOP_ENVIRONMENT_UNITY);
}

bool
BrowserWindow::WrapNative(CefWindowHandle window) {
  if (window_handler_) {
    LOG(ERROR) << "Browser window already have native window. Can't wrap one more window.";
    return false;
  }

  XFlush(cef_get_xdisplay());
  window_handler_ = gdk_window_foreign_new(window);
  if (!window_handler_) {
    LOG(ERROR) << "Browser window has been destroyed. Can't wrap.";
    return false;
  }

  AddRef();
  g_object_set_data(G_OBJECT(window_handler_), "wrapper", this);
  gdk_window_set_icon_list(window_handler_, window_util::GetDefaultIcons());
  gdk_window_set_events(window_handler_, (GdkEventMask) (
          GDK_STRUCTURE_MASK|GDK_VISIBILITY_NOTIFY_MASK
  ));
  return true;
}

void
BrowserWindow::Resize(int width, int height) {
  bool need_froze_size = !resizable_;
  if (need_froze_size)
    FrozeSize(0, 0);

  gdk_window_resize(
      window_handler_,
      static_cast<int>(width * window_util::GetDeviceScaleFactor()),
      static_cast<int>(height * window_util::GetDeviceScaleFactor())
  );

  if (need_froze_size)
    FrozeSize(width, height);
}

void
BrowserWindow::SetMinSize(int width, int height) {
  // Deal with HiDPI
  width *= window_util::GetDeviceScaleFactor();
  height *= window_util::GetDeviceScaleFactor();

  GdkGeometry hints;
  hints.min_width = width;
  hints.min_height = height;
  gdk_window_set_geometry_hints(window_handler_, &hints, GDK_HINT_MIN_SIZE);
}

void
BrowserWindow::FrozeSize(int width, int height) {
  GdkGeometry hints = {0};
  bool set_hints = false;
  if (resizable_ && width && height) {
    // Deal with HiDPI
    width *= window_util::GetDeviceScaleFactor();
    height *= window_util::GetDeviceScaleFactor();

    resizable_ = false;
    set_hints = true;
    hints.min_width = width;
    hints.min_height = height;
    hints.max_width = width;
    hints.max_height = height;
  } else if (!resizable_) {
    resizable_ = true;
    set_hints = true;
  }

  if (set_hints) {
    gdk_window_set_geometry_hints(window_handler_, &hints, (GdkWindowHints) (GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));
  }
}

void
BrowserWindow::SetTitle(std::string title) {
  gdk_window_set_title(window_handler_, title.c_str());
}

void
BrowserWindow::Present() {
  Show();
  if (!hided_) {
      // If window mapped - activate it immediately
      SetActive();
  }
}

void
BrowserWindow::Show() {
  bool handled = OnShow();
  if (!handled) {
    gdk_window_show(window_handler_);
    SaveRestorePosition(false);
  }
}

void
BrowserWindow::Hide() {
  bool handled = OnHide();
  if (!handled) {
    SaveRestorePosition(true);
    gdk_window_hide(window_handler_);
  }
}

void
BrowserWindow::Close() {
  gdk_window_destroy(window_handler_);
}

void
BrowserWindow::Popupping() {
  gdk_window_set_type_hint(window_handler_, GDK_WINDOW_TYPE_HINT_DIALOG);
}

void
BrowserWindow::OnNativeEvent(BrowserWindowNativeEvent event) {
  // TODO(buglloc): implement native window event handlers
  switch (event->type) {
    case GDK_DESTROY:
      // Release reference when wrapped window destroy
      Release();
      break;
    case GDK_MAP:
      hided_ = false;
      SetActive();
      break;
    case GDK_UNMAP:
      hided_ = true;
      break;
    case GDK_VISIBILITY_NOTIFY:
      visible_ = (event->visibility.state != GDK_VISIBILITY_FULLY_OBSCURED);
      break;
    default:
//      LOG(INFO) << "window: " << window_handler_ << ", event: " << event->type;
      break;
  }

}

void
BrowserWindow::ToggleVisibility() {
  bool is_hided = (
     hided_
     || !visible_
     || (gdk_window_get_state(window_handler_) & (GDK_WINDOW_STATE_ICONIFIED|GDK_WINDOW_STATE_WITHDRAWN))
  );

  if (is_hided || !IsActive()) {
    Present();
  } else {
    Hide();
  }
}

void
BrowserWindow::SetActive() {
  std::time_t time = 0;
  if (on_kde_) {
    // TODO(buglloc): Ugly hack for deals with KDE focus stealing prevention
    time = std::time(nullptr);
  } else {
    time = gdk_x11_display_get_user_time(gdk_window_get_display(window_handler_));
  }

  gdk_window_focus(window_handler_, time);
}

bool
BrowserWindow::IsActive() {
  GdkWindow *active_window = gdk_screen_get_active_window(
     gdk_window_get_screen(window_handler_)
  );

  if (active_window == NULL)
    return true;

  return (GDK_WINDOW_XID(window_handler_) == GDK_WINDOW_XID(active_window));
}

void
BrowserWindow::SaveRestorePosition(bool save) {
  if (save) {
    GdkRectangle extents;
    gdk_window_get_frame_extents(window_handler_, &extents);
    last_x_ = extents.x;
    last_y_ = extents.y;
    restore_last_position_ = true;
  } else if (
     restore_last_position_
     && last_x_ >= 0 && last_x_ < (gdk_screen_width() - 60)
     && last_y_ >= 0 && last_y_ < (gdk_screen_height() - 60)
     ) {

    restore_last_position_ = false;
    gdk_window_move(window_handler_, last_x_, last_y_);
  }
}

void
BrowserWindow::FlushChanges() {
  gdk_flush();
}

void
BrowserWindow::MoveResize(Position position, int width, int height) {
  // Deal with HiDPI
  width *= window_util::GetDeviceScaleFactor();
  height *= window_util::GetDeviceScaleFactor();

  CefRect screen_rect = GetScreenRect();
  int x = screen_rect.x;
  int y = screen_rect.y;

  switch(position) {
    case Position::NORTH_WEST:
      x += BROWSER_WINDOW_PADDING;
      y += BROWSER_WINDOW_PADDING;
      break;
    case Position::NORTH:
      x += (screen_rect.width - width) / 2;
      y += BROWSER_WINDOW_PADDING;
      break;
    case Position::NORTH_EAST:
      x += screen_rect.width - width - BROWSER_WINDOW_PADDING;
      y += BROWSER_WINDOW_PADDING;
      break;
    case Position::WEST:
      x += BROWSER_WINDOW_PADDING;
      y += (screen_rect.height - height) / 2;
      break;
    case Position::CENTER:
      x += (screen_rect.width - width) / 2;
      y += (screen_rect.height - height) / 2;
      break;
    case Position::EAST:
      x += screen_rect.width - width - BROWSER_WINDOW_PADDING;
      y += (screen_rect.height - height) / 2;
      break;
    case Position::SOUTH_WEST:
      x += BROWSER_WINDOW_PADDING;
      y += screen_rect.height - height - BROWSER_WINDOW_PADDING;
      break;
    case Position::SOUTH:
      x += (screen_rect.width - width) / 2;
      y += screen_rect.height - height - BROWSER_WINDOW_PADDING;
      break;
    case Position::SOUTH_EAST:
      x += screen_rect.width - width - BROWSER_WINDOW_PADDING;
      y += screen_rect.height - height - BROWSER_WINDOW_PADDING;
      break;
    default:
      x += BROWSER_WINDOW_PADDING;
      y += BROWSER_WINDOW_PADDING;
      LOG(WARNING) << "Unknown window position: " << position << ", used NORTH_WEST";
      break;
  }

  // Special (ugly) hack for Unity, because it doesn't properly move "sticked" window
  // TODO(buglloc): Maybe we have better solution?
  bool sticked = sticked_;
  if (on_unity_ && sticked)
    UnStick();

  gdk_window_move_resize(window_handler_, x, y, width, height);

  if (on_unity_ && sticked)
    Stick();
}

void
BrowserWindow::Stick() {
  sticked_ = true;
  gdk_window_set_keep_above(window_handler_, true);
  // Unity have strange issues with _NET_WM_STATE_STICKY, so be very careful
  gdk_window_stick(window_handler_);
}

void
BrowserWindow::UnStick() {
  sticked_ = false;
  gdk_window_set_keep_above(window_handler_, false);
  gdk_window_unstick(window_handler_);
}

CefRect
BrowserWindow::GetScreenRect() {
  GdkRectangle monitor_rect;
  gint monitor_id = gdk_screen_get_monitor_at_window(
     gdk_window_get_screen(window_handler_),
     window_handler_
  );
  gdk_screen_get_monitor_geometry(gdk_window_get_screen(window_handler_), monitor_id, &monitor_rect);

  return CefRect(
     monitor_rect.x,
     monitor_rect.y,
     monitor_rect.width,
     monitor_rect.height
  );
}

void
BrowserWindow::Fullscreen() {
  gdk_window_fullscreen(window_handler_);
  in_fullscreen_ = true;
}

void
BrowserWindow::UnFullscreen() {
  gdk_window_unfullscreen(window_handler_);
  in_fullscreen_ = false;
}

bool
BrowserWindow::IsInFullscreen() {
  return in_fullscreen_;
}
