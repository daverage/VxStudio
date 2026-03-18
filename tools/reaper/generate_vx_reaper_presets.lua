local root = "/Users/andrzejmarczewski/Documents/GitHub/VxStudio"
local output_root = root .. "/assets/reaper"
local rpl_dir = output_root .. "/RPL Files"
local fxchain_dir = output_root .. "/FX Chains"
local report_path = output_root .. "/generation-report.txt"
local bootstrap_path = output_root .. "/bootstrap.txt"

os.execute(string.format("mkdir -p %q %q", rpl_dir, fxchain_dir))
do
  local f = io.open(bootstrap_path, "wb")
  if f then
    f:write("bootstrap\n")
    f:close()
  end
end

local plugins = {
  cleanup = {
    display_name = "VXCleanup (VX Suite)",
    library_name = "VST3: VXCleanup (VX Suite)",
    file_stem = "VXCleanup",
    binary = "VXCleanup.vst3",
    unique_id = 588610322,
    class_id = "ABCDEF019182FAEB565853315658434C",
  },
  deepfilternet = {
    display_name = "VXDeepFilterNet (VX Suite)",
    library_name = "VST3: VXDeepFilterNet (VX Suite)",
    file_stem = "VXDeepFilterNet",
    binary = "VXDeepFilterNet.vst3",
    unique_id = 1795708663,
    class_id = "ABCDEF019182FAEB5658533156584446",
  },
  denoiser = {
    display_name = "VXDenoiser (VX Suite)",
    library_name = "VST3: VXDenoiser (VX Suite)",
    file_stem = "VXDenoiser",
    binary = "VXDenoiser.vst3",
    unique_id = 1661487711,
    class_id = "ABCDEF019182FAEB565853315658444E",
  },
  deverb = {
    display_name = "VXDeverb (VX Suite)",
    library_name = "VST3: VXDeverb (VX Suite)",
    file_stem = "VXDeverb",
    binary = "VXDeverb.vst3",
    unique_id = 2064150567,
    class_id = "ABCDEF019182FAEB5658533156584456",
  },
  finish = {
    display_name = "VXFinish (VX Suite)",
    library_name = "VST3: VXFinish (VX Suite)",
    file_stem = "VXFinish",
    binary = "VXFinish.vst3",
    unique_id = 1124949677,
    class_id = "ABCDEF019182FAEB565853315658464E",
  },
  optocomp = {
    display_name = "VXOptoComp (VX Suite)",
    library_name = "VST3: VXOptoComp (VX Suite)",
    file_stem = "VXOptoComp",
    binary = "VXOptoComp.vst3",
    unique_id = 1576466783,
    class_id = "ABCDEF019182FAEB5658533156584F43",
  },
  proximity = {
    display_name = "VXProximity (VX Suite)",
    library_name = "VST3: VXProximity (VX Suite)",
    file_stem = "VXProximity",
    binary = "VXProximity.vst3",
    unique_id = 1866070543,
    class_id = "ABCDEF019182FAEB5658533156585052",
  },
  subtract = {
    display_name = "VXSubtract (VX Suite)",
    library_name = "VST3: VXSubtract (VX Suite)",
    file_stem = "VXSubtract",
    binary = "VXSubtract.vst3",
    unique_id = 87841608,
    class_id = "ABCDEF019182FAEB5658533156585342",
  },
  tone = {
    display_name = "VXTone (VX Suite)",
    library_name = "VST3: VXTone (VX Suite)",
    file_stem = "VXTone",
    binary = "VXTone.vst3",
    unique_id = 1127215343,
    class_id = "ABCDEF019182FAEB565853315658544E",
  },
}

