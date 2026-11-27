--[[
openfx-misc tests, run inside DaVinci Resolve (Workspace > Scripts > Utility).

Written for DaVinci Resolve 21.2.0 (free). Its Scripts menu lists only Lua
scripts, and their Lua has no `io` and no `os.execute`, so this script writes
no files itself:
- it creates a scratch project (openfx-misc-tests-<stamp>);
- it renders each case of CASES in a Fusion composition clip on its own
  timeline, and exports frame 0 with Timeline:ExportFrame to
  <results root>/<stamp>-<case>.<ext>;
- it prints everything it found (Fusion tool and input IDs, what was set,
  the export results) as one JSON line between OPENFX-MISC-RESULTS-BEGIN
  and OPENFX-MISC-RESULTS-END on stdout, which goes to resolve-stdout.log
  when Resolve is started with the README's command (print() does not reach
  Workspace > Console);
- it deletes the scratch project, reopens the project that was open, and
  never quits Resolve.

The results root is $OPENFX_MISC_RESOLVE_RESULTS, or else
~/openfx-misc-resolve-results (install-script.sh creates it). After quitting
Resolve, `pytest tests/resolve` reads the newest results block and checks it
(see README.md).
]]

local CHECKERBOARD = "net.sf.openfx.CheckerBoardPlugin"
local CONSTANT = "net.sf.openfx.ConstantPlugin"
local INVERT = "net.sf.openfx.Invert"
local SIZE = {256, 256}
local BEGIN_MARK = "OPENFX-MISC-RESULTS-BEGIN"
local END_MARK = "OPENFX-MISC-RESULTS-END"

-- Each case: a chain of tools, first to last, the last one connected to
-- MediaOut1. "params" are OFX param names and values; the script finds the
-- Fusion inputs for them (see set_param). "checks" are for test_resolve.py:
-- {x, y, {r, g, b}} in exported-frame pixels (y = 0 is the top row).
--
-- CheckerBoard defaults: 64-pixel boxes from the centre (128, 128). In OFX
-- coordinates (y up), the box right of and above the centre is color0 (0.1),
-- left of it color1 (0.5), below-right color3 (0.5), below-left color2 (0.1).
local CASES = {
  {
    name = "checkerboard_defaults",
    tools = {{plugin = CHECKERBOARD, params = {}}},
    checks = {{160, 96, {0.1, 0.1, 0.1}}, {96, 96, {0.5, 0.5, 0.5}},
              {160, 160, {0.5, 0.5, 0.5}}, {96, 160, {0.1, 0.1, 0.1}}},
    formats = {"png", "tif", "exr"},
  },
  {
    name = "constant_color",
    tools = {{plugin = CONSTANT,
              params = {{"color", {0.25, 0.5, 0.75, 1.0}}}}},
    checks = {{128, 128, {0.25, 0.5, 0.75}}, {10, 245, {0.25, 0.5, 0.75}}},
    formats = {"png"},
  },
  {
    name = "invert_checkerboard",
    tools = {{plugin = CHECKERBOARD, params = {}},
             {plugin = INVERT, params = {{"processA", false}}}},
    checks = {{160, 96, {0.9, 0.9, 0.9}}, {96, 96, {0.5, 0.5, 0.5}}},
    formats = {"png"},
  },
}

-- Minimal JSON encoder: nil, booleans, finite numbers, strings, and tables
-- (a table with keys 1..n is an array, anything else an object with sorted
-- string keys). Other values (userdata, functions, NaN) become strings or null.
local encode

