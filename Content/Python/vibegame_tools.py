"""Project toolset for Epic's Unreal MCP server: exposes real editor Python to MCP clients.

Why: Epic's ProgrammaticToolset.execute_tool_script runs in a sandbox (json/math/re/... only, no
`unreal`), so it cannot call pipeline_unreal.py. This toolset is registered at editor start by
Content/Python/init_unreal.py and shows up in list_toolsets as `vibegame_tools.VibeGamePipelineTools`.

Tools (call through unreal-mcp call_tool, toolset_name "vibegame_tools.VibeGamePipelineTools"):
    run_pipeline(function, args_json)  -> reloads pipeline_unreal, calls function(**args), returns JSON
    run_python(code)                   -> exec() editor Python, returns JSON {"ok", "stdout", "error"}
"""
import contextlib
import importlib
import io
import json
import traceback

import unreal

import toolset_registry


def _capture(fn):
    buf = io.StringIO()
    out = {"ok": True, "stdout": "", "result": None, "error": None}
    try:
        with contextlib.redirect_stdout(buf):
            out["result"] = fn()
    except Exception:
        out["ok"] = False
        out["error"] = traceback.format_exc()
        unreal.log_error("[vibegame_tools] " + out["error"])
    out["stdout"] = buf.getvalue()
    return json.dumps(out, default=str)


@unreal.uclass()
class VibeGamePipelineTools(unreal.ToolsetDefinition):
    """VibeGame pipeline tools: run functions from Content/Python/pipeline_unreal.py, or run
    arbitrary editor Python with the full `unreal` API, and get printed output back as JSON."""

    @toolset_registry.tool_call
    @staticmethod
    def run_pipeline(function: str, args_json: str = "{}") -> str:
        """Reloads pipeline_unreal and calls one of its functions with keyword arguments.

        Args:
            function: Name of a function in pipeline_unreal.py (e.g. 'import_static_mesh').
            args_json: JSON object of keyword arguments, e.g. '{"src_path": "C:/x.fbx"}'.

        Returns:
            JSON string {"ok": bool, "result": <return value>, "stdout": str, "error": str|null}.
        """
        def call():
            import pipeline_unreal
            importlib.reload(pipeline_unreal)
            return getattr(pipeline_unreal, function)(**json.loads(args_json or "{}"))
        return _capture(call)

    @toolset_registry.tool_call
    @staticmethod
    def run_python(code: str) -> str:
        """Executes Python code in the editor with the full `unreal` module available.

        Assign to a variable named `result` to return a value; print() output is captured.

        Args:
            code: Python source code to execute.

        Returns:
            JSON string {"ok": bool, "result": <value of `result`>, "stdout": str, "error": str|null}.
        """
        def call():
            scope = {"__name__": "__vibegame_run_python__", "unreal": unreal}
            exec(compile(code, "<run_python>", "exec"), scope)
            return scope.get("result")
        return _capture(call)
