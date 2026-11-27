--[[
A fake Resolve for openfx_misc_tests.lua, run offline by test_resolve.py:

  fuscript -l lua -q fake_resolve.lua openfx_misc_tests.lua

It defines the global `resolve` with the API calls the script uses, then runs
the script. ExportFrame writes no file: test_resolve.py writes the PNGs.
]]

local script = (...) or (arg and arg[1])

local function input(tool, id)
  return {
    GetAttrs = function(_)
      return {INPS_ID = id, INPS_Name = id,
              INPS_DataType = (id == "Source") and "Image" or "Number"}
    end,
    GetConnectedOutput = function(_) return tool._conn[id] end,
  }
end

local IDS = {
  ["net.sf.openfx.CheckerBoardPlugin"] = {"boxSize", "color0"},
  ["net.sf.openfx.ConstantPlugin"] = {"colorRed", "colorGreen", "colorBlue",
                                      "colorAlpha"},
  ["net.sf.openfx.Invert"] = {"Source", "processR", "processA"},
}

local function new_tool(plugin)
  local tool = {_plugin = plugin, _values = {}, _conn = {}}
  function tool:GetAttrs() return {TOOLS_RegID = "ofx." .. self._plugin} end
  function tool:GetInputList()
    local list = {}
    for i, id in ipairs(IDS[self._plugin]) do list[i] = input(self, id) end
    return list
  end
  function tool:SetInput(k, v) self._values[k] = v end
  function tool:GetInput(k) return self._values[k] end
  function tool:ConnectInput(k, src) self._conn[k] = src end
  return setmetatable(tool, {__index = function(t, k) return input(t, k) end})
end

local function new_comp()
  local comp = {MediaOut1 = new_tool("MediaOut")}
  function comp:SetActiveTool() end
  function comp:AddTool(id) return new_tool(id:sub(5)) end
  return comp
end

local function new_timeline()
  local timeline = {}
  function timeline:InsertFusionCompositionIntoTimeline()
    return {GetFusionCompByIndex = function(_, _) return new_comp() end}
  end
  function timeline:ExportFrame(_, opts)
    if opts.filePath:sub(-4) ~= ".png" then
      return {success = false, error = "fake: png only"}
    end
    return {success = true, width = 256, height = 256}
  end
  return timeline
end

local function new_project(name)
  local project = {}
  function project:GetName() return name end
  function project:SetSetting(_, _) return true end
  function project:GetMediaPool()
    return {CreateEmptyTimeline = function(_, _) return new_timeline() end}
  end
  function project:SetCurrentTimeline(_) return true end
  return project
end

local manager = {}
function manager:GetCurrentProject() return new_project("user project") end
function manager:CreateProject(name) return new_project(name) end
function manager:CloseProject(_) return true end
function manager:DeleteProject(_) return true end
function manager:LoadProject(name) return new_project(name) end

resolve = {}
function resolve:GetProjectManager() return manager end
function resolve:GetVersionString() return "21.2.0.6" end
function resolve:IsStudio() return false end
function resolve:GetEffectsList(_) return {{category = "ofx", id = "x"}} end

-- fuscript gives each chunk its own environment: run the script in ours,
-- where `resolve` is defined.
local chunk = assert(loadfile(script))
setfenv(chunk, getfenv(1))
chunk()
