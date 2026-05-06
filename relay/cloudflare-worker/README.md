# WindroseTextSigns Relay Prototype

This is the first-pass outbound relay for internet client/server sync.

The mod should use the same room ID on client and server:

```text
wts-v1-<worldIslandId-lowercase>
```

The Windrose server logs expose `WorldIslandId`, and clients expose the same value in `R5.log` as:

```text
BL connected. IslandId '<worldIslandId>'
```

## Prototype Flow

Server:

1. `POST /v1/rooms/<roomId>/server/snapshot`
2. `GET /v1/rooms/<roomId>/server/requests?after=<seq>`

Client:

1. `GET /v1/rooms/<roomId>/client/snapshot`
2. `POST /v1/rooms/<roomId>/client/requests`

The Durable Object is only a room router/cache. The game server remains authoritative.

## Deploy Notes

This scaffold is not wired into the C++ mod yet. Keep `WTS_RELAY_ENABLED=false` until the HTTP client pass is implemented.

```powershell
cd "C:\Users\User\Documents\Windrose Addons\WindroseTextSigns\relay\cloudflare-worker"
npm install
npx wrangler deploy
```

