"""Runs automatically at editor start (every init_unreal.py on the Python path is executed).

Registers the project's MCP toolset (vibegame_tools.py). Never raises: a failure here must not
break editor start-up; it is logged as a LogPython error instead.
"""
import traceback

import unreal

try:
    import vibegame_tools
    from toolset_registry.registration import Registration

    _vibegame_registration = Registration([vibegame_tools.VibeGamePipelineTools])
    if _vibegame_registration.register():
        unreal.log("[vibegame_tools] VibeGamePipelineTools registered")
    else:
        unreal.log_warning("[vibegame_tools] ToolsetRegistry not available; toolset not registered")
except Exception:
    unreal.log_error("[vibegame_tools] registration failed: " + traceback.format_exc())
