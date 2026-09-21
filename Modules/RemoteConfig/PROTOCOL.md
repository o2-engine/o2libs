# The live-ops protocol, as the game sees it

Three requests. Bodies are JSON sent as `text/plain` — a "simple" request, so a browser build does
not pay a CORS preflight round trip. Times are seconds since the epoch, UTC.

## `POST <url>/v1/fetch`

```json
{ "key": "o2c_…", "player": "59693faf…", "platform": "web", "app": "1.2.0", "build": 12,
  "attrs": { "level": 7, "country": "GE", "payer": true }, "etag": "30e8041abb405ab9" }
```

`player` is `o2libs::PlayerIdentity` — random, made on the first run — or the game's own account id.
`attrs` are the game's words about the player: numbers, strings and flags, used by conditions. The
country the request came from beats the one in `attrs` when the service knows it. `etag` is the one
of the answer the client holds, if any.

```json
{ "etag": "30e8041abb405ab9", "ttl": 900, "time": 1789983071, "cdn": "https://…/liveops/c/<project>",
  "configs":     { "season_pass": ["797e5aad…", "4de7cdfe…"], "economy": ["d3bddaba…"] },
  "experiments": { "pass_rewards": "rich" },
  "exposure":    { "season_pass": ["pass_rewards"] },
  "timeline":    [ { "at": 1790035200, "configs": { "weekend_rush": ["78953314…"] } },
                   { "at": 1790236800, "configs": { "weekend_rush": ["78b558bd…"] } } ] }
```

- `configs` — for every config the player has **now**, its chain: the base document, then the merge
  patches of the player's groups, in the order to apply them.
- `timeline` — every moment within the next seven days at which something the player has changes,
  in time order, with the new chains of just the configs that change; an empty chain removes the
  config. The client downloads these documents with the rest and switches on time by itself.
- `exposure` — which experiments shaped which config; reading that config is the exposure.
- `time` corrects the device clock for the timeline; `ttl` is when to ask again.

The same `etag` back: `{ "unchanged": true, "etag": "…", "ttl": 900, "time": … }`.

Errors: `401` unknown key, `400` malformed, `429` too many requests. The client keeps what it has
and asks again later.

## `GET <cdn>/<id>.json`

An immutable document (`Cache-Control: immutable`), `id` = 32 hex characters, the first 128 bits of
the SHA-256 of its canonical JSON. Anyone who knows an id can read the document; ids cannot be
guessed, but configs are not the place for secrets.

## `POST <url>/v1/events`

```json
{ "key": "o2c_…", "player": "…", "events": [ { "type": "exposure", "experiment": "pass_rewards", "group": "rich", "at": 1789983080 } ] }
```

At most 100 events. The service counts an exposure once per player and experiment, and only for the
group it gave that player itself. A `4xx` means "never resend these".

## Merging

`o2libs/Core/JsonMergePatch` and the service's `merge.ts` are the same algorithm and run the same
vectors: `Tests/merge-vectors.json`.

## `POST <url>/v1/saves/sync` (the Saves module)

```json
{ "key": "o2c_…", "player": "…", "platform": "ios", "app": "1.2.0", "rev": 3, "changed": true, "updatedAt": 1789983080,
  "save": { "format": 1, "player": "…", "updatedAt": 1789983080,
            "sections": { "progress": { "v": 1, "data": { "level": 12 } }, "wallet": { "v": 2, "data": { "soft": 400 } } } } }
```

`rev` is the revision the game holds (0: never synchronized), `save` goes only when `changed`. The answer is
`{ "rev": 4 }` when the game's save was stored or nothing was to be done, or `{ "rev": 5, "save": { … } }` when the
service's copy wins and the game has to take it: the save was edited on the portal, or the game holds an older
revision and has no changes of its own. A save is at most 256 KB.
