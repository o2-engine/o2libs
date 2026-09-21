# Saves

The player's save. One entry point, `o2Saves`.

A save is made of **sections** - the campaign, the wallet, the settings - each a serializable object of the
game. The whole save is one JSON document under the player's identity (`o2PlayerIdentity`), written to the
writable folder of the application on every platform - `localStorage` in the browser - through `o2Storage`.
A section this build of the game does not know is kept as it is, so modules of a game come and go without
losing anybody's progress.

```cpp
class ProgressSave: public SaveSection
{
public:
    int            level = 1; // @SERIALIZABLE
    Vector<String> unlocked;  // @SERIALIZABLE

    SERIALIZABLE(ProgressSave);
};

auto progress = o2Saves.GetSection<ProgressSave>("progress");   // created and read on first use
progress->level++;
o2Saves.MarkChanged();      // written locally in a moment, sent to the service a little later
```

`Save()` writes now; the destructor writes what is unsaved. A section whose layout changed returns a higher
`GetVersion()` and brings older data up in `OnMigrate(fromVersion, data)` before it is read; `ResetToDefaults()`
and `OnLoaded()` are there to override.

Scripts keep plain objects:

```js
var progress = o2libs.Saves.Get("progress") || { level: 1 };
progress.level++;
o2libs.Saves.Set("progress", progress);
```

## The service and the portal

When the project is connected (`Assets/LiveOps.json`, the portal's Live-ops tab → Connect) the save is also
kept by the live-ops service: `Coroutine<bool> Sync()` sends it when it changed and takes the service's copy
when that one wins - a new device of the same player, or a save **edited on the portal's Saves tab**, which
replaces the local one whatever the game has. The game's section objects stay the same objects, their content
is replaced, and `onReplaced` is called. Without a connection the save is local only and `Sync()` completes
with `false`. Sync runs by itself `syncPeriod` seconds after a change and at start.

The player id is what the portal finds a save by: `o2PlayerIdentity.GetId()`, or the id of the game's own
account after `SetId`.
