"""Headless entry point used by tools/unreal-python.ps1.

Environment: PIPELINE_FUNCTION = name of a function in pipeline_unreal.py, PIPELINE_ARGS = JSON object of kwargs.
Prints RESULT_JSON:<json> on success, PIPELINE_FAILED plus a LogPython error on failure.
"""
import json
import os
import traceback

import unreal

import pipeline_unreal

fn_name = os.environ.get("PIPELINE_FUNCTION", "").strip()
raw_args = os.environ.get("PIPELINE_ARGS", "") or "{}"
try:
    if not fn_name:
        raise ValueError("PIPELINE_FUNCTION is not set")
    fn = getattr(pipeline_unreal, fn_name)
    result = fn(**json.loads(raw_args))
    print("RESULT_JSON:" + json.dumps(result, default=str))
except Exception:
    unreal.log_error("PIPELINE_ERROR: " + traceback.format_exc())
    print("PIPELINE_FAILED")
