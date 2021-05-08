function init()
  connect(g_game, {
    onGameStart = online,
    onGameEnd = offline
  })
  if g_game.isOnline() then online() end
end

function terminate()
  disconnect(g_game, {
    onGameStart = online,
    onGameEnd = offline
  })
  offline()
end

function online()
  g_keyboard.bindKeyDown('Ctrl+R', toggleMount)
end

function offline()
  g_keyboard.unbindKeyDown('Ctrl+R')
end

function toggleMount()
  local player = g_game.getLocalPlayer()
  if player then
    player:toggleMount()
  end
end

function mount()
  local player = g_game.getLocalPlayer()
  if player then
    player:mount()
  end
end

function dismount()
  local player = g_game.getLocalPlayer()
  if player then
    player:dismount()
  end
end
