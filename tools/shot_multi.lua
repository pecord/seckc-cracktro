-- Multi-capture: dump raw framebuffer at several vsync targets in ONE run.
-- SHOT_FRAMES="300,700,1100" SHOT_DIR="/tmp/frames" (writes <DIR>/<n>.raw)
local list = {}
for n in string.gmatch(os.getenv('SHOT_FRAMES') or '300', '%d+') do
  list[#list + 1] = tonumber(n)
end
local DIR  = os.getenv('SHOT_DIR') or '/tmp/frames'
local idx  = 1
local frames = 0

PCSX.Events.createEventListener('GPU::Vsync', function()
  frames = frames + 1
  while idx <= #list and frames >= list[idx] do
    local shot = PCSX.GPU.takeScreenShot()
    local f = Support.File.open(DIR .. '/' .. list[idx] .. '.raw', 'TRUNCATE')
    f:writeMoveSlice(shot.data)
    f:close()
    print('SHOT ' .. list[idx] .. ' ' .. shot.width .. 'x' .. shot.height)
    idx = idx + 1
  end
  if idx > #list then PCSX.quit(0) end
end)
print('MULTI_LOADED ' .. (os.getenv('SHOT_FRAMES') or ''))
