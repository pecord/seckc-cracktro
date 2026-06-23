-- Headless screenshot: wait N vsyncs, dump framebuffer as a PPM, quit.
-- Lua 5.1 / LuaJIT safe (no bitwise operators).

local TARGET = tonumber(os.getenv('SHOT_FRAME') or '200')
local OUT    = os.getenv('SHOT_OUT') or '/tmp/rave.ppm'
local frames = 0
local done   = false

local function dump()
  if done then return end
  done = true
  local shot = PCSX.GPU.takeScreenShot()
  local w, h, bpp = shot.width, shot.height, shot.bpp
  print('SHOT_BPP ' .. tostring(bpp) .. ' size ' .. w .. 'x' .. h)
  -- Dump raw framebuffer (16bpp RGB555 little-endian); expand to PNG in Python.
  print('SHOT_WH ' .. w .. ' ' .. h)
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
