# RemoteConfig

Remote configs, scheduled launches and A/B tests for an o2 game: the client of the o2 live-ops
service (`o2portal/liveops`), managed from the portal's **Live-ops** tab.

## How it works

1. The game asks the service **which configs this player gets**. The answer is small — ids, not
   data: for every config a chain of document ids, plus the player's A/B groups and the switches
   scheduled for the next seven days.
2. The game downloads the documents it does not have yet **by id from the CDN**. Documents are
   immutable and named by their content, so they are cached forever, shared between configs and
   never re-downloaded.
3. A config is its chain merged in order: the base document, then one JSON merge patch
   ([RFC 7386](https://www.rfc-editor.org/rfc/rfc7386)) per A/B group the player is in. A player can
   be in any number of experiments at once; the service guarantees at publish time that experiments
   which can share a player do not write to the same place.

Everything is kept in `o2libs::Storage`, so the next run starts from the last answer at once and an
offline player still gets a scheduled event on time. A new answer is applied only when all of its
documents are in — never half of one published version and half of another. The exact wire format is
in [PROTOCOL.md](PROTOCOL.md).

## Setup

The portal's Live-ops tab → **Connect** writes `Assets/LiveOps.json`:

```json
{ "url": "https://…/liveops", "key": "o2c_…", "appVersion": "1.0", "build": 1 }
```

`O2LIBS_START` reads it, brings up the stored configs and starts a fetch. Nothing else is needed; a
game with its own rules calls `RemoteConfig::Init(url, key)` and `Fetch` itself. The key is not a
secret — it only says which project is asking.

## Scripts

```js
var pass  = o2libs.RemoteConfig.Get("season_pass");              // the whole config, or undefined
var price = o2libs.RemoteConfig.GetNumber("offers", "starter.price", 4.99);
var skin  = o2libs.RemoteConfig.GetString("season_pass", "rewards.skin", "default");
var on    = o2libs.RemoteConfig.GetBool("weekend_rush", "active", false);

o2libs.RemoteConfig.OnChanged(function () { /* a fetch finished or a scheduled launch began */ });

o2libs.RemoteConfig.SetNumberAttribute("level", 12);             // targeting; sent with the next fetch
o2libs.RemoteConfig.SetAttribute("country", "GE");

if (o2libs.RemoteConfig.GetGroup("new_tutorial") == "b") { }     // when the code itself branches
```

Paths are dotted and reach into arrays: `"rewards.0.coins"`. A missing config, a missing path or a
value of another type gives the default.

## C++

```cpp
#include "o2libs/o2libs.h"

int levels = o2RemoteConfig.Get<int>("season_pass", "levels", 30);

SeasonPass pass;                                  // any ISerializable
o2RemoteConfig.GetObject("season_pass", pass);

const DataValue& whole = o2RemoteConfig.GetConfig("season_pass");
o2RemoteConfig.SetAttribute("level", 12.0);
o2RemoteConfig.onChanged = []() { … };            // or RemoteConfig::OnChanged for several listeners
```

A component that follows a config without subscribing compares `o2RemoteConfig.GetRevision()` with the one
it read at - see `RotatorComponent` in the o2 template. Tests of game code put a document in place of a
config with `SetLocalOverride(key, data)` / `ClearLocalOverride()`: no service, no network, and the same
calls make a debug menu.

`o2RemoteConfig` is the game's `RemoteConfigClient`. A client can also be made by hand with its own
transport, storage and clock — that is how the tests run it.

## Exposure

Reading a config that an experiment shaped — or calling `GetGroup` — is the moment the player meets
the experiment. The client reports it once per experiment and group, in batches, surviving restarts
and lost connections; the portal shows *in the group* and *exposed* side by side.

## Platforms

HTTPS comes from the engine's platform backends: macOS, iOS, Android and the browser. On Windows and
Linux the engine has plain HTTP only, so the client works there against an `http://` address
(a local service, a development build).
