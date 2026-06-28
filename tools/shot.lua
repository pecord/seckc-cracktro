-- Legacy PCSX-Redux screenshot helper: wait N vsyncs, dump raw RGB555.
-- tools/render.sh uses DuckStation instead because this PCSX build returns
-- empty screenshots in headless mode.
-- Lua 5.1 / LuaJIT safe (no bitwise operators).

local TARGET = tonumber(os.getenv('SHOT_FRAME') or '200')
local OUT    = os.getenv('SHOT_OUT') or '/tmp/rave.ppm'
local READY  = os.getenv('SHOT_READY') or '/tmp/rave.ready'
local frames = 0
local done   = false

local function mark_ready_for_vram()
  local f = io.open(READY, 'wb')
  if f then
    f:write('ready\n')
    f:close()
  end
  print('SHOT_READY ' .. READY)
end

local function dump()
  if done then return end
  done = true
  PCSX.pauseEmulator()

  local shot = PCSX.GPU.takeScreenShot()
  local w, h, bpp = tonumber(shot.width or 0), tonumber(shot.height or 0), tonumber(shot.bpp or 0)
  print('SHOT_BPP ' .. tostring(bpp) .. ' size ' .. w .. 'x' .. h)
  print('SHOT_WH ' .. w .. ' ' .. h)

  if w <= 0 or h <= 0 or bpp <= 0 or shot.data == nil then
    -- Some headless PCSX-Redux builds return an empty screenshot while VRAM
    -- is valid. Leave the emulator paused so an external harness can fall back.
    mark_ready_for_vram()
    return
  end

  -- Dump raw framebuffer (16bpp RGB555 little-endian); expand to PNG in Python.
  local f = Support.File.open(OUT, 'TRUNCATE')
  f:writeMoveSlice(shot.data)
  f:close()
  print('SHOT_DONE ' .. OUT)
  PCSX.quit(0)
end

PCSX.Events.createEventListener('GPU::Vsync', function()
  frames = frames + 1
  if frames == 1 then print('VSYNC_FIRING') end
  if frames % 60 == 0 then print('FRAME ' .. frames) end
  if frames >= TARGET then dump() end
end)
print('SHOT_LUA_LOADED target=' .. TARGET)
