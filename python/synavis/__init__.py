# Synavis Python package initializer
# Expose the compiled PySynavis extension as `rtc` for backwards compatibility with existing modules
try:
    from . import PySynavis as rtc
except Exception:
    # If the compiled extension is not available yet, provide a helpful import-time error
    raise ImportError("PySynavis extension module not found. Make sure the native extension is built and installed.")

__all__ = ["rtc"]