local function encode_string(s)
  s = s:gsub('[%c"\\]', function(c)
    local named = {['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n',
                   ['\r'] = '\\r', ['\t'] = '\\t'}
    return named[c] or string.format("\\u%04x", c:byte())
  end)
  return '"' .. s .. '"'
end

local function is_array(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  for i = 1, n do
    if t[i] == nil then return false end
  end
  return true, n
end

encode = function(v)
  local kind = type(v)
  if v == nil then
    return "null"
  elseif kind == "boolean" then
    return v and "true" or "false"
  elseif kind == "number" then
    if v ~= v or v == math.huge or v == -math.huge then return "null" end
    if v == math.floor(v) and math.abs(v) < 1e15 then
      return string.format("%d", v)
    end
    return string.format("%.14g", v)
  elseif kind == "string" then
    return encode_string(v)
  elseif kind == "table" then
    local array, n = is_array(v)
    local parts = {}
    if array and n > 0 then
      for i = 1, n do parts[i] = encode(v[i]) end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = tostring(k) end
    table.sort(keys)
    local lookup = {}
    for k, val in pairs(v) do lookup[tostring(k)] = val end
    for _, k in ipairs(keys) do
      parts[#parts + 1] = encode_string(k) .. ":" .. encode(lookup[k])
    end
    return "{" .. table.concat(parts, ",") .. "}"
  end
  return encode_string(tostring(v))
end

local function log(message)
  print("openfx-misc tests: " .. message)
end

local function stamp()
  local ok, s = pcall(os.date, "%Y%m%d-%H%M%S")
  if ok and s then return s end
  return string.format("run%06d", math.random(0, 999999))
end

local function results_root()
  return os.getenv("OPENFX_MISC_RESOLVE_RESULTS")
      or (os.getenv("HOME") .. "/openfx-misc-resolve-results")
end

local ATTRS = {"INPS_ID", "INPS_Name", "INPS_DataType", "INPN_MinAllowed",
               "INPN_MaxAllowed", "INPN_MinScale", "INPN_MaxScale"}

-- The tool's inputs in panel order, as tables of their attributes.
local function inputs_of(tool)
  local list = tool:GetInputList() or {}
  local keys = {}
  for k in pairs(list) do keys[#keys + 1] = k end
  table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
  if #keys > 0 and type(keys[1]) == "number" then table.sort(keys) end
  local inputs = {}
  for _, k in ipairs(keys) do
    local attrs = list[k]:GetAttrs() or {}
    local entry = {}
    for _, a in ipairs(ATTRS) do entry[a] = attrs[a] end
    inputs[#inputs + 1] = entry
  end
  return inputs
end

local function starts_with(s, prefix)
  return s:sub(1, #prefix) == prefix
end

-- Sets an OFX param through the tool's Fusion input(s). A scalar goes to the
-- input whose ID is the param name. A table goes to the inputs whose IDs start
-- with the name (e.g. colorRed, colorGreen...), in panel order, if there are
-- as many. Returns the input IDs used and the values read back.
local function set_param(tool, inputs, name, value)
  local ids = {}
  for _, i in ipairs(inputs) do
    if i.INPS_ID then ids[#ids + 1] = i.INPS_ID end
  end
  if type(value) == "boolean" then value = value and 1 or 0 end
  if type(value) == "table" then
    local parts = {}
    for _, id in ipairs(ids) do
      if starts_with(id, name) and id ~= name then parts[#parts + 1] = id end
    end
    if #parts == #value then
      local read = {}
      for i, id in ipairs(parts) do
        tool:SetInput(id, value[i])
        read[i] = tool:GetInput(id)
      end
      return {inputs = parts, read = read}
    end
  end
  for _, id in ipairs(ids) do
    if id == name then
      tool:SetInput(name, value)
      return {inputs = {name}, read = {tool:GetInput(name)}}
    end
  end
  return {inputs = {}, error = "no input " .. name}
end

-- The ID of the main image input: Source if there is one.
local function image_input(inputs)
  local first
  for _, i in ipairs(inputs) do
    if i.INPS_DataType == "Image" then
      if i.INPS_ID == "Source" then return "Source" end
      first = first or i.INPS_ID
    end
  end
  return first
end

local function run_case(project, case, prefix)
  local result = {name = case.name, checks = case.checks, tools = {},
                  frames = {}}
  local timeline = project:GetMediaPool():CreateEmptyTimeline(case.name)
  if not timeline or not project:SetCurrentTimeline(timeline) then
    result.error = "cannot create a timeline"
    return result
  end
  local item = timeline:InsertFusionCompositionIntoTimeline()
  local comp = item and item:GetFusionCompByIndex(1)
  if not comp then
    result.error = "cannot insert a Fusion composition"
    return result
  end
  local previous
  for _, spec in ipairs(case.tools) do
    local entry = {plugin = spec.plugin}
    result.tools[#result.tools + 1] = entry
    comp:SetActiveTool()  -- nothing selected: AddTool connects nothing
    local tool = comp:AddTool("ofx." .. spec.plugin)
    if not tool then
      result.error = "no Fusion tool ofx." .. spec.plugin
      return result
    end
    entry.reg_id = (tool:GetAttrs() or {}).TOOLS_RegID
    local inputs = inputs_of(tool)
    entry.inputs = inputs
    if previous then
      local source = image_input(inputs)
      entry.source_input = source
      if source then tool:ConnectInput(source, previous) end
      if not source or not tool[source]:GetConnectedOutput() then
        result.error = "cannot connect " .. tostring(source) .. " of "
            .. spec.plugin
        return result
      end
    end
    entry.set = {}
    for _, p in ipairs(spec.params) do
      entry.set[p[1]] = set_param(tool, inputs, p[1], p[2])
    end
    previous = tool
  end
  comp.MediaOut1:ConnectInput("Input", previous)
  for _, fmt in ipairs(case.formats) do
    local path = prefix .. "-" .. case.name .. "." .. fmt
    local frame = timeline:ExportFrame(0, {outputType = "file",
                                           filePath = path}) or {}
    result.frames[fmt] = {path = path, success = frame.success,
                          error = frame.error, width = frame.width,
                          height = frame.height}
    log(case.name .. ": " .. fmt .. " " .. tostring(frame.success)
        .. " " .. tostring(frame.error))
  end
  return result
end

local function main(resolve)
  local run = stamp()
  local root = results_root()
  local manager = resolve:GetProjectManager()
  local current = manager:GetCurrentProject()
  local previous_name = current and current:GetName()
  local name = "openfx-misc-tests-" .. run
  local effects = {}
  for _, e in ipairs(resolve:GetEffectsList("video") or {}) do
    if e.category == "ofx" then effects[#effects + 1] = e.id end
  end
  table.sort(effects)
  local results = {
    stamp = run,
    root = root,
    resolve_version = resolve:GetVersionString(),
    studio = resolve:IsStudio(),
    lua = _VERSION,
    ofx_plugin_path = os.getenv("OFX_PLUGIN_PATH"),
    size = SIZE,
    ofx_effects = effects,
    cases = {},
  }
  log("starting run " .. run)
  local project = manager:CreateProject(name)
  if not project then
    results.error = "cannot create project " .. name
  else
    results.settings = {
      colorScienceMode = project:SetSetting("colorScienceMode", "davinciYRGB"),
      timelineResolutionWidth = project:SetSetting(
          "timelineResolutionWidth", tostring(SIZE[1])),
      timelineResolutionHeight = project:SetSetting(
          "timelineResolutionHeight", tostring(SIZE[2])),
    }
    for _, case in ipairs(CASES) do
      local ok, result = pcall(run_case, project, case, root .. "/" .. run)
      if not ok then
        result = {name = case.name, checks = case.checks,
                  error = tostring(result)}
      end
      results.cases[#results.cases + 1] = result
    end
    manager:CloseProject(project)
    results.project_deleted = manager:DeleteProject(name)
    if previous_name then
      results.previous_project_reloaded =
          manager:LoadProject(previous_name) and true or false
    end
  end
  print(BEGIN_MARK .. " " .. run)
  print(encode(results))
  print(END_MARK .. " " .. run)
  log("done (run " .. run .. "): quit Resolve normally, then run "
      .. "`pytest tests/resolve` in the openfx-misc checkout")
end

-- Resolve defines `resolve` (and Resolve()) for scripts run from its menus;
-- fake_resolve.lua defines a fake one. OPENFX_MISC_NO_MAIN skips the run.
if not os.getenv("OPENFX_MISC_NO_MAIN") then
  local app = resolve or (Resolve and Resolve())
  if app then
    main(app)
  else
    print("openfx-misc tests: no Resolve object; run this from Resolve's "
          .. "Workspace > Scripts menu")
  end
end

return {encode = encode, CASES = CASES, main = main}
