"""Native app window around the control panel (Edge WebView2 on Windows, WebKit on macOS)."""

from __future__ import annotations

import logging

log = logging.getLogger(__name__)

TITLE = "Listen to the Broadcast"


def run_window(url: str) -> bool:
    """Show the panel in a native window and block until it's closed.

    Returns False without blocking if no native window is available, so the
    caller can fall back to the browser.
    """
    try:
        import webview
    except ImportError:
        return False
    try:
        webview.create_window(TITLE, url, width=1320, height=920, min_size=(420, 560),
                              background_color="#0e1116", text_select=True)
        webview.start()
    except Exception as e:  # e.g. WebView2 runtime missing
        log.warning("native window unavailable: %s", e)
        return False
    return True