local scenarios = {
  {
    name = "Camera Review - Far Phone",
    chain = { "subtract", "cleanup", "denoiser", "deepfilternet", "deverb", "proximity", "tone", "optocomp", "finish" },
    params = {
      subtract = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Learn"] = 0.0, ["Subtract"] = 0.22, ["Protect"] = 0.68 },
      cleanup = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Low Shelf"] = 1.0, ["High Shelf"] = 0.0, ["Cleanup"] = 0.38, ["Body"] = 0.56, ["Focus"] = 0.60 },
      denoiser = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Clean"] = 0.18, ["Guard"] = 0.72 },
      deepfilternet = { ["Model"] = 0.0, ["Listen"] = 0.0, ["Clean"] = 0.26, ["Guard"] = 0.62 },
      deverb = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Reduce"] = 0.20, ["Blend"] = 0.18 },
      proximity = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Closer"] = 0.24, ["Air"] = 0.14 },
      tone = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Bass"] = 0.54, ["Treble"] = 0.57 },
      optocomp = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Peak Red."] = 0.24, ["Body"] = 0.52, ["Gain"] = 0.55 },
      finish = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Finish"] = 0.14, ["Body"] = 0.52, ["Gain"] = 0.54 },
    },
  },
  {
    name = "Live Music - Front Of Room",
    chain = { "cleanup", "tone", "optocomp", "finish" },
    params = {
      subtract = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Learn"] = 0.0, ["Subtract"] = 0.0, ["Protect"] = 0.80 },
      cleanup = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Low Shelf"] = 0.0, ["High Shelf"] = 0.0, ["Cleanup"] = 0.10, ["Body"] = 0.60, ["Focus"] = 0.48 },
      denoiser = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Clean"] = 0.0, ["Guard"] = 0.80 },
      deepfilternet = { ["Model"] = 0.0, ["Listen"] = 0.0, ["Clean"] = 0.0, ["Guard"] = 0.80 },
      deverb = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Reduce"] = 0.05, ["Blend"] = 0.10 },
      proximity = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Closer"] = 0.0, ["Air"] = 0.0 },
      tone = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Bass"] = 0.52, ["Treble"] = 0.54 },
      optocomp = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Peak Red."] = 0.18, ["Body"] = 0.54, ["Gain"] = 0.52 },
      finish = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Finish"] = 0.10, ["Body"] = 0.54, ["Gain"] = 0.52 },
    },
  },
  {
    name = "Podcast Finishing - Clean Voice",
    chain = { "cleanup", "proximity", "tone", "optocomp", "finish" },
    params = {
      subtract = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Learn"] = 0.0, ["Subtract"] = 0.0, ["Protect"] = 0.80 },
      cleanup = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Low Shelf"] = 1.0, ["High Shelf"] = 0.0, ["Cleanup"] = 0.20, ["Body"] = 0.58, ["Focus"] = 0.54 },
      denoiser = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Clean"] = 0.0, ["Guard"] = 0.80 },
      deepfilternet = { ["Model"] = 0.0, ["Listen"] = 0.0, ["Clean"] = 0.0, ["Guard"] = 0.80 },
      deverb = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Reduce"] = 0.05, ["Blend"] = 0.10 },
      proximity = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Closer"] = 0.08, ["Air"] = 0.06 },
      tone = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Bass"] = 0.53, ["Treble"] = 0.56 },
      optocomp = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Peak Red."] = 0.20, ["Body"] = 0.54, ["Gain"] = 0.54 },
      finish = { ["Mode"] = 0.0, ["Listen"] = 0.0, ["Finish"] = 0.18, ["Body"] = 0.54, ["Gain"] = 0.56 },
    },
  },
  {
    name = "Mixed Audio - Voice + Guitar",
    chain = { "cleanup", "tone", "optocomp", "finish" },
    params = {
      subtract = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Learn"] = 0.0, ["Subtract"] = 0.0, ["Protect"] = 0.85 },
      cleanup = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Low Shelf"] = 0.0, ["High Shelf"] = 0.0, ["Cleanup"] = 0.16, ["Body"] = 0.60, ["Focus"] = 0.52 },
      denoiser = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Clean"] = 0.0, ["Guard"] = 0.85 },
      deepfilternet = { ["Model"] = 0.0, ["Listen"] = 0.0, ["Clean"] = 0.0, ["Guard"] = 0.85 },
      deverb = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Reduce"] = 0.0, ["Blend"] = 0.10 },
      proximity = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Closer"] = 0.0, ["Air"] = 0.04 },
      tone = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Bass"] = 0.50, ["Treble"] = 0.55 },
      optocomp = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Peak Red."] = 0.16, ["Body"] = 0.52, ["Gain"] = 0.53 },
      finish = { ["Mode"] = 1.0, ["Listen"] = 0.0, ["Finish"] = 0.10, ["Body"] = 0.52, ["Gain"] = 0.53 },
    },
  },
}

local report = {}
local plugin_order = {
  "cleanup",
  "deepfilternet",
  "denoiser",
  "deverb",
  "finish",
  "optocomp",
  "proximity",
  "subtract",
  "tone",
}

