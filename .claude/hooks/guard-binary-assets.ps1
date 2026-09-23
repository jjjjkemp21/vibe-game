# PreToolUse hook (Edit|Write): blocks text edits to binary Unreal/Blender assets. Exit 2 = block (stderr goes to Claude).
# Windows PowerShell 5.1 compatible. ASCII only.
$ErrorActionPreference = 'Continue'
try {
    $raw = [Console]::In.ReadToEnd()
    $data = $raw | ConvertFrom-Json
} catch { exit 0 }
$path = $null
if ($data -and $data.tool_input -and ($data.tool_input.PSObject.Properties.Name -contains 'file_path')) { $path = [string]$data.tool_input.file_path }
if (-not $path) { exit 0 }
$ext = [IO.Path]::GetExtension($path).ToLowerInvariant()
if (@('.uasset', '.umap', '.blend') -contains $ext) {
    [Console]::Error.WriteLine('BLOCKED: ' + $path + ' is a binary asset. Change Unreal assets through the editor (unreal-mcp, via the editor-operator subagent) or Unreal Python (Content/Python/pipeline_unreal.py); change Blender files through recipes in art/recipes. Never write them as text.')
    exit 2
}
exit 0
