"""Opt-in developer logging; released add-ons stay quiet by default."""
import os
import traceback


_previous = None


def enabled():
    return os.environ.get('ENDFIELD_POSER_DEBUG_LOG') == '1'


def exception():
    if enabled():
        configure()
        traceback.print_exc()


def configure():
    global _previous
    if not enabled() or os.name != 'nt':
        return
    import ctypes
    kernel = ctypes.windll.kernel32
    current = kernel.GetConsoleOutputCP()
    # No console (e.g. redirected output): do not allocate one or change the
    # terminal/system locale. Blender's native reports already contain UTF-8.
    if current and current != 65001 and kernel.SetConsoleOutputCP(65001):
        if _previous is None:
            _previous = current


def restore():
    global _previous
    if _previous is not None:
        import ctypes
        kernel = ctypes.windll.kernel32
        if kernel.GetConsoleOutputCP() == 65001:
            kernel.SetConsoleOutputCP(_previous)
        _previous = None
