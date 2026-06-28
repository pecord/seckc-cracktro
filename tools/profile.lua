-- Headless profiler: read the demo's per-frame timing (written to g_perf in RAM
-- by the PERF_HUD build) and print it to stdout. PCSX-Redux models PS1 timing,
-- so this reflects the console budget far better than the browser core.
--   work/total/peak are hblank counts; an NTSC field is ~263 hblanks.
-- Run: PCSX-Redux -no-ui -run -interpreter -bios openbios.bin -exe rave.exe \
--      -dofile tools/profile.lua -stdout
local ffi = require('ffi')
local OFF = tonumber(os.getenv('PERF_OFF') or '0x3e484')   -- g_perf & 0x1fffff
local START = tonumber(os.getenv('PERF_START') or '600')
local frames, dumped = 0, 0

local function r32(o)
  local m = PCSX.getMemPtr()
  if m == nil then return -1 end
  return tonumber(ffi.cast('uint32_t*', m + o)[0])
end

PCSX.Events.createEventListener('GPU::Vsync', function()
  frames = frames + 1
  if frames >= START and frames % 20 == 0 then
    local w, t, pk, fc = r32(OFF), r32(OFF + 4), r32(OFF + 8), r32(OFF + 12)
    local fps = (t and t > 0) and math.floor(60 * 263 / t) or 0
    local pct = math.floor((w or 0) * 100 / 263)
    print(string.format('PERF work=%d total=%d peak=%d budget=%d%% fps=%d framecount=%d', w, t, pk, pct, fps, fc))
    dumped = dumped + 1
  end
  if dumped >= 6 then PCSX.quit(0) end
end)
print('PROFILE_LUA_LOADED off=' .. string.format('0x%x', OFF))