local function wrap_base64(text, indent)
  local lines = {}
  local width = 96
  local i = 1
  while i <= #text do
    lines[#lines + 1] = indent .. text:sub(i, i + width - 1)
    i = i + width
  end
  return table.concat(lines, "\n")
end

local function hash32(text, seed)
  local h = seed or 2166136261
  for i = 1, #text do
    h = (h * 16777619) % 4294967296
    h = (h + text:byte(i)) % 4294967296
  end
  return string.format("%08X", h)
end

local function fxid_for(scenario_name, plugin_key)
  local a = hash32("A|" .. scenario_name .. "|" .. plugin_key, 2166136261)
  local b = hash32("B|" .. scenario_name .. "|" .. plugin_key, 2166136261)
  local c = hash32("C|" .. scenario_name .. "|" .. plugin_key, 2166136261)
  local d = hash32("D|" .. scenario_name .. "|" .. plugin_key, 2166136261)
  return string.format("{%s-%s-%s-%s-%s}", a, b:sub(1, 4), b:sub(5, 8), c:sub(1, 4), c:sub(5, 8) .. d)
end

local function write_file(path, contents)
  local f = assert(io.open(path, "wb"))
  f:write(contents)
  f:close()
end

local function with_temp_track(fn)
  reaper.InsertTrackAtIndex(0, true)
  local tr = reaper.GetTrack(0, 0)
  local ok, result = pcall(fn, tr)
  reaper.DeleteTrack(tr)
  if not ok then
    error(result)
  end
  return result
end

local function set_params_for_fx(track, fx, params)
  for i = 0, reaper.TrackFX_GetNumParams(track, fx) - 1 do
    local _, name = reaper.TrackFX_GetParamName(track, fx, i)
    local value = params[name]
    if value ~= nil then
      reaper.TrackFX_SetParamNormalized(track, fx, i, value)
    end
  end
end

local function add_fx_chunk(track, plugin_key, params)
  local plugin = plugins[plugin_key]
  local fx = reaper.TrackFX_AddByName(track, plugin.display_name, false, -1)
  assert(fx >= 0, "Plugin not found in REAPER: " .. plugin.display_name)
  set_params_for_fx(track, fx, params)
  local ok, chunk = reaper.TrackFX_GetNamedConfigParm(track, fx, "vst_chunk")
  assert(ok and chunk and #chunk > 0, "Failed to fetch vst_chunk for " .. plugin.display_name)
  return fx, chunk
end

local function build_rpl_for_plugin(plugin_key)
  local plugin = plugins[plugin_key]
  local parts = { string.format("<REAPER_PRESET_LIBRARY `%s`\n", plugin.library_name) }
  with_temp_track(function(track)
    for _, scenario in ipairs(scenarios) do
      local _, chunk = add_fx_chunk(track, plugin_key, scenario.params[plugin_key])
      parts[#parts + 1] = string.format("\n <PRESET `%s`\n", scenario.name)
      parts[#parts + 1] = wrap_base64(chunk, " ")
      parts[#parts + 1] = "\n >\n"
      reaper.TrackFX_Delete(track, 0)
    end
  end)
  parts[#parts + 1] = ">\n"
  write_file(rpl_dir .. "/" .. plugin.file_stem .. ".RPL", table.concat(parts))
  report[#report + 1] = "Generated RPL for " .. plugin.file_stem
end

local function build_fxchain_block(plugin_key, preset_name, params)
  local plugin = plugins[plugin_key]
  return with_temp_track(function(track)
    local _, chunk = add_fx_chunk(track, plugin_key, params)
    local parts = {
      "BYPASS 0 0 0\n",
      string.format("<VST \"%s\" %s 0 \"\" %d{%s} \"\"\n", plugin.library_name, plugin.binary, plugin.unique_id, plugin.class_id),
      wrap_base64(chunk, "  "),
      "\n>\n",
      "FXID " .. fxid_for(preset_name, plugin_key) .. "\n",
      string.format("PRESETNAME \"%s\"\n", preset_name),
      "WAK 0 0\n",
    }
    return table.concat(parts)
  end)
end

local function build_fxchain_for_scenario(scenario)
  local parts = {}
  for _, plugin_key in ipairs(scenario.chain) do
    parts[#parts + 1] = build_fxchain_block(plugin_key, scenario.name, scenario.params[plugin_key])
  end
  local path = string.format("%s/VX Suite - %s.RfxChain", fxchain_dir, scenario.name)
  write_file(path, table.concat(parts))
  report[#report + 1] = "Generated FX chain for " .. scenario.name
end

local ok, err = xpcall(function()
  for _, plugin_key in ipairs(plugin_order) do
    build_rpl_for_plugin(plugin_key)
  end

  table.sort(report)

  for _, scenario in ipairs(scenarios) do
    build_fxchain_for_scenario(scenario)
  end

  write_file(report_path, table.concat(report, "\n") .. "\n")
  reaper.ShowConsoleMsg("VX Suite REAPER preset pack generated in " .. output_root .. "\n")
end, debug.traceback)

if not ok then
  write_file(report_path, "ERROR\n" .. tostring(err) .. "\n")
  reaper.ShowConsoleMsg("VX Suite REAPER preset generation failed:\n" .. tostring(err) .. "\n")
end
